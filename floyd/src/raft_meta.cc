// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/raft_meta.h"

#include <stdlib.h>

#include "rocksdb/status.h"

#include "floyd/src/logger.h"

namespace floyd {

static const std::string kCurrentTerm = "CURRENTTERM";
static const std::string kVoteForIp = "VOTEFORIP";
static const std::string kVoteForPort = "VOTEFORPORT";
static const std::string kCommitIndex = "COMMITINDEX";
static const std::string kLastApplied = "APPLYINDEX";
/*
 * fencing token is not part of raft, fencing token is used for implementing distributed lock
 */
static const std::string kFencingToken = "FENCINGTOKEN";

RaftMeta::RaftMeta(rocksdb::DB* db)
  : db_(db)
{
}

void RaftMeta::Init() {
  if (GetCurrentTerm() == 0) {
    SetCurrentTerm(0);
  }
  if (GetVotedForIp() == "") {
    SetVotedForIp("");
  }
  if (GetVotedForPort() == 0) {
    SetVotedForPort(0);
  }
  if (GetCommitIndex() == 0) {
    SetCommitIndex(0);
  }
  if (GetLastApplied() == 0) {
    SetLastApplied(0);
  }
}

uint64_t RaftMeta::GetCurrentTerm() const {
  std::string buf;
  uint64_t ans;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), kCurrentTerm, &buf);
  if (s.IsNotFound()) {
    return 0;
  }
  memcpy(&ans, buf.data(), sizeof(uint64_t));
  return ans;
}

void RaftMeta::SetCurrentTerm(const uint64_t current_term) {
  char buf[8];
  memcpy(buf, &current_term, sizeof(uint64_t));
  db_->Put(rocksdb::WriteOptions(), kCurrentTerm, std::string(buf, 8));
  return;
}

std::string RaftMeta::GetVotedForIp() const {
  std::string buf;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), kVoteForIp, &buf);
  if (s.IsNotFound()) {
    return std::string("");
  }
  return buf;
}

void RaftMeta::SetVotedForIp(const std::string ip) {
  db_->Put(rocksdb::WriteOptions(), kVoteForIp, ip);
  return;
}

int RaftMeta::GetVotedForPort() const {
  std::string buf;
  int ans;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), kVoteForPort, &buf);
  if (s.IsNotFound()) {
    return 0;
  }
  memcpy(&ans, buf.data(), sizeof(int));
  return ans;
}

void RaftMeta::SetVotedForPort(const int port) {
  char buf[4];
  memcpy(buf, &port, sizeof(int));
  db_->Put(rocksdb::WriteOptions(), kVoteForPort, std::string(buf, sizeof(int)));
  return;
}

uint64_t RaftMeta::GetCommitIndex() const {
  std::string buf;
  uint64_t ans;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), kCommitIndex, &buf);
  if (s.IsNotFound()) {
    return 0;
  }
  memcpy(&ans, buf.data(), sizeof(uint64_t));
  return ans;
}

void RaftMeta::SetCommitIndex(uint64_t commit_index) {
  char buf[8];
  memcpy(buf, &commit_index, sizeof(uint64_t));
  db_->Put(rocksdb::WriteOptions(), kCommitIndex, std::string(buf, 8));
}

uint64_t RaftMeta::GetLastApplied() const {
  std::string buf;
  uint64_t ans;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), kLastApplied, &buf);
  if (s.IsNotFound()) {
    return 0;
  }
  memcpy(&ans, buf.data(), sizeof(uint64_t));
  return ans;
}

void RaftMeta::SetLastApplied(uint64_t last_applied) {
  char buf[8];
  memcpy(buf, &last_applied, sizeof(uint64_t));
  db_->Put(rocksdb::WriteOptions(), kLastApplied, std::string(buf, 8));
}

uint64_t RaftMeta::GetNewFencingToken() {
  std::string buf;
  uint64_t ans;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), kFencingToken, &buf);
  if (s.IsNotFound()) {
    ans = 0;
  }
  memcpy(&ans, buf.data(), sizeof(uint64_t));
  ans++;
  char wbuf[8];
  memcpy(wbuf, &ans, sizeof(uint64_t));
  db_->Put(rocksdb::WriteOptions(), kFencingToken, std::string(wbuf, 8));
  return ans;
}

}  // namespace floyd
