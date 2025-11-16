/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2023/4/25.
//

#include "sql/executor/update_executor.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/update_stmt.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"
#include "storage/record/record.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/common/condition_filter.h"
#include "storage/record/record_scanner.h"
#include "storage/field/field_meta.h"  // 修复FieldMeta头文件路径
#include "sql/expr/expression.h"      // 添加Expression头文件
#include "sql/expr/tuple.h"           // 修复：RowTuple在tuple.h中定义

RC UpdateExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt    *stmt    = sql_event->stmt();
  Session *session = sql_event->session_event()->session();
  ASSERT(stmt->type() == StmtType::UPDATE,
      "update executor can not run this command: %d",
      static_cast<int>(stmt->type()));

  UpdateStmt *update_stmt = static_cast<UpdateStmt *>(stmt);

  Table *table = update_stmt->table();
  const char *attribute_name = update_stmt->attribute_name();
  Value *values = update_stmt->values();
  FilterStmt *filter_stmt = update_stmt->filter_stmt();
  
  // 获取要更新的字段元数据
  const TableMeta &table_meta = table->table_meta();
  const FieldMeta *field_meta = table_meta.field(attribute_name);
  if (nullptr == field_meta) {
    LOG_ERROR("Field not found: %s in table %s", attribute_name, table->name());
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  // 从FilterStmt创建Expression列表用于条件过滤
  vector<unique_ptr<Expression>> predicates;
  if (filter_stmt != nullptr) {
    const vector<FilterUnit *> &filter_units = filter_stmt->filter_units();
    for (const FilterUnit *filter_unit : filter_units) {
      const FilterObj &filter_obj_left = filter_unit->left();
      const FilterObj &filter_obj_right = filter_unit->right();

      unique_ptr<Expression> left(filter_obj_left.is_attr
                                      ? static_cast<Expression *>(new FieldExpr(filter_obj_left.field))
                                      : static_cast<Expression *>(new ValueExpr(filter_obj_left.value)));

      unique_ptr<Expression> right(filter_obj_right.is_attr
                                       ? static_cast<Expression *>(new FieldExpr(filter_obj_right.field))
                                       : static_cast<Expression *>(new ValueExpr(filter_obj_right.value)));

      ComparisonExpr *cmp_expr = new ComparisonExpr(filter_unit->comp(), std::move(left), std::move(right));
      predicates.emplace_back(cmp_expr);
    }
  }

  // 获取记录扫描器
  RecordScanner *scanner = nullptr;
  RC rc = table->get_record_scanner(scanner, session->current_trx(), ReadWriteMode::READ_WRITE);
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to get record scanner for table %s", table->name());
    return rc;
  }

  // 打开扫描器
  rc = scanner->open_scan();
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to open record scanner for table %s", table->name());
    delete scanner;
    return rc;
  }

  int updated_count = 0;
  Record record;
  
  // 遍历所有记录
  while (true) {
    rc = scanner->next(record);
    if (rc == RC::RECORD_EOF) {
      rc = RC::SUCCESS;
      break;
    }
    if (rc != RC::SUCCESS) {
      LOG_ERROR("Failed to get next record from scanner");
      break;
    }

    // 检查是否满足过滤条件
    bool should_update = true;
    if (!predicates.empty()) {
      // 创建RowTuple用于表达式计算
      RowTuple tuple;
      tuple.set_record(&record);
      tuple.set_schema(table, table->table_meta().field_metas());

      // 检查所有条件是否都满足
      Value value;
      for (unique_ptr<Expression> &expr : predicates) {
        rc = expr->get_value(tuple, value);
        if (rc != RC::SUCCESS) {
          LOG_ERROR("Failed to evaluate filter expression");
          should_update = false;
          break;
        }

        bool expr_result = value.get_boolean();
        if (!expr_result) {
          should_update = false;
          break;
        }
      }
    }

    // 如果不满足条件，跳过此记录
    if (!should_update) {
      continue;
    }

    // 创建新记录，更新指定字段
    Record new_record;
    char *new_data = (char *)malloc(record.len());
    memcpy(new_data, record.data(), record.len());
    new_record.set_data_owner(new_data, record.len());
    new_record.set_rid(record.rid());

    // 更新字段值
    if (values != nullptr && update_stmt->value_amount() > 0) {
      // 获取字段在记录中的偏移量
      int field_offset = field_meta->offset();
      
      // 根据字段类型更新值
      switch (field_meta->type()) {
        case AttrType::INTS: {
          int int_value = values[0].get_int();
          memcpy(new_data + field_offset, &int_value, sizeof(int));
          break;
        }
        case AttrType::FLOATS: {
          float float_value = values[0].get_float();
          memcpy(new_data + field_offset, &float_value, sizeof(float));
          break;
        }
        case AttrType::CHARS: {
          // 修复：将临时字符串保存到局部变量中，避免指针失效
          std::string char_value_str = values[0].get_string();
          const char *char_value = char_value_str.c_str();
          int field_len = field_meta->len();
          strncpy(new_data + field_offset, char_value, field_len);
          // 确保字符串以null结尾
          if (strlen(char_value) < (size_t)field_len) {
            new_data[field_offset + strlen(char_value)] = '\0';
          }
          break;
        }
        case AttrType::DATES: {
          int date_value = values[0].get_date();
          memcpy(new_data + field_offset, &date_value, sizeof(int));
          break;
        }
        default: {
          LOG_ERROR("Unsupported field type: %d", field_meta->type());
          free(new_data);
          rc = RC::UNSUPPORTED;
          break;
        }
      }
    }

    if (rc != RC::SUCCESS) {
      free(new_data);
      break;
    }

    // 调用表的更新方法
    rc = table->update_record_with_trx(record, new_record, session->current_trx());
    if (rc != RC::SUCCESS) {
      LOG_ERROR("Failed to update record in table %s", table->name());
      // 注意：不要手动free(new_data)，因为Record对象会负责释放
      break;
    }

    updated_count++;
    // 注意：不要手动free(new_data)，因为Record对象会负责释放
  }

  // 关闭扫描器
  scanner->close_scan();
  delete scanner;

  if (rc == RC::SUCCESS) {
    LOG_INFO("Update completed. %d records updated in table %s", updated_count, table->name());
    // 只需要设置返回码为SUCCESS，系统会自动处理返回消息
    sql_event->session_event()->sql_result()->set_return_code(RC::SUCCESS);
  }

  return rc;
}