// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_FLOYD_PEER_THREAD_H_
#define FLOYD_SRC_FLOYD_PEER_THREAD_H_

#include <string>
#include <map>
#include <boost/asio/ts/io_context.hpp>

#include "floyd/src/floyd_context.h"

namespace floyd {

class RaftMeta;
class FloydPrimary;
class RaftLog;
class ClientPool;
class FloydApply;
class Peer;
typedef std::map<std::string, std::unique_ptr<Peer>> PeersSet;

class Peer final {
 public:
  Peer(boost::asio::io_context& ctx_, std::string server, PeersSet& peers, FloydContext& context, FloydPrimary& primary, RaftMeta& raft_meta,
      RaftLog& raft_log, ClientPool &pool, FloydApply& apply, const Options& options, Logger* info_log);
  ~Peer() = default;

  // Apend Entries
  // call by other thread, put job to peer_thread's bg_thread_
  void AddAppendEntriesTask();
  void AddRequestVoteTask();

  /*
   * the two main RPC call in raft consensus protocol is here
   * AppendEntriesRPC
   * RequestVoteRPC
   * the response to these two RPC at floyd_impl.h
   */
  void AppendEntriesRPC();
  // Request Vote
  void RequestVoteRPC();

  uint64_t GetMatchIndex();

  void set_next_index(const uint64_t next_index) {
    next_index_ = next_index;
  }
  uint64_t next_index() {
    return next_index_;
  }

  void set_match_index(const uint64_t match_index) {
    match_index_ = match_index;
  }
  uint64_t match_index() {
    return match_index_;
  }

  std::string peer_addr() const {
    return peer_addr_;
  }

 private:
  bool CheckAndVote(uint64_t vote_term);
  uint64_t QuorumMatchIndex();
  void AdvanceLeaderCommitIndex();
  void UpdatePeerInfo();

  boost::asio::io_context& ctx;
  std::string peer_addr_;
  PeersSet& peers_;
  FloydContext& context_;
  FloydPrimary& primary_;
  RaftMeta& raft_meta_;
  RaftLog& raft_log_;
  ClientPool& pool_;
  FloydApply& apply_;
  Options options_;
  Logger* const info_log_;


  std::atomic<uint64_t> next_index_;
  std::atomic<uint64_t> match_index_;
  uint64_t peer_last_op_time;

  // No copying allowed
  Peer(const Peer&);
  void operator=(const Peer&);
};

}  // namespace floyd
#endif   // FLOYD_SRC_FLOYD_PEER_THREAD_H_
