// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_RAFT_META_H_
#define FLOYD_SRC_RAFT_META_H_

#include <pthread.h>
#include <string>

#include "rocksdb/db.h"

#include "floyd/include/floyd_options.h"

namespace floyd {

class Logger;

/*
 * we use RaftMeta to avoid passing the floyd_impl's this point to other thread
 */
/*
 * main data stored in raftmeta
 * static const std::string kcurrentterm = "currentterm";
 * static const std::string kvoteforip = "voteforip";
 * static const std::string kvoteforport = "voteforport";
 * static const std::string kcommitindex = "commitindex";
 * static const std::string klastapplied = "applyindex";
 * fencing token is not part of raft, fencing token is used for implementing distributed lock
 * static const std::string kFencingToken = "FENCINGTOKEN";
 */
class RaftMeta final {
 public:
  RaftMeta(rocksdb::DB *db);
  ~RaftMeta() = default;

  void Init();

  // return persistent state from zeppelin
  uint64_t GetCurrentTerm() const;
  void SetCurrentTerm(const uint64_t current_term);

  std::string GetVotedForIp() const;
  int GetVotedForPort() const;
  void SetVotedForIp(const std::string ip);
  void SetVotedForPort(const int port);

  uint64_t GetCommitIndex() const;
  void SetCommitIndex(const uint64_t commit_index);

  uint64_t GetLastApplied() const;
  void SetLastApplied(uint64_t last_applied);

  uint64_t GetNewFencingToken();
 private:
  // db used to data that need to be persistent
  rocksdb::DB * const db_;

};

} // namespace floyd
#endif  // FLOYD_SRC_RAFT_META_H_
