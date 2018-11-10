// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_FLOYD_APPLY_H_
#define FLOYD_SRC_FLOYD_APPLY_H_

#include <boost/asio/ts/io_context.hpp>

#include "floyd/src/floyd_context.h"

namespace floyd {

class RaftMeta;
class RaftLog;
class Logger;
class FloydImpl;
class Entry;

class FloydApply final {
 public:
  FloydApply(boost::asio::io_context& ctx_, FloydContext& context, rocksdb::DB* db, RaftMeta& raft_meta,
      RaftLog& raft_log, FloydImpl* impl_, Logger* info_log); 
  ~FloydApply() = default;
  void ScheduleApply();

 private:
  boost::asio::io_context& ctx;
  FloydContext& context_;
  rocksdb::DB* const db_;
  /*
   * we will store the increasing id in raft_meta_
   */
  RaftMeta& raft_meta_;
  RaftLog& raft_log_;
  FloydImpl* const impl_;
  Logger* const info_log_;
  void ApplyStateMachine();
  rocksdb::Status Apply(const Entry& log_entry);
  rocksdb::Status MembershipChange(const std::string& ip_port, bool add);


  FloydApply(const FloydApply&);
  void operator=(const FloydApply&);
};

}  // namespace floyd

#endif  // FLOYD_SRC_FLOYD_APPLY_H_
