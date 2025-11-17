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
// Created by WangYunlai on 2022/12/30.
//

#include "sql/operator/join_physical_operator.h"

JoinPhysicalOperator::JoinPhysicalOperator() {}

RC JoinPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 2) {
    LOG_WARN("nlj operator should have 2 children");
    return RC::INTERNAL;
  }

  RC rc         = RC::SUCCESS;
  left_         = children_[0].get();
  right_        = children_[1].get();
  right_closed_ = true;
  round_done_   = true;

  rc   = left_->open(trx);
  trx_ = trx;
  return rc;
}

RC JoinPhysicalOperator::next()
{
  RC   rc             = RC::SUCCESS;
  while (RC::SUCCESS == rc) {
    bool left_need_step = (left_tuple_ == nullptr);
    if (round_done_) {
      left_need_step = true;
    }

    if (left_need_step) {
      rc = left_next();
      if (rc != RC::SUCCESS) {
        return rc;
      }
    }

    rc = right_next();
    if (rc != RC::SUCCESS) {
      if (rc == RC::RECORD_EOF) {
        rc = RC::SUCCESS;
        round_done_ = true;
        continue;
      } else {
        return rc;
      }
    }
    
    // 检查JOIN条件是否满足
    if (evaluate_join_conditions()) {
      return RC::SUCCESS;  // 找到满足条件的记录
    }
  }
  return rc;
}

RC JoinPhysicalOperator::close()
{
  RC rc = left_->close();
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to close left oper. rc=%s", strrc(rc));
  }

  if (!right_closed_) {
    rc = right_->close();
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to close right oper. rc=%s", strrc(rc));
    } else {
      right_closed_ = true;
    }
  }
  return rc;
}

Tuple *JoinPhysicalOperator::current_tuple() { return &joined_tuple_; }

RC JoinPhysicalOperator::left_next()
{
  RC rc = RC::SUCCESS;
  rc    = left_->next();
  if (rc != RC::SUCCESS) {
    return rc;
  }

  left_tuple_ = left_->current_tuple();
  joined_tuple_.set_left(left_tuple_);
  return rc;
}

RC JoinPhysicalOperator::right_next()
{
  RC rc = RC::SUCCESS;
  if (round_done_) {
    if (!right_closed_) {
      rc = right_->close();

      right_closed_ = true;
      if (rc != RC::SUCCESS) {
        return rc;
      }
    }

    rc = right_->open(trx_);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    right_closed_ = false;

    round_done_ = false;
  }

  rc = right_->next();
  if (rc != RC::SUCCESS) {
    if (rc == RC::RECORD_EOF) {
      round_done_ = true;
    }
    return rc;
  }

  right_tuple_ = right_->current_tuple();
  joined_tuple_.set_right(right_tuple_);
  return rc;
}

void JoinPhysicalOperator::set_predicates(vector<unique_ptr<Expression>> &&predicates)
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

bool JoinPhysicalOperator::evaluate_join_conditions()
{
  // 如果没有JOIN条件，默认返回true（笛卡尔积）
  if (predicates_.empty()) {
    return true;
  }
  
  // 创建一个复合元组，包含左右两个元组
  // 这样表达式才能正确引用两个表的字段
  JoinedTuple composite_tuple;
  composite_tuple.set_left(left_tuple_);
  composite_tuple.set_right(right_tuple_);
  
  for (auto &predicate : predicates_) {
    if (predicate == nullptr) {
      continue;
    }
    
    Value value;
    RC rc = predicate->get_value(composite_tuple, value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to evaluate join condition. rc=%s", strrc(rc));
      return false;
    }
    
    if (value.get_boolean() == false) {
      return false;  // 任何一个条件不满足，就返回false
    }
  }
  
  return true;  // 所有条件都满足
}