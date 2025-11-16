/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/table/lsm_table_engine.h"
#include "storage/record/heap_record_scanner.h"
#include "common/log/log.h"
#include "storage/index/bplus_tree_index.h"
#include "storage/common/meta_util.h"
#include "storage/db/db.h"
#include "storage/record/lsm_record_scanner.h"
#include "storage/common/codec.h"
#include "storage/trx/lsm_mvcc_trx.h"

RC LsmTableEngine::insert_record(Record &record)
{
  RC rc = RC::SUCCESS;
  // TODO: set auto increment id, and keep durability.
  // TODO: support set primary key as a part of lsm_key.
  bytes lsm_key;
  Codec::encode(table_->table_id(), inc_id_.fetch_add(1), lsm_key);
  rc = lsm_->put(string_view((char *)lsm_key.data(), lsm_key.size()), string_view(record.data(), record.len()));
  return rc;
}

RC LsmTableEngine::get_record_scanner(RecordScanner *&scanner, Trx *trx, ReadWriteMode mode)
{
  scanner = new LsmRecordScanner(table_, db_->lsm(), trx);
  RC rc = scanner->open_scan();
  if (rc != RC::SUCCESS) {
    LOG_ERROR("failed to open scanner. rc=%s", strrc(rc));
  }
  return rc;
}

RC LsmTableEngine::open()
{
  return RC::UNIMPLEMENTED;
}

// 添加update_record方法实现
RC LsmTableEngine::update_record(const Record &record, const char *attribute_name, const Value &value)
{
  // 对于LSM表引擎，更新操作实际上是通过删除旧记录并插入新记录来实现的
  // 因为LSM树是追加写的，不支持原地更新
  
  RC rc = RC::SUCCESS;

  // 1. 查找要更新的字段
  const FieldMeta *field_meta = table_meta_->field(attribute_name);
  if (nullptr == field_meta) {
    LOG_WARN("field not found. table=%s, field=%s", table_meta_->name(), attribute_name);
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  // 2. 检查字段类型是否匹配
  if (field_meta->type() != value.attr_type()) {
    LOG_WARN("field type mismatch. table=%s, field=%s, field_type=%d, value_type=%d",
             table_meta_->name(), attribute_name, field_meta->type(), value.attr_type());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  // 3. 创建新的记录副本
  Record new_record;
  rc = new_record.copy_data(record.data(), record.len());
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to copy record data. rc=%s", strrc(rc));
    return rc;
  }
  new_record.set_rid(record.rid());

  // 4. 更新字段值
  char *record_data = const_cast<char *>(new_record.data());
  size_t copy_len = field_meta->len();
  const size_t data_len = value.length();
  if (field_meta->type() == AttrType::CHARS) {
    if (copy_len > data_len) {
      copy_len = data_len + 1;
    }
  }
  memcpy(record_data + field_meta->offset(), value.data(), copy_len);

  // 5. 对于LSM表，更新操作实际上是删除旧记录并插入新记录
  // 注意：这里简化处理，实际LSM表可能需要更复杂的逻辑
  
  // 由于LSM表不支持直接更新，我们返回UNSUPPORTED
  // 在实际实现中，可能需要通过事务机制来处理更新
  LOG_WARN("LSM table engine does not support direct record update. Use transaction-based update instead.");
  return RC::UNSUPPORTED;
}