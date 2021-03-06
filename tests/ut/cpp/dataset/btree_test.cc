/**
 * Copyright 2019 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sstream>
#include "dataset/util/btree.h"
#include "dataset/util/auto_index.h"
#include "dataset/util/system_pool.h"
#include "dataset/util/task_manager.h"
#include "common/common.h"
#include "gtest/gtest.h"
#include "dataset/util/de_error.h"
#include "utils/log_adapter.h"

using namespace mindspore::dataset;
using mindspore::MsLogLevel::INFO;
using mindspore::ExceptionType::NoExceptionType;
using mindspore::LogStream;

// For testing purposes, we will make the branching factor very low.
struct mytraits {
    using slot_type = uint16_t;

    static const slot_type kLeafSlots = 6;

    static const slot_type kInnerSlots = 3;

    static const bool kAppendMode = false;

};


class MindDataTestBPlusTree : public UT::Common {
 public:
    MindDataTestBPlusTree() = default;
};

// Test serial insert.
TEST_F(MindDataTestBPlusTree, Test1) {
  Allocator<std::string> alloc(std::make_shared<SystemPool>());
  BPlusTree<uint64_t, std::string, std::less<uint64_t>, mytraits> btree(alloc);
  Status rc;
  for (int i = 0; i < 100; i++) {
    uint64_t key = 2 * i;
    std::ostringstream oss;
    oss << "Hello World. I am " << key;
    rc = btree.DoInsert(key, oss.str());
    EXPECT_TRUE(rc.IsOk());
  }
  for (int i = 0; i < 100; i++) {
    uint64_t key = 2 * i + 1;
    std::ostringstream oss;
    oss << "Hello World. I am " << key;
    rc = btree.DoInsert(key, oss.str());
    EXPECT_TRUE(rc.IsOk());
  }
  EXPECT_EQ(btree.size(), 200);

  // Test iterator
  {
    int cnt = 0;
    auto it = btree.begin();
    uint64_t prev = it.key();
    ++it;
    ++cnt;
    while (it != btree.end()) {
      uint64_t cur = it.key();
      std::string val = "Hello World. I am " + std::to_string(cur);
      EXPECT_TRUE(prev < cur);
      EXPECT_EQ(it.value(), val);
      prev = cur;
      ++it;
      ++cnt;
    }
    EXPECT_EQ(cnt, 200);
    // Now go backward
    for (int i = 0; i < 10; i++) {
      --it;
      EXPECT_EQ(199 - i, it.key());
    }
  }

  // Test nearch
  {
    MS_LOG(INFO) << "Locate key " << 100 << " Expect found.";
    auto it = btree.Search(100);
    EXPECT_FALSE(it == btree.cend());
    EXPECT_EQ(it.key(), 100);
    EXPECT_EQ(it.value(), "Hello World. I am 100");
    MS_LOG(INFO) << "Locate key " << 300 << " Expect not found.";
    it = btree.Search(300);
    EXPECT_TRUE(it == btree.cend());
  }

  // Test duplicate key
  {
    rc = btree.DoInsert(100, "Expect error");
    EXPECT_EQ(rc, Status(StatusCode::kDuplicateKey));
  }
}

// Test concurrent insert.
TEST_F(MindDataTestBPlusTree, Test2) {
  Allocator<std::string> alloc(std::make_shared<SystemPool>());
  BPlusTree<uint64_t, std::string, std::less<uint64_t>, mytraits> btree(alloc);
  TaskGroup vg;
  auto f = [&](int k) -> Status {
    TaskManager::FindMe()->Post();
      for (int i = 0; i < 100; i++) {
        uint64_t key = k * 100 + i;
        std::ostringstream oss;
        oss << "Hello World. I am " << key;
        Status rc = btree.DoInsert(key, oss.str());
        EXPECT_TRUE(rc.IsOk());
      }
      return Status::OK();
  };
  // Spawn two threads. One insert the odd numbers and the other insert the even numbers just like Test1
  for (int k = 0; k < 100; k++) {
    vg.CreateAsyncTask("Concurrent Insert", std::bind(f, k));
  }
  vg.join_all();
  EXPECT_EQ(btree.size(), 10000);

  // Test iterator
  {
    int cnt = 0;
    auto it = btree.begin();
    uint64_t prev = it.key();
    ++it;
    ++cnt;
    while (it != btree.end()) {
      uint64_t cur = it.key();
      std::string val = "Hello World. I am " + std::to_string(cur);
      EXPECT_TRUE(prev < cur);
      EXPECT_EQ(it.value(), val);
      prev = cur;
      ++it;
      ++cnt;
    }
    EXPECT_EQ(cnt, 10000);
  }

  // Test search
  {
    MS_LOG(INFO) << "Locating key from 0 to 9999. Expect found.";
    for (int i = 0; i < 10000; i++) {
      auto it = btree.Search(i);
      bool eoS = (it == btree.cend());
      EXPECT_FALSE(eoS);
      if (!eoS) {
        EXPECT_EQ(it.key(), i);
        std::string val = "Hello World. I am " + std::to_string(i);
        EXPECT_EQ(it.value(), val);
      }
    }
    MS_LOG(INFO) << "Locate key " << 10000 << ". Expect not found";
    auto it = btree.Search(10000);
    EXPECT_TRUE(it == btree.cend());
  }

  // Test to retrieve key at certain position.
  {
    for (int i = 0; i < 10000; i++) {
      int k = btree.KeyAtPos(i);
      EXPECT_EQ(k, i);
    }
  }
}

TEST_F(MindDataTestBPlusTree, Test3) {
  Allocator<std::string> alloc(std::make_shared<SystemPool>());
  AutoIndexObj<std::string> ai(alloc);
  Status rc;
  rc = ai.insert("Hello World");
  EXPECT_TRUE(rc.IsOk());
  ai.insert({"a", "b", "c"});
  EXPECT_TRUE(rc.IsOk());
  uint64_t min = ai.min_key();
  uint64_t max = ai.max_key();
  EXPECT_EQ(min, 1);
  EXPECT_EQ(max, 4);
  auto it = ai.Search(3);
  EXPECT_EQ(it.value(), "b");
  MS_LOG(INFO) << "Dump all the values using [] operator.";
  for (uint64_t i = min; i <= max; i++) {
    MS_LOG(DEBUG) << ai[i] << std::endl;
  }
}
