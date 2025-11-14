#pragma once

#include "common/type/data_type.h"
#include <string>

class DateType : public DataType {
public:
  DateType() : DataType(AttrType::DATES) {}
  virtual ~DateType() = default;

  int compare(const Value &left, const Value &right) const override;
  RC set_value_from_str(Value &val, const std::string &data) const override;
  RC to_string(const Value &val, std::string &result) const override;
};