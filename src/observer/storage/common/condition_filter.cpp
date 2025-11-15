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
// Created by Wangyunlai on 2021/5/7.
//

#include "condition_filter.h"
#include "common/log/log.h"
#include "common/value.h"
#include "storage/record/record_manager.h"
#include "storage/table/table.h"
#include <math.h>
#include <stddef.h>

using namespace common;

ConditionFilter::~ConditionFilter() {}

DefaultConditionFilter::DefaultConditionFilter()
{
  left_.is_attr     = false;
  left_.attr_length = 0;
  left_.attr_offset = 0;

  right_.is_attr     = false;
  right_.attr_length = 0;
  right_.attr_offset = 0;
}
DefaultConditionFilter::~DefaultConditionFilter() {}

RC DefaultConditionFilter::init(const ConDesc &left, const ConDesc &right, AttrType attr_type, CompOp comp_op)
{
  if (attr_type <= AttrType::UNDEFINED || attr_type >= AttrType::MAXTYPE) {
    LOG_ERROR("Invalid condition with unsupported attribute type: %d", attr_type);
    return RC::INVALID_ARGUMENT;
  }

  if (comp_op < EQUAL_TO || comp_op >= NO_OP) {
    LOG_ERROR("Invalid condition with unsupported compare operation: %d", comp_op);
    return RC::INVALID_ARGUMENT;
  }

  left_      = left;
  right_     = right;
  attr_type_ = attr_type;
  comp_op_   = comp_op;
  return RC::SUCCESS;
}

#include "common/type/char_type.h"
#include "common/type/date_type.h"

RC DefaultConditionFilter::init(Table &table, const ConditionSqlNode &condition)
{
  const TableMeta &table_meta = table.table_meta();
  ConDesc          left;
  ConDesc          right;

  AttrType type_left  = AttrType::UNDEFINED;
  AttrType type_right = AttrType::UNDEFINED;

  if (1 == condition.left_is_attr) {
    left.is_attr                = true;
    const FieldMeta *field_left = table_meta.field(condition.left_attr.attribute_name.c_str());
    if (nullptr == field_left) {
      LOG_WARN("No such field in condition. %s.%s", table.name(), condition.left_attr.attribute_name.c_str());
      return RC::SCHEMA_FIELD_MISSING;
    }
    left.attr_length = field_left->len();
    left.attr_offset = field_left->offset();

    type_left = field_left->type();
  } else {
    left.is_attr = false;
    left.value   = condition.left_value;  // 校验type 或者转换类型
    type_left    = condition.left_value.attr_type();

    left.attr_length = 0;
    left.attr_offset = 0;
  }

  if (1 == condition.right_is_attr) {
    right.is_attr                = true;
    const FieldMeta *field_right = table_meta.field(condition.right_attr.attribute_name.c_str());
    if (nullptr == field_right) {
      LOG_WARN("No such field in condition. %s.%s", table.name(), condition.right_attr.attribute_name.c_str());
      return RC::SCHEMA_FIELD_MISSING;
    }
    right.attr_length = field_right->len();
    right.attr_offset = field_right->offset();
    type_right        = field_right->type();
  } else {
    right.is_attr = false;
    right.value   = condition.right_value;
    type_right    = condition.right_value.attr_type();

    right.attr_length = 0;
    right.attr_offset = 0;
  }

  // 修改：允许DATES类型与CHARS类型之间的比较
  if (type_left != type_right) {
    // 特殊处理：如果一边是DATES类型，另一边是CHARS类型，尝试转换CHARS为DATES
    if ((type_left == AttrType::DATES && type_right == AttrType::CHARS) ||
        (type_left == AttrType::CHARS && type_right == AttrType::DATES)) {
      
      // 确定哪个是DATES类型，哪个是CHARS类型
      // 删除第124-125行的未使用变量定义
      // AttrType date_type = (type_left == AttrType::DATES) ? type_left : type_right;
      // AttrType char_type = (type_left == AttrType::CHARS) ? type_left : type_right;
      
      // 删除第130行的未使用变量定义  
      // ConDesc *date_desc = (type_left == AttrType::DATES) ? &left : &right;
      
      // 添加char_desc变量的定义
      ConDesc *char_desc = (type_left == AttrType::CHARS) ? &left : &right;
      
      // 如果CHARS描述符是属性（列），不能转换
      if (char_desc->is_attr) {
        LOG_WARN("Cannot compare DATE column with CHAR column directly");
        return RC::SCHEMA_FIELD_TYPE_MISMATCH;
      }
      
      // 尝试将CHARS值转换为DATES值
      Value converted_value;
      CharType char_type_instance;
      RC rc = char_type_instance.cast_to(char_desc->value, AttrType::DATES, converted_value);
      if (rc != RC::SUCCESS) {
        LOG_WARN("Failed to convert CHAR value to DATE type");
        return RC::SCHEMA_FIELD_TYPE_MISMATCH;
      }
      
      // 更新转换后的值
      char_desc->value = converted_value;
      char_desc->value.set_type(AttrType::DATES);
      
      // 设置统一的类型为DATES
      type_left = AttrType::DATES;
      type_right = AttrType::DATES;
    } else {
      // 其他类型不匹配的情况仍然返回错误
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
  }

  return init(left, right, type_left, condition.comp);
}

bool DefaultConditionFilter::filter(const Record &rec) const
{
  Value left_value;
  Value right_value;

  if (left_.is_attr) {  // value
    left_value.set_type(attr_type_);
    left_value.set_data(rec.data() + left_.attr_offset, left_.attr_length);
  } else {
    left_value.set_value(left_.value);
  }

  if (right_.is_attr) {
    right_value.set_type(attr_type_);
    right_value.set_data(rec.data() + right_.attr_offset, right_.attr_length);
  } else {
    right_value.set_value(right_.value);
  }

  // 特殊处理：如果类型不匹配但允许转换，尝试转换
  if (left_value.attr_type() != right_value.attr_type()) {
    // 处理DATES与CHARS类型转换
    if ((left_value.attr_type() == AttrType::DATES && right_value.attr_type() == AttrType::CHARS) ||
        (left_value.attr_type() == AttrType::CHARS && right_value.attr_type() == AttrType::DATES)) {
      
      Value *char_value = (left_value.attr_type() == AttrType::CHARS) ? &left_value : &right_value;
      
      // 尝试转换CHARS为DATES
      Value converted_value;
      CharType char_type_instance;
      RC rc = char_type_instance.cast_to(*char_value, AttrType::DATES, converted_value);
      if (rc == RC::SUCCESS) {
        // 使用转换后的值进行比较
        if (char_value == &left_value) {
          left_value = converted_value;
        } else {
          right_value = converted_value;
        }
      } else {
        // 转换失败，尝试直接比较字符串（作为备选方案）
        // 这里可以添加日志记录转换失败
        LOG_DEBUG("Date conversion failed, proceeding with original comparison");
      }
    }
  }

  int cmp_result = left_value.compare(right_value);

  switch (comp_op_) {
    case EQUAL_TO: return 0 == cmp_result;
    case LESS_EQUAL: return cmp_result <= 0;
    case NOT_EQUAL: return cmp_result != 0;
    case LESS_THAN: return cmp_result < 0;
    case GREAT_EQUAL: return cmp_result >= 0;
    case GREAT_THAN: return cmp_result > 0;

    default: break;
  }

  LOG_PANIC("Never should print this.");
  return cmp_result;  // should not go here
}

CompositeConditionFilter::~CompositeConditionFilter()
{
  if (memory_owner_) {
    delete[] filters_;
    filters_ = nullptr;
  }
}

RC CompositeConditionFilter::init(const ConditionFilter *filters[], int filter_num, bool own_memory)
{
  filters_      = filters;
  filter_num_   = filter_num;
  memory_owner_ = own_memory;
  return RC::SUCCESS;
}
RC CompositeConditionFilter::init(const ConditionFilter *filters[], int filter_num)
{
  return init(filters, filter_num, false);
}

RC CompositeConditionFilter::init(Table &table, const ConditionSqlNode *conditions, int condition_num)
{
  if (condition_num == 0) {
    return RC::SUCCESS;
  }
  if (conditions == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  RC                rc                = RC::SUCCESS;
  ConditionFilter **condition_filters = new ConditionFilter *[condition_num];
  for (int i = 0; i < condition_num; i++) {
    DefaultConditionFilter *default_condition_filter = new DefaultConditionFilter();
    rc                                               = default_condition_filter->init(table, conditions[i]);
    if (rc != RC::SUCCESS) {
      delete default_condition_filter;
      for (int j = i - 1; j >= 0; j--) {
        delete condition_filters[j];
        condition_filters[j] = nullptr;
      }
      delete[] condition_filters;
      condition_filters = nullptr;
      return rc;
    }
    condition_filters[i] = default_condition_filter;
  }
  return init((const ConditionFilter **)condition_filters, condition_num, true);
}

bool CompositeConditionFilter::filter(const Record &rec) const
{
  for (int i = 0; i < filter_num_; i++) {
    if (!filters_[i]->filter(rec)) {
      return false;
    }
  }
  return true;
}