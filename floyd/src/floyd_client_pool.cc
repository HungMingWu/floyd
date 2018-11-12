// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/floyd_client_pool.h"

#include <unistd.h>
#include "floyd/src/logger.h"
#include "floyd/include/floyd_options.h"

#include "floyd/src/floyd_ds.h"

namespace floyd {

struct CmdVisitor {
  std::string operator()(const ReadRequest&) {
    return "Read";
  }
  std::string operator()(const AppendEntries&) {
    return "AppendEntries";
  }
  std::string operator()(const LockRequest &cmd) {
    if (cmd.operate == LockRequest::LockOperator::kTryLock) return "TryLock";
    else if (cmd.operate == LockRequest::LockOperator::kUnLock) return "UnLock";
    else return "UnknownCmd";
  }
  std::string operator()(const CmdRequest1& cmd) {
    switch (cmd.type) {
      case Type::kRead:
        return "Read";
      case Type::kWrite:
        return "Write";
      case Type::kDelete:
        return "Delete";
      case Type::kRequestVote:
        return "RequestVote";
      case Type::kAppendEntries:
        return "AppendEntries";
      case Type::kServerStatus:
        return "ServerStatus";
      default:
        return  "UnknownCmd";
    }
  }
};
static std::string CmdType(const CmdRequest& cmd) {
  return std::visit(CmdVisitor{}, cmd);
}


ClientPool::ClientPool(Logger* info_log, int timeout_ms, int retry)
  : info_log_(info_log),
    timeout_ms_(timeout_ms),
    retry_(retry) {
}

// sleep 1 second after each send message
nonstd::expected<CmdResponse, std::error_code> ClientPool::SendAndRecv(const std::string& server, const CmdRequest& req) {
  /*
   * if (req.type() == kAppendEntries) {
   *   LOGV(INFO_LEVEL, info_log_, "ClientPool::SendAndRecv to %s"
   *       " Request type %s prev_log_index %lu prev_log_term %lu leader_commit %lu "
   *       "append entries size %d at term %d", server.c_str(),
   *       CmdType(req).c_str(), req.append_entries().prev_log_index(), req.append_entries().prev_log_term(),
   *       req.append_entries().leader_commit(), req.append_entries().entries().size(), req.append_entries().term());
   * }
   */
  LOGV(DEBUG_LEVEL, info_log_, "ClientPool::SendAndRecv Send %s command to server %s", CmdType(req).c_str(), server.c_str());
  Client *client = GetClient(server);
  //pink::PinkCli* cli = client->cli;

  std::lock_guard l(client->mu);
  std::error_code ret = UpHoldCli(client);
  if (!ret) {
#if 0
    if (req.type == Type::kAppendEntries) {
      LOGV(WARN_LEVEL, info_log_, "ClientPool::SendAndRecv Server Connect to %s failed, error reason: %s"
          " Request type %s prev_log_index %lu prev_log_term %lu leader_commit %lu "
          "append entries size %d at term %d", server.c_str(), ret.message().c_str(),
          CmdType(req).c_str(), req.append_entries.prev_log_index, req.append_entries.prev_log_term,
          req.append_entries.leader_commit, req.append_entries.entries.size(), req.append_entries.term);
    } else {
      LOGV(WARN_LEVEL, info_log_, "ClientPool::SendAndRecv Server Connect to %s failed, error reason: %s"
          " Request type %s", server.c_str(), ret.message().c_str(), CmdType(req).c_str());
      sleep(1);
    }
#endif
    //cli->Close();
    return nonstd::make_unexpected(ret);
  }

#if 0
  ret = cli->Send((void *)(&req));
#endif
  if (!ret) {
#if 0
    if (req.type == Type::kAppendEntries) {
      LOGV(WARN_LEVEL, info_log_, "ClientPool::SendAndRecv Server Send to %s failed, error reason: %s"
          " Request type %s prev_log_index %lu prev_log_term %lu leader_commit %lu "
          "append entries size %d at term %d", server.c_str(), ret.message().c_str(),
          CmdType(req).c_str(), req.append_entries.prev_log_index, req.append_entries.prev_log_term,
          req.append_entries.leader_commit, req.append_entries.entries.size(), req.append_entries.term);
    } else {
      LOGV(WARN_LEVEL, info_log_, "ClientPool::SendAndRecv Server Send to %s failed, error reason: %s"
          " Request type %s", server.c_str(), ret.message().c_str(), CmdType(req).c_str());
      sleep(1);
    }
#endif
    //cli->Close();
    return nonstd::make_unexpected(ret);
  }

#if 0
  ret = cli->Recv(res);
#endif
  if (!ret) {
#if 0
    if (req.type == Type::kAppendEntries) {
      LOGV(WARN_LEVEL, info_log_, "ClientPool::SendAndRecv Server Recv to %s failed, error reason: %s"
          " Request type %s prev_log_index %lu prev_log_term %lu leader_commit %lu "
          "append entries size %d at term %d", server.c_str(), ret.message().c_str(),
          CmdType(req).c_str(), req.append_entries.prev_log_index, req.append_entries.prev_log_term,
          req.append_entries.leader_commit, req.append_entries.entries.size(), req.append_entries.term);
    } else {
      LOGV(WARN_LEVEL, info_log_, "ClientPool::SendAndRecv Server Recv to %s failed, error reason: %s"
          " Request type %s", server.c_str(), ret.message().c_str(), CmdType(req).c_str());
      sleep(1);
    }
#endif
    //cli->Close();
    return nonstd::make_unexpected(ret);
  }
  CmdResponse res;
  if (!ret) {
    if (res.code == CmdResponse::StatusCode::kOk || res.code == CmdResponse::StatusCode::kNotFound) {
      return res;
    }
  }
  return res;
}

ClientPool::~ClientPool() {
  std::lock_guard l(mu_);
  for (auto& iter : client_map_) {
    delete iter.second;
  }
  LOGV(DEBUG_LEVEL, info_log_, "ClientPool dtor");
}

Client* ClientPool::GetClient(const std::string& server) {
  std::lock_guard l(mu_);
  auto iter = client_map_.find(server);
  if (iter == client_map_.end()) {
    std::string ip;
    int port;
    // slash::ParseIpPortString(server, ip, port);
    Client* client = new Client(ip, port);
    client_map_[server] = client;
    return client;
  } else {
    return iter->second;
  }
}

std::error_code ClientPool::UpHoldCli(Client *client) {
  std::error_code ret;
#if 0
  if (client == NULL || client->cli == NULL) {
    // return Status::Corruption("null PinkCli");
    return {};
  }

  pink::PinkCli* cli = client->cli;
  if (!cli->Available()) {
    ret = cli->Connect();
    if (!ret) {
      cli->set_send_timeout(timeout_ms_);
      cli->set_recv_timeout(timeout_ms_);
    }
  }
#endif
  return ret;
}

} // namespace floyd
