/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/lang/comparator.h"
#include "common/log/log.h"
#include "common/type/char_type.h"
#include "common/value.h"

int CharType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::CHARS, "left type is not CHARS");
  
  // 如果右边也是CHARS类型，直接比较字符串
  if (right.attr_type() == AttrType::CHARS) {
    return common::compare_string(
        (void *)left.value_.pointer_value_, left.length_, (void *)right.value_.pointer_value_, right.length_);
  }
  
  // 如果右边是数值类型，尝试将左边的字符串转换为数值进行比较
  if (right.attr_type() == AttrType::INTS) {
    try {
      int left_val = stoi(string(left.value_.pointer_value_));
      return common::compare_int((void *)&left_val, (void *)&right.value_.int_value_);
    } catch (exception const &ex) {
      LOG_TRACE("failed to convert string to int. s=%s, ex=%s", left.value_.pointer_value_, ex.what());
      // 如果转换失败，认为字符串小于任何数值
      return -1;
    }
  }
  
  if (right.attr_type() == AttrType::FLOATS) {
    try {
      float left_val = stof(string(left.value_.pointer_value_));
      return common::compare_float((void *)&left_val, (void *)&right.value_.float_value_);
    } catch (exception const &ex) {
      LOG_TRACE("failed to convert string to float. s=%s, ex=%s", left.value_.pointer_value_, ex.what());
      // 如果转换失败，认为字符串小于任何数值
      return -1;
    }
  }
  
  // 其他类型不支持比较
  return INT32_MAX;
}

RC CharType::set_value_from_str(Value &val, const string &data) const
{
  val.set_string(data.c_str());
  return RC::SUCCESS;
}

RC CharType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::INTS: {
      return DataType::type_instance(AttrType::INTS)->set_value_from_str(result, val.get_string());
    }
    case AttrType::FLOATS: {
      return DataType::type_instance(AttrType::FLOATS)->set_value_from_str(result, val.get_string());
    }
    case AttrType::BOOLEANS: {
      return DataType::type_instance(AttrType::BOOLEANS)->set_value_from_str(result, val.get_string());
    }
    case AttrType::DATES: {
      // parse date string into result. Use DateType's set_value_from_str, then ensure result type is DATES
      RC rc = DataType::type_instance(AttrType::DATES)->set_value_from_str(result, val.get_string());
      if (rc != RC::SUCCESS) return rc;
      result.set_type(AttrType::DATES);
      return RC::SUCCESS;
    }
    default: {
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
  }
  return RC::SUCCESS;
}

int CharType::cast_cost(AttrType type)
{
  if (type == AttrType::CHARS) {
    return 0;
  }
  return INT32_MAX;
}

RC CharType::to_string(const Value &val, string &result) const
{
  stringstream ss;
  ss << val.value_.pointer_value_;
  result = ss.str();
  return RC::SUCCESS;
}