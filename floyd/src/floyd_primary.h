// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_FLOYD_PRIMARY_THREAD_H_
#define FLOYD_SRC_FLOYD_PRIMARY_THREAD_H_

#include <string>
#include <map>
#include <vector>

#include <boost/asio/ts/io_context.hpp>
#include <boost/asio/ts/timer.hpp>

#include "floyd/src/floyd_context.h"
#include "floyd/src/floyd_peer.h"

namespace floyd {

class FloydPrimary;

class FloydContext;
class FloydApply;
class RaftMeta;
class Peer;
class Options;

enum TaskType {
  kHeartBeat = 0,
  kCheckLeader = 1,
  kNewCommand = 2
};

class FloydPrimary final {
 public:
  FloydPrimary(boost::asio::io_context& ctx_, FloydContext& context, PeersSet& peers, RaftMeta& raft_meta,
      const Options& options, Logger* info_log);
  ~FloydPrimary() = default;

  void AddTask(TaskType type, bool is_delay = true);
 private:
  boost::asio::io_context& ctx;
  boost::asio::system_timer timer;
  FloydContext& context_;
  PeersSet& peers_;
  RaftMeta& raft_meta_;
  Options options_;
  Logger* const info_log_;

  std::atomic<uint64_t> reset_elect_leader_time_;
  std::atomic<uint64_t> reset_leader_heartbeat_time_;

  // The Launch* work is done by floyd_peer_thread
  // Cron task
  void LaunchHeartBeat();
  void LaunchCheckLeader();
  void LaunchNewCommand();

  void NoticePeerTask(TaskType type);

  // No copying allowed
  FloydPrimary(const FloydPrimary&);
  void operator=(const FloydPrimary&);
};

}  // namespace floyd
#endif  // FLOYD_SRC_FLOYD_PRIMARY_THREAD_H_
