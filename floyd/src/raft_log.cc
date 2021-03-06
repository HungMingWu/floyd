// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/raft_log.h"

#include <cereal/types/memory.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <vector>
#include <string>

#include "rocksdb/db.h"
#include "rocksdb/iterator.h"

#include "floyd/src/logger.h"
#include "floyd/include/floyd_options.h"
#include "floyd/src/floyd_ds.h"

namespace floyd {
std::string UintToBitStr(const uint64_t num) {
  std::ostringstream os;
  cereal::PortableBinaryOutputArchive archive(os);
  archive(num);
  return std::move(os.str());
}

uint64_t BitStrToUint(const std::string &str) {
  uint64_t num;
  std::istringstream is(str);
  cereal::PortableBinaryInputArchive archive(is);
  archive(num);
  return num;
}

RaftLog::RaftLog(rocksdb::DB *db, Logger *info_log) :
  db_(db),
  info_log_(info_log),
  last_log_index_(0) {
  rocksdb::Iterator *it = db_->NewIterator(rocksdb::ReadOptions());
  it->SeekToLast();
  if (it->Valid()) {
    it->Prev();
    it->Prev();
    it->Prev();
    it->Prev();
    it->Prev();
    if (it->Valid()) {
      last_log_index_ = BitStrToUint(it->key().ToString());
    }
  }
  delete it;
}

RaftLog::~RaftLog() {
}

uint64_t RaftLog::Append(const std::vector<Entry> &entries) {
  std::lock_guard l(lli_mutex_);
  rocksdb::WriteBatch wb;
  LOGV(DEBUG_LEVEL, info_log_, "RaftLog::Append: entries.size %lld", entries.size());
  // try to commit entries in one batch
  for (const auto &entry : entries) {
    std::ostringstream os;
    cereal::BinaryOutputArchive archive(os);
    archive(entry);
    last_log_index_++;
    wb.Put(UintToBitStr(last_log_index_), os.str());
  }
  rocksdb::Status s;
  s = db_->Write(rocksdb::WriteOptions(), &wb);
  if (!s.ok()) {
    last_log_index_ -= entries.size();
    LOGV(ERROR_LEVEL, info_log_, "RaftLog::Append append entries failed, entries size %u, last_log_index_ is %lu",
        entries.size(), last_log_index_);
  }
  return last_log_index_;
}

uint64_t RaftLog::GetLastLogIndex() {
  return last_log_index_;
}

std::optional<Entry> RaftLog::GetEntry(const uint64_t index) {
  std::lock_guard l(lli_mutex_);
  std::string buf = UintToBitStr(index);
  std::string res;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), buf, &res);
  if (s.IsNotFound()) {
    LOGV(ERROR_LEVEL, info_log_, "RaftLog::GetEntry: GetEntry not found, index is %lld", index);
    return std::nullopt;
  }
  Entry entry;
  std::istringstream is(res);
  cereal::BinaryInputArchive archive(is);
  archive(entry);
  return entry;
}

std::pair<uint64_t, uint64_t> RaftLog::GetLastLogTermAndIndex() {
  std::lock_guard l(lli_mutex_);
  if (last_log_index_ == 0) {
    return {0, 0};
  }
  std::string buf;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), UintToBitStr(last_log_index_), &buf);
  if (!s.ok() || s.IsNotFound()) {
    return {0, 0};
  }
  Entry entry;
  std::istringstream is(buf);
  cereal::BinaryInputArchive archive(is);
  archive(entry);
  return {entry.term, last_log_index_};
}

/*
 * truncate suffix from index
 */
int RaftLog::TruncateSuffix(uint64_t index) {
  // we need to delete the unnecessary entry, since we don't store
  // last_log_index in rocksdb
  rocksdb::WriteBatch batch;
  for (; last_log_index_ >= index; last_log_index_--) {
    batch.Delete(UintToBitStr(last_log_index_));
  }
  if (batch.Count() > 0) {
    rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) {
      LOGV(ERROR_LEVEL, info_log_, "RaftLog::TruncateSuffix Error last_log_index %lu "
          "truncate from %lu", last_log_index_, index);
      return -1;
    }
  }
  return 0;
}

}  // namespace floyd
