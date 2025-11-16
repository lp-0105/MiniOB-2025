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
// Created by Wangyunlai on 2023/6/14.
//

#include "sql/executor/drop_table_executor.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/drop_table_stmt.h"
#include "storage/db/db.h"
// 删除不必要的头文件引用
// #include "storage/default/default_handler.h"

RC DropTableExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt *stmt = sql_event->stmt();
  if (stmt->type() != StmtType::DROP_TABLE) {
    LOG_ERROR("cannot run drop table executor on stmt type: %d", static_cast<int>(stmt->type()));
    return RC::INTERNAL;
  }

  DropTableStmt *drop_table_stmt = static_cast<DropTableStmt *>(stmt);
  SessionEvent *session_event = sql_event->session_event();
  Session *session = session_event->session();
  Db *db = session->get_current_db();
  
  if (nullptr == db) {
    LOG_ERROR("cannot find current db");
    return RC::SCHEMA_DB_NOT_OPENED;
  }

  const char *table_name = drop_table_stmt->table_name().c_str();
  
  // 修改为直接调用Db类的drop_table方法
  RC rc = db->drop_table(table_name);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to drop table. db=%s, table_name=%s, rc=%s", 
             db->name(), table_name, strrc(rc));
    return rc;
  }

  LOG_INFO("drop table success. db=%s, table=%s", db->name(), table_name);
  return RC::SUCCESS;
}