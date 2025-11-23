/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/operator/physical_operator.h"
#include "sql/parser/parse.h"
#include "sql/expr/expression.h"
#include "storage/trx/trx.h"
#include <unordered_map>
#include <string>

/**
 * @brief Hash Join 算子
 * @ingroup PhysicalOperator
 */
class HashJoinPhysicalOperator : public PhysicalOperator
{
public:
  HashJoinPhysicalOperator() = default;
  virtual ~HashJoinPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::HASH_JOIN; }

  OpType get_op_type() const override { return OpType::INNERHASHJOIN; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;
  Tuple *current_tuple() override;

  void set_predicates(vector<unique_ptr<Expression>> &&predicates);

private:
  bool evaluate_join_conditions(JoinedTuple &joined_tuple);  //! 评估JOIN条件是否满足

private:
  Trx *trx_ = nullptr;
  
  // 左表和右表的真实对象是在PhysicalOperator::children_中
  PhysicalOperator *left_        = nullptr;
  PhysicalOperator *right_       = nullptr;
  Tuple            *left_tuple_  = nullptr;
  Tuple            *right_tuple_ = nullptr;
  JoinedTuple       joined_tuple_;         //! 当前关联的左右两个tuple
  
  vector<unique_ptr<Expression>> predicates_;  //! JOIN条件列表
  
  // Hash Join相关成员
  bool hash_built_ = false;  //! 哈希表是否已构建
  unordered_map<string, vector<Tuple*>> hash_table_;  //! 哈希表
  size_t current_tuple_index_ = 0; //! 当前处理的元组索引
  vector<Tuple*> current_right_tuples_; //! 当前右表行匹配的左表元组列表
};