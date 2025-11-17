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
// Created by Longda on 2021/4/13.
//

#include <memory>
#include "sql/operator/hash_join_physical_operator.h"
#include "common/log/log.h"
#include "sql/expr/expression.h"
#include "sql/parser/parse_defs.h"
#include "common/value.h"
#include "storage/field/field.h"
#include "storage/field/field_meta.h"
#include "storage/table/table.h"
#include "sql/operator/table_scan_physical_operator.h"
#include "sql/operator/index_scan_physical_operator.h"
#include "common/sys/rc.h"
#include "common/lang/vector.h"
#include "common/lang/string.h"

#include "storage/trx/trx.h"
#include <unordered_map>
#include <string>

RC HashJoinPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 2) {
    LOG_WARN("hash join operator should have 2 children");
    return RC::INTERNAL;
  }

  RC rc = RC::SUCCESS;
  left_  = children_[0].get();
  right_ = children_[1].get();
  trx_   = trx;

  // 打开左表（构建哈希表的一方）
  rc = left_->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open left operator. rc=%s", strrc(rc));
    return rc;
  }

  // 获取左表和右表的名称
  string left_table_name;
  string right_table_name;
  
  // 获取左表名称
  if (children_.size() >= 1 && children_[0]) {
    auto *left_scan_oper = dynamic_cast<TableScanPhysicalOperator *>(children_[0].get());
    if (left_scan_oper) {
      left_table_name = left_scan_oper->param();
    }
  }
  
  // 获取右表名称
  if (children_.size() >= 2 && children_[1]) {
    auto *right_scan_oper = dynamic_cast<TableScanPhysicalOperator *>(children_[1].get());
    if (right_scan_oper) {
      right_table_name = right_scan_oper->param();
    }
  }
  
  LOG_INFO("Hash Join: left_table=%s, right_table=%s", left_table_name.c_str(), right_table_name.c_str());
  
  // 构建哈希表
  while ((rc = left_->next()) == RC::SUCCESS) {
    left_tuple_ = left_->current_tuple();
    if (left_tuple_ == nullptr) {
      continue;
    }

    // 从JOIN条件中提取连接键
    for (auto &predicate : predicates_) {
      if (predicate->type() == ExprType::COMPARISON) {
        auto *comp_expr = dynamic_cast<ComparisonExpr *>(predicate.get());
        if (comp_expr && comp_expr->comp() == CompOp::EQUAL_TO) {
          // 获取连接键值
          Value join_key_value;
          // 获取左右表达式
          auto &left_expr = comp_expr->left();
          auto &right_expr = comp_expr->right();
          
          // 确定哪个表达式是左表字段
          Expression *left_field_expr = nullptr;
          if (left_expr->type() == ExprType::FIELD) {
            auto *field_expr = dynamic_cast<FieldExpr *>(left_expr.get());
            if (field_expr && field_expr->field().table()) {
              // 检查这个字段是否属于左表
              if (left_table_name == field_expr->field().table_name()) {
                left_field_expr = left_expr.get();
                LOG_INFO("Hash Join: Found left table field: %s.%s", 
                         field_expr->field().table_name(), field_expr->field().field_name());
              }
            }
          } else if (right_expr->type() == ExprType::FIELD) {
            auto *field_expr = dynamic_cast<FieldExpr *>(right_expr.get());
            if (field_expr && field_expr->field().table()) {
              // 检查这个字段是否属于左表
              if (left_table_name == field_expr->field().table_name()) {
                left_field_expr = right_expr.get();
                LOG_INFO("Hash Join: Found left table field: %s.%s", 
                         field_expr->field().table_name(), field_expr->field().field_name());
              }
            }
          }
          
          if (left_field_expr) {
            RC value_rc = left_field_expr->get_value(*left_tuple_, join_key_value);
            if (value_rc == RC::SUCCESS) {
              string key = join_key_value.to_string();
              hash_table_[key].push_back(left_tuple_);
              LOG_INFO("Hash Join: Added left tuple with key=%s to hash table", key.c_str());
            } else {
              LOG_WARN("Hash Join: Failed to get value from left tuple for key field");
            }
          }
        }
      }
    }
  }

  if (rc != RC::RECORD_EOF) {
    LOG_WARN("failed to scan left table. rc=%s", strrc(rc));
    return rc;
  }

  LOG_INFO("Hash Join: Built hash table with %zu entries", hash_table_.size());
  hash_built_ = true;

  // 打开右表（探测哈希表的一方）
  rc = right_->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open right operator. rc=%s", strrc(rc));
    return rc;
  }

  // 初始化迭代器
  current_tuple_index_ = 0;
  current_right_tuples_.clear();

  return RC::SUCCESS;
}

RC HashJoinPhysicalOperator::next()
{
  if (!hash_built_) {
    LOG_WARN("hash table not built yet");
    return RC::INTERNAL;
  }

  // 获取左表和右表的名称
  string left_table_name;
  string right_table_name;
  
  // 获取左表名称
  if (children_.size() >= 1 && children_[0]) {
    auto *left_scan_oper = dynamic_cast<TableScanPhysicalOperator *>(children_[0].get());
    if (left_scan_oper) {
      left_table_name = left_scan_oper->param();
    }
  }
  
  // 获取右表名称
  if (children_.size() >= 2 && children_[1]) {
    auto *right_scan_oper = dynamic_cast<TableScanPhysicalOperator *>(children_[1].get());
    if (right_scan_oper) {
      right_table_name = right_scan_oper->param();
    }
  }

  while (true) {
    // 如果当前还有匹配的左表元组需要处理
    if (!current_right_tuples_.empty() && current_tuple_index_ < current_right_tuples_.size()) {
      left_tuple_ = current_right_tuples_[current_tuple_index_];
      joined_tuple_.set_left(left_tuple_);
      joined_tuple_.set_right(right_tuple_);
      
      // 评估所有JOIN条件
      if (evaluate_join_conditions()) {
        current_tuple_index_++;
        return RC::SUCCESS;  // 找到匹配的行
      }
      
      // 条件不满足，继续下一个左表元组
      current_tuple_index_++;
      continue;
    }

    // 当前右表行的所有左表匹配元组处理完毕，获取下一个右表行
    current_right_tuples_.clear();
    current_tuple_index_ = 0;
    
    RC rc = right_->next();
    if (rc == RC::SUCCESS) {
      right_tuple_ = right_->current_tuple();
      
      // 从JOIN条件中提取连接键
      for (auto &predicate : predicates_) {
        if (predicate->type() == ExprType::COMPARISON) {
          auto *comp_expr = dynamic_cast<ComparisonExpr *>(predicate.get());
          if (comp_expr && comp_expr->comp() == CompOp::EQUAL_TO) {
            // 获取连接键值
            Value join_key_value;
            // 获取左右表达式
            auto &left_expr = comp_expr->left();
            auto &right_expr = comp_expr->right();
            
            // 确定哪个表达式是右表字段
            Expression *right_field_expr = nullptr;
            if (left_expr->type() == ExprType::FIELD) {
              auto *field_expr = dynamic_cast<FieldExpr *>(left_expr.get());
              if (field_expr && field_expr->field().table()) {
                // 检查这个字段是否属于右表
                if (right_table_name == field_expr->field().table_name()) {
                  right_field_expr = left_expr.get();
                  LOG_INFO("Hash Join: Found right table field: %s.%s", 
                           field_expr->field().table_name(), field_expr->field().field_name());
                }
              }
            } else if (right_expr->type() == ExprType::FIELD) {
              auto *field_expr = dynamic_cast<FieldExpr *>(right_expr.get());
              if (field_expr && field_expr->field().table()) {
                // 检查这个字段是否属于右表
                if (right_table_name == field_expr->field().table_name()) {
                  right_field_expr = right_expr.get();
                  LOG_INFO("Hash Join: Found right table field: %s.%s", 
                           field_expr->field().table_name(), field_expr->field().field_name());
                }
              }
            }
            
            if (right_field_expr) {
              RC value_rc = right_field_expr->get_value(*right_tuple_, join_key_value);
              if (value_rc == RC::SUCCESS) {
                string key = join_key_value.to_string();
                LOG_INFO("Hash Join: Looking for key=%s in hash table (size=%zu)", key.c_str(), hash_table_.size());
                auto it = hash_table_.find(key);
                if (it != hash_table_.end()) {
                  LOG_INFO("Hash Join: Found %zu matching tuples for key=%s", it->second.size(), key.c_str());
                  // 保存所有匹配的左表元组
                  current_right_tuples_ = it->second;
                } else {
                  LOG_INFO("Hash Join: No matching tuples found for key=%s", key.c_str());
                }
              } else {
                LOG_WARN("Hash Join: Failed to get value from right tuple for key field");
              }
            }
          }
        }
      }
      
      // 如果没有找到匹配，继续下一行右表
      if (current_right_tuples_.empty()) {
        continue;
      }
    } else if (rc == RC::RECORD_EOF) {
      // 右表扫描完毕
      return RC::RECORD_EOF;
    } else {
      LOG_WARN("failed to get next tuple from right table. rc=%s", strrc(rc));
      return rc;
    }
  }
}

RC HashJoinPhysicalOperator::close()
{
  RC rc = left_->close();
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to close left operator. rc=%s", strrc(rc));
  }

  rc = right_->close();
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to close right operator. rc=%s", strrc(rc));
  }

  // 清理哈希表
  hash_table_.clear();
  hash_built_ = false;
  current_right_tuples_.clear();

  return RC::SUCCESS;
}

Tuple *HashJoinPhysicalOperator::current_tuple()
{
  return &joined_tuple_;
}

void HashJoinPhysicalOperator::set_predicates(vector<unique_ptr<Expression>> &&predicates)
{
  // 清除现有条件
  predicates_.clear();
  
  // 逐个添加条件，避免直接移动整个vector可能导致的内存问题
  for (auto &pred : predicates) {
    if (pred) {
      predicates_.push_back(std::move(pred));
    }
  }
}

bool HashJoinPhysicalOperator::evaluate_join_conditions()
{
  if (predicates_.empty()) {
    return true;
  }

  // 创建复合元组，包含左右两个元组
  // 这样JOIN条件可以同时引用两个表的字段
  JoinedTuple composite_tuple;
  composite_tuple.set_left(left_tuple_);
  composite_tuple.set_right(right_tuple_);

  for (auto &predicate : predicates_) {
    Value value;
    RC rc = predicate->get_value(composite_tuple, value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to evaluate join condition. rc=%s", strrc(rc));
      return false;
    }
    
    if (!value.get_boolean()) {
      return false;
    }
  }
  
  return true;
}