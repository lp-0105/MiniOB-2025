/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/tokenizer/jieba_tokenizer.h"
#include "common/utils/private_accessor.h"

JiebaTokenizer::JiebaTokenizer() 
  : jieba_(nullptr) {
  // 初始化cppjieba，使用默认词典路径
  // 使用CMAKE_SOURCE_DIR获取项目根目录，确保在任何环境下都能正确找到词典
  std::string base_path = std::string(CMAKE_SOURCE_DIR) + "/deps/3rd/cppjieba/dict/";
  std::string DICT_PATH = base_path + "jieba.dict.utf8";
  std::string HMM_PATH = base_path + "hmm_model.utf8";
  std::string USER_DICT_PATH = base_path + "user.dict.utf8";
  std::string IDF_PATH = base_path + "idf.utf8";
  std::string STOP_WORD_PATH = base_path + "stop_words.utf8";
  
  jieba_ = new cppjieba::Jieba(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH);
}

JiebaTokenizer::~JiebaTokenizer() {
  if (jieba_ != nullptr) {
    delete jieba_;
    jieba_ = nullptr;
  }
}

RC JiebaTokenizer::cut(std::string &text, std::vector<std::string> &tokens) {
  if (jieba_ == nullptr) {
    return RC::INVALID_ARGUMENT;
  }
  
  // 使用项目内部的cppjieba进行分词
  jieba_->Cut(text, tokens, true);
  
  return RC::SUCCESS;
}