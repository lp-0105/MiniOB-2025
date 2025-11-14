#include "common/type/date_type.h"
#include "common/time/datetime.h"
#include "common/log/log.h"
#include <cstdio>

int DateType::compare(const Value &left, const Value &right) const {
  int l = left.get_int();
  int r = right.get_int();
  return (l < r) ? -1 : (l > r) ? 1 : 0;
}

RC DateType::set_value_from_str(Value &val, const std::string &data) const {
  int y, m, d;
  if (sscanf(data.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  if (y < 1970 || y > 2038 || m < 1 || m > 12 || d < 1 || d > 31) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  int julian_day = common::DateTime::julian_date(y, m, d);
  val.set_int(julian_day - common::DateTime::JULIAN_19700101);
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &val, std::string &result) const {
  int days_since_epoch = val.get_int();
  int julian_day = days_since_epoch + common::DateTime::JULIAN_19700101;
  int year, month, day;
  common::DateTime::get_ymd(julian_day, year, month, day);
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
  result = buf;
  return RC::SUCCESS;
}