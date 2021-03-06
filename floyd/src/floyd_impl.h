// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_FLOYD_IMPL_H_
#define FLOYD_SRC_FLOYD_IMPL_H_

#include <string>
#include <set>
#include <utility>
#include <map>
#include <system_error>
#include <boost/asio/ts/io_context.hpp>

#include "floyd/include/floyd.h"
#include "floyd/include/floyd_options.h"
#include "floyd/src/raft_log.h"
#include "floyd/src/floyd_ds.h"
#include "floyd/src/expected.hpp"

namespace floyd {

class Log;
class ClientPool;
class RaftMeta;
class Peer;
class FloydPrimary;
class FloydApply;
class FloydWorker;
class FloydWorkerConn;
class FloydContext;
class Logger;
class CmdResponse;

typedef std::map<std::string, std::unique_ptr<Peer>> PeersSet;

static const std::string kMemberConfigKey = "#MEMBERCONFIG";

class FloydImpl : public Floyd {
 public:
  explicit FloydImpl(const Options& options);
  virtual ~FloydImpl();

  std::error_code Init();

  std::error_code Write(const std::string& key, const std::string& value) override;
  std::error_code Delete(const std::string& key) override;
  std::error_code Read(const std::string& key, std::string* value) override;
  std::error_code DirtyRead(const std::string& key, std::string* value) override;
  // ttl is millisecond
  std::error_code TryLock(const std::string& name, const std::string& holder, uint64_t ttl) override;
  std::error_code UnLock(const std::string& name, const std::string& holder) override;

  // membership change interface
  std::error_code AddServer(const std::string& new_server) override;
  std::error_code RemoveServer(const std::string& out_server) override;
  std::error_code GetAllServers(std::set<std::string>* nodes) override;

  // return true if leader has been elected
  virtual bool GetLeader(std::string* ip_port) override;
  virtual bool GetLeader(std::string* ip, int* port) override;
  virtual bool HasLeader() override;
  virtual bool IsLeader() override;

  int GetLocalPort() {
    return options_.local_port;
  }

  virtual bool GetServerStatus(std::string* msg);
  // log level can be modified
  void set_log_level(const int log_level);
  // used when membership changed
  void AddNewPeer(const std::string& server);
  void RemoveOutPeer(const std::string& server);

 private:
  // friend class Floyd;
  friend class FloydWorkerConn;
  friend class FloydWorkerHandle;
  friend class Peer;

  boost::asio::io_context ctx;
  rocksdb::DB* db_;
  // state machine db point
  // raft log
  rocksdb::DB* log_and_meta_;  // used to store logs and meta data
  std::unique_ptr<RaftLog> raft_log_;
  std::unique_ptr<RaftMeta> raft_meta_;

  Options options_;
  // debug log used for ouput to file
  Logger* info_log_;

  std::unique_ptr<FloydContext> context_;

  std::unique_ptr<FloydWorker> worker_;
  std::unique_ptr<FloydApply> apply_;
  std::unique_ptr<FloydPrimary> primary_;
  PeersSet peers_;
  std::unique_ptr<ClientPool> worker_client_pool_;

  bool IsSelf(const std::string& ip_port);

  nonstd::expected<CmdResponse, std::error_code> DoCommand(const CmdRequest& cmd);
  nonstd::expected<CmdResponse, std::error_code> ExecuteCommand(const CmdRequest& cmd);
  CmdResponse::ServerStatus DoGetServerStatus();

  /*
   * these two are the response to the request vote and appendentries
   */
  std::pair<int, CmdResponse> ReplyRequestVote(const CmdRequest1& cmd);
  std::pair<int, CmdResponse> ReplyAppendEntries(const AppendEntries& cmd);

  bool AdvanceFollowerCommitIndex(uint64_t new_commit_index);

  int InitPeers();

  // No coping allowed
  FloydImpl(const FloydImpl&);
  void operator=(const FloydImpl&);
};  // FloydImpl

}  // namespace floyd
#endif  // FLOYD_SRC_FLOYD_IMPL_H_
