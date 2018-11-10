// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/floyd_impl.h"

#include <utility>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/binary.hpp>

#include "pink/include/bg_thread.h"
#include "slash/include/env.h"
#include "slash/include/slash_string.h"

#include "floyd/src/floyd_context.h"
#include "floyd/src/floyd_apply.h"
#include "floyd/src/floyd_worker.h"
#include "floyd/src/raft_log.h"
#include "floyd/src/floyd_peer.h"
#include "floyd/src/floyd_primary.h"
#include "floyd/src/floyd_client_pool.h"
#include "floyd/src/logger.h"
#include "floyd/src/raft_meta.h"
#include "floyd/src/floyd_ds.h"

namespace floyd {

static void BuildReadRequest(const std::string& key, CmdRequest* cmd) {
  cmd->type = Type::kRead;
  auto &kv_request = cmd->kv_request;
  kv_request.key = key;
}

static void BuildReadResponse(const std::string &key, const std::string &value,
                              CmdResponse::StatusCode code, CmdResponse *response) {
  response->code = code;
  auto &kv_response = response->kv_response;
  if (code == CmdResponse::StatusCode::kOk) {
    kv_response.value = value;
  }
}

static void BuildWriteRequest(const std::string& key,
                              const std::string& value, CmdRequest* cmd) {
  cmd->type = Type::kWrite;
  auto &kv_request = cmd->kv_request;
  kv_request.key = key;;
  kv_request.value = value;
}

static void BuildDeleteRequest(const std::string& key, CmdRequest* cmd) {
  cmd->type = Type::kDelete;
  auto &kv_request = cmd->kv_request;
  kv_request.key = key;
}

static void BuildTryLockRequest(const std::string& name, const std::string& holder, uint64_t ttl,
                              CmdRequest* cmd) {
  cmd->type = Type::kTryLock;
  auto &lock_request = cmd->lock_request;
  lock_request.name = name;
  lock_request.holder = holder;
  lock_request.lease_end = slash::NowMicros() + ttl * 1000;
}

static void BuildUnLockRequest(const std::string& name, const std::string& holder,
                              CmdRequest* cmd) {
  cmd->type = Type::kUnLock;
  auto &lock_request = cmd->lock_request;
  lock_request.name = name;
  lock_request.holder = holder;
}

static void BuildAddServerRequest(const std::string& new_server, CmdRequest* cmd) {
  cmd->type = Type::kAddServer;
  auto &add_server_request = cmd->add_server_request;
  add_server_request.new_server = new_server;
}

static void BuildRemoveServerRequest(const std::string& old_server, CmdRequest* cmd) {
  cmd->type = Type::kRemoveServer;
  auto &remove_server_request = cmd->remove_server_request;
  remove_server_request.old_server = old_server;
}

static void BuildGetAllServersRequest(CmdRequest* cmd) {
  cmd->type = Type::kGetAllServers;
}

static void BuildRequestVoteResponse(uint64_t term, bool granted,
                                     CmdResponse* response) {
  response->type = Type::kRequestVote;
  auto &request_vote_res = response->request_vote_res;
  request_vote_res.term = term;
  request_vote_res.vote_granted = granted;
}

static void BuildAppendEntriesResponse(bool succ, uint64_t term,
                                       uint64_t log_index,
                                       CmdResponse* response) {
  response->type = Type::kAppendEntries;
  auto &append_entries_res = response->append_entries_res;
  append_entries_res.term = term;
  append_entries_res.last_log_index = log_index;
  append_entries_res.success = succ;
}

static std::vector<Entry> BuildLogEntry(const CmdRequest& cmd, uint64_t current_term) {
  Entry entry;
  entry.term = current_term;
  entry.key = cmd.kv_request.key;
  entry.value = cmd.kv_request.value;
  if (cmd.type == Type::kRead) {
    entry.optype = Entry::OpType::kRead;
  } else if (cmd.type == Type::kWrite) {
    entry.optype = Entry::OpType::kWrite;
  } else if (cmd.type == Type::kDelete) {
    entry.optype = Entry::OpType::kDelete;
  } else if (cmd.type == Type::kTryLock) {
    entry.optype = Entry::OpType::kTryLock;
    entry.key = cmd.lock_request.name;
    entry.holder = cmd.lock_request.holder;
    entry.lease_end = cmd.lock_request.lease_end;
  } else if (cmd.type == Type::kUnLock) {
    entry.optype = Entry::OpType::kUnLock;
    entry.key = cmd.lock_request.name;
    entry.holder = cmd.lock_request.holder;
  } else if (cmd.type == Type::kAddServer) {
    entry.optype = Entry::OpType::kAddServer;
    entry.server = cmd.add_server_request.new_server;
  } else if (cmd.type == Type::kRemoveServer) {
    entry.optype = Entry::OpType::kRemoveServer;
    entry.server = cmd.remove_server_request.old_server;
  } else if (cmd.type == Type::kGetAllServers) {
    entry.optype = Entry::OpType::kGetAllServers;
  }
  return { entry };
}

FloydImpl::FloydImpl(const Options& options)
  : db_(NULL),
    log_and_meta_(NULL),
    options_(options),
    info_log_(NULL) {
}

FloydImpl::~FloydImpl() {
  // worker will use floyd, delete worker first
  worker_->Stop();
  delete info_log_;
  delete db_;
  delete log_and_meta_;
}

bool FloydImpl::IsSelf(const std::string& ip_port) {
  return (ip_port == slash::IpPortString(options_.local_ip, options_.local_port));
}

bool FloydImpl::GetLeader(std::string *ip_port) {
  if (context_->leader_ip.empty() || context_->leader_port == 0) {
    return false;
  }
  *ip_port = slash::IpPortString(context_->leader_ip, context_->leader_port);
  return true;
}

bool FloydImpl::IsLeader() {
  if (context_->leader_ip == "" || context_->leader_port == 0) {
    return false;
  }
  if (context_->leader_ip == options_.local_ip && context_->leader_port == options_.local_port) {
    return true;
  }
  return false;
}

bool FloydImpl::GetLeader(std::string* ip, int* port) {
  *ip = context_->leader_ip;
  *port = context_->leader_port;
  return (!ip->empty() && *port != 0);
}

bool FloydImpl::HasLeader() {
  if (context_->leader_ip == "" || context_->leader_port == 0) {
    return false;
  }
  return true;
}

void FloydImpl::set_log_level(const int log_level) {
  if (info_log_) {
    info_log_->set_log_level(log_level);
  }
}

void FloydImpl::AddNewPeer(const std::string& server) {
  if (IsSelf(server)) {
    return;
  }
  // Add Peer
  auto peers_iter = peers_.find(server);
  if (peers_iter == peers_.end()) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ApplyAddMember server %s:%d add new peer thread %s",
        options_.local_ip.c_str(), options_.local_port, server.c_str());
    Peer* pt = new Peer(ctx, server, peers_, *context_, *primary_, *raft_meta_, *raft_log_,
        *worker_client_pool_, *apply_, options_, info_log_);
    peers_.insert(std::pair<std::string, Peer*>(server, pt));
  }
}

void FloydImpl::RemoveOutPeer(const std::string& server) {
  if (IsSelf(server)) {
    return; 
  }
  // Stop and remove peer
  auto peers_iter = peers_.find(server);
  if (peers_iter != peers_.end()) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ApplyRemoveMember server %s:%d remove peer thread %s",
        options_.local_ip.c_str(), options_.local_port, server.c_str());
    peers_.erase(peers_iter);
  }
}

int FloydImpl::InitPeers() {
  // Create peer threads
  // peers_.clear();
  for (auto iter = context_->members.begin(); iter != context_->members.end(); iter++) {
    if (!IsSelf(*iter)) {
      Peer* pt = new Peer(ctx, *iter, peers_, *context_, *primary_, *raft_meta_, *raft_log_,
          *worker_client_pool_, *apply_, options_, info_log_);
      peers_.insert(std::pair<std::string, Peer*>(*iter, pt));
    }
  }

  LOGV(INFO_LEVEL, info_log_, "FloydImpl::InitPeers Floyd start %d peer thread", peers_.size());
  return 0;
}

std::error_code FloydImpl::Init() {
  slash::CreatePath(options_.path);
  if (NewLogger(options_.path + "/LOG", &info_log_) != 0) {
    // return Status::Corruption("Open LOG failed, ", strerror(errno));
    return {};
  }

  // TODO(anan) set timeout and retry
  worker_client_pool_ = std::make_unique<ClientPool>(info_log_);

  // Create DB
  rocksdb::Options options;
  options.create_if_missing = true;
  options.write_buffer_size = 1024 * 1024 * 1024;
  options.max_background_flushes = 8;
  rocksdb::Status s = rocksdb::DB::Open(options, options_.path + "/db/", &db_);
  if (!s.ok()) {
    LOGV(ERROR_LEVEL, info_log_, "Open db failed! path: %s", options_.path.c_str());
    // return Status::Corruption("Open DB failed, " + s.ToString());
    return {};
  }

  s = rocksdb::DB::Open(options, options_.path + "/log/", &log_and_meta_);
  if (!s.ok()) {
    LOGV(ERROR_LEVEL, info_log_, "Open DB log_and_meta failed! path: %s", options_.path.c_str());
    // return Status::Corruption("Open DB log_and_meta failed, " + s.ToString());
    return {};
  }

  // Recover Context
  raft_log_ = std::make_unique<RaftLog>(log_and_meta_, info_log_);
  raft_meta_ = std::make_unique<RaftMeta>(log_and_meta_);
  raft_meta_->Init();
  context_ = std::make_unique<FloydContext>(options_);
  context_->RecoverInit(*raft_meta_);

  // Recover Members when exist
  std::string mval;
  s = db_->Get(rocksdb::ReadOptions(), kMemberConfigKey, &mval);
  if (s.ok()) {
    Membership db_members;
    std::istringstream is(mval);
    cereal::BinaryInputArchive archive(is);
    archive(db_members);
    // Prefer persistent membership than config
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::Init: Load Membership from db, count: %d", db_members.getnodes().size());
    for (const auto &node : db_members.getnodes()) {
      context_->members.insert(node);
    }
  } else {
    Membership db_members(options_.members);
    std::ostringstream os;
    cereal::BinaryOutputArchive archive(os);
    archive(db_members);
    s = db_->Put(rocksdb::WriteOptions(), kMemberConfigKey, os.str());
    if (!s.ok()) {
      LOGV(ERROR_LEVEL, info_log_, "Record membership in db failed! error: %s", s.ToString().c_str());
      // return Status::Corruption("Record membership in db failed! error: " + s.ToString());
      return {};
    }
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::Init: Load Membership from option, count: %d", options_.members.size());
    for (const auto& m : options_.members) {
      context_->members.insert(m);
    }
  }

  // peers and primary refer to each other
  // Create PrimaryThread before Peers
  primary_ = std::make_unique<FloydPrimary>(ctx, *context_, peers_, *raft_meta_, options_, info_log_);

  // Start worker thread after Peers, because WorkerHandle will check peers
  worker_ = std::make_unique<FloydWorker>(options_.local_port, 1000, this);
  int ret = 0;
  if ((ret = worker_->Start()) != 0) {
    LOGV(ERROR_LEVEL, info_log_, "FloydImpl::Init worker thread failed to start, ret is %d", ret);
    // return Status::Corruption("failed to start worker, return " + std::to_string(ret));
    return {};
  }
  // Apply thread should start at the last
  apply_ = std::make_unique<FloydApply>(ctx, *context_, db_, *raft_meta_, *raft_log_, this, info_log_);

  InitPeers();

  primary_->AddTask(kCheckLeader);

  // test only
  // options_.Dump();
  LOGV(INFO_LEVEL, info_log_, "FloydImpl::Init Floyd started!\nOptions\n%s", options_.ToString().c_str());
  return {};
}

std::error_code Floyd::Open(const Options& options, Floyd** floyd) {
  *floyd = NULL;
  FloydImpl *impl = new FloydImpl(options);
  std::error_code s = impl->Init();
  if (!s) {
    *floyd = impl;
  } else {
    delete impl;
  }
  return s;
}

Floyd::~Floyd() {
}

std::error_code FloydImpl::Write(const std::string& key, const std::string& value) {
  CmdRequest cmd;
  BuildWriteRequest(key, value, &cmd);
  CmdResponse response;
  std::error_code s = DoCommand(cmd, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    return {};
  }
  //return Status::Corruption("Write Error");
  return s;
}

std::error_code FloydImpl::Delete(const std::string& key) {
  CmdRequest cmd;
  BuildDeleteRequest(key, &cmd);
  CmdResponse response;
  std::error_code s = DoCommand(cmd, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    return {};
  }
  //return Status::Corruption("Delete Error");
  return s;
}

std::error_code FloydImpl::Read(const std::string& key, std::string* value) {
  CmdRequest request;
  BuildReadRequest(key, &request);
  CmdResponse response;
  std::error_code s = DoCommand(request, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    *value = response.kv_response.value;
    return {};
  } else if (response.code == CmdResponse::StatusCode::kNotFound) {
    // return Status::NotFound("not found the key");
    return s;
  } else {
    // return Status::Corruption("Read Error");
    return s;
  }
}

std::error_code FloydImpl::DirtyRead(const std::string& key, std::string* value) {
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), key, value);
  if (s.ok()) {
    return {};
  } else if (s.IsNotFound()) {
    // return Status::NotFound("");
    return {};
  }
  // return Status::Corruption(s.ToString());
  return {};
}

std::error_code FloydImpl::TryLock(const std::string& name, const std::string& holder, uint64_t ttl) {
  CmdRequest request;
  BuildTryLockRequest(name, holder, ttl, &request);
  CmdResponse response;
  std::error_code s = DoCommand(request, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    return {};
  }
  // return Status::Corruption("Lock Error");
  return s;
}

std::error_code FloydImpl::UnLock(const std::string& name, const std::string& holder) {
  CmdRequest request;
  BuildUnLockRequest(name, holder, &request);
  CmdResponse response;
  std::error_code s = DoCommand(request, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    return {};
  }
  // return Status::Corruption("UnLock Error");
  return s;
}

std::error_code FloydImpl::AddServer(const std::string& new_server) {
  CmdRequest request;
  BuildAddServerRequest(new_server, &request);
  CmdResponse response;
  std::error_code s = DoCommand(request, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    return {};
  }
  // return Status::Corruption("AddServer Error");
  return s;
}

std::error_code FloydImpl::RemoveServer(const std::string& old_server) {
  CmdRequest request;
  BuildRemoveServerRequest(old_server, &request);
  CmdResponse response;
  std::error_code s = DoCommand(request, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    return {};
  }
  // return Status::Corruption("RemoveServer Error");
  return s;
}

std::error_code FloydImpl::GetAllServers(std::set<std::string>* nodes) {
  CmdRequest request;
  BuildGetAllServersRequest(&request);
  CmdResponse response;
  std::error_code s = DoCommand(request, &response);
  if (!s) {
    return s;
  }
  if (response.code == CmdResponse::StatusCode::kOk) {
    nodes->clear();
    for (const auto &node : response.all_servers.nodes) {
      nodes->insert(node);
    }
    return {};
  }
  // return Status::Corruption("GetALlServers Error");
  return s;
}


bool FloydImpl::GetServerStatus(std::string* msg) {
  LOGV(DEBUG_LEVEL, info_log_, "FloydImpl::GetServerStatus start");
  CmdResponse::ServerStatus server_status;
  {
  std::lock_guard l(context_->global_mu);
  DoGetServerStatus(&server_status);
  }

  char str[512];
  snprintf (str, sizeof(str),
            "      Node           |    Role    | Term |      Leader      |      VoteFor      | LastLogTerm | LastLogIdx | CommitIndex | LastApplied |\n"
            "%15s:%-6d%10s%7lu%14s:%-6d%14s:%-6d%10lu%13lu%14lu%13lu\n",
            options_.local_ip.c_str(), options_.local_port, server_status.role.c_str(), server_status.term,
            server_status.leader_ip.c_str(), server_status.leader_port,
            server_status.voted_for_ip.c_str(), server_status.voted_for_port,
            server_status.last_log_term, server_status.last_log_index, server_status.commit_index,
            server_status.last_applied);

  msg->clear();
  msg->append(str);
  return true;
}

std::error_code FloydImpl::DoCommand(const CmdRequest& request, CmdResponse *response) {
  // Execute if is leader
  auto [leader_ip, leader_port] = [this]
  {
    std::lock_guard l(context_->global_mu);
    return std::make_tuple(context_->leader_ip, context_->leader_port);
  }();

  if (options_.local_ip == leader_ip && options_.local_port == leader_port) {
    return ExecuteCommand(request, response);
  } else if (leader_ip == "" || leader_port == 0) {
    // return Status::Incomplete("no leader node!");
    return {};
  }
  // Redirect to leader
  return worker_client_pool_->SendAndRecv(
      slash::IpPortString(leader_ip, leader_port),
      request, response);
}

bool FloydImpl::DoGetServerStatus(CmdResponse::ServerStatus* res) {
  std::string role_msg;
  switch (context_->role) {
    case Role::kFollower:
      role_msg = "follower";
      break;
    case Role::kCandidate:
      role_msg = "candidate";
      break;
    case Role::kLeader:
      role_msg = "leader";
      break;
  }

  res->term = context_->current_term;
  res->commit_index = context_->commit_index;
  res->role = role_msg;

  std::string ip;
  int port;
  ip = context_->leader_ip;
  port = context_->leader_port;
  if (ip.empty()) {
    res->leader_ip = "null";
  } else {
    res->leader_ip = ip;
  }
  res->leader_port = port;

  ip = context_->voted_for_ip;
  port = context_->voted_for_port;
  if (ip.empty()) {
    res->voted_for_ip = "null";
  } else {
    res->voted_for_ip = ip;
  }
  res->voted_for_port = port;

  uint64_t last_log_index;
  uint64_t last_log_term;
  raft_log_->GetLastLogTermAndIndex(&last_log_term, &last_log_index);

  res->last_log_term = last_log_term;
  res->last_log_index = last_log_index;
  res->last_applied = raft_meta_->GetLastApplied();
  return true;
}

std::error_code FloydImpl::ExecuteCommand(const CmdRequest& request,
                                 CmdResponse *response) {
  // Append entry local
  std::vector<Entry> entries = BuildLogEntry(request, context_->current_term);
  response->type = request.type;
  response->code = CmdResponse::StatusCode::kError;

  uint64_t last_log_index = raft_log_->Append(entries);
  if (last_log_index <= 0) {
    // return Status::IOError("Append Entry failed");
    return {};
  }

  // Notify primary then wait for apply
  if (options_.single_mode) {
    context_->commit_index = last_log_index;
    raft_meta_->SetCommitIndex(context_->commit_index);
    apply_->ScheduleApply();
  } else {
    primary_->AddTask(kNewCommand);
  }

  {
  std::unique_lock l(context_->apply_mu);
  while (context_->last_applied < last_log_index) {
    if (context_->apply_cond.wait_for(l, std::chrono::milliseconds(1000)) == std::cv_status::timeout) {
      // return Status::Timeout("FloydImpl::ExecuteCommand Timeout");
      return {};
    }
  }
  }

  // Complete CmdRequest if needed
  std::string value;
  rocksdb::Status rs;
  Lock lock;
  switch (request.type) {
    case Type::kWrite:
      response->code = CmdResponse::StatusCode::kOk;
      break;
    case Type::kDelete:
      response->code = CmdResponse::StatusCode::kOk;
      break;
    case Type::kRead:
      rs = db_->Get(rocksdb::ReadOptions(), request.kv_request.key, &value);
      if (rs.ok()) {
        BuildReadResponse(request.kv_request.key, value, CmdResponse::StatusCode::kOk, response);
      } else if (rs.IsNotFound()) {
        BuildReadResponse(request.kv_request.key, value, CmdResponse::StatusCode::kNotFound, response);
      } else {
        BuildReadResponse(request.kv_request.key, value, CmdResponse::StatusCode::kError, response);
        // return Status::Corruption("get key error");
	return {};
      }
      LOGV(DEBUG_LEVEL, info_log_, "FloydImpl::ExecuteCommand Read %s, key(%s) value(%s)",
           rs.ToString().c_str(), request.kv_request.key.c_str(), value.c_str());
      break;
    case Type::kTryLock:
      rs = db_->Get(rocksdb::ReadOptions(), request.lock_request.name, &value);
      if (rs.ok()) {
        std::istringstream is(value);
        cereal::BinaryInputArchive archive(is);
        archive(lock);
        if (lock.holder == request.lock_request.holder && lock.lease_end == request.lock_request.lease_end) {
          response->code = CmdResponse::StatusCode::kOk;
        }
      } else {
        response->code = CmdResponse::StatusCode::kLocked;
      }
      break;
    case Type::kUnLock:
      rs = db_->Get(rocksdb::ReadOptions(), request.lock_request.name, &value);
      if (rs.IsNotFound()) {
        response->code = CmdResponse::StatusCode::kOk;
      } else {
        response->code = CmdResponse::StatusCode::kLocked;
      }
      break;
    case Type::kAddServer:
      response->code = CmdResponse::StatusCode::kOk;
      break;
    case Type::kRemoveServer:
      response->code = CmdResponse::StatusCode::kOk;
      break;
    case Type::kGetAllServers:
      rs = db_->Get(rocksdb::ReadOptions(), kMemberConfigKey, &value);
      if (!rs.ok()) {
        // return Status::Corruption(rs.ToString());
	return {};
      }
      {
	std::istringstream is(value);
  	cereal::BinaryInputArchive archive(is);
        archive(response->all_servers);
      }
      response->code = CmdResponse::StatusCode::kOk;
      break;
    default:
      // return Status::Corruption("Unknown request type");
      return {};
  }
  return {};
}

int FloydImpl::ReplyRequestVote(const CmdRequest& request, CmdResponse* response) {
  std::lock_guard l(context_->global_mu);
  bool granted = false;
  const auto &request_vote = request.request_vote;
  LOGV(DEBUG_LEVEL, info_log_, "FloydImpl::ReplyRequestVote: my_term=%lu request.term=%lu",
       context_->current_term, request_vote.term);
  /*
   * If RPC request or response contains term T > currentTerm: set currentTerm = T, convert to follower (5.1)
   */
  if (request_vote.term > context_->current_term) {
    context_->BecomeFollower(request_vote.term);
    context_->voted_for_ip.clear();
    context_->voted_for_port = 0;
    raft_meta_->SetCurrentTerm(context_->current_term);
    raft_meta_->SetVotedForIp(context_->voted_for_ip);
    raft_meta_->SetVotedForPort(context_->voted_for_port);
  }
  // if caller's term smaller than my term, then I will notice him
  if (request_vote.term < context_->current_term) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyRequestVote: Leader %s:%d term %lu is smaller than my %s:%d current term %lu",
        request_vote.ip.c_str(), request_vote.port, request_vote.term, options_.local_ip.c_str(), options_.local_port,
        context_->current_term);
    BuildRequestVoteResponse(context_->current_term, granted, response);
    return -1;
  }
  uint64_t my_last_log_term = 0;
  uint64_t my_last_log_index = 0;
  raft_log_->GetLastLogTermAndIndex(&my_last_log_term, &my_last_log_index);
  // if votedfor is null or candidateId, and candidated's log is at least as up-to-date
  // as receiver's log, grant vote
  if ((request_vote.last_log_term < my_last_log_term) ||
      ((request_vote.last_log_term == my_last_log_term) && (request_vote.last_log_index < my_last_log_index))) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyRequestVote: Leader %s:%d last_log_term %lu is smaller than my(%s:%d) last_log_term term %lu,"
        " or Leader's last log term equal to my last_log_term, but Leader's last_log_index %lu is smaller than my last_log_index %lu,"
        "my current_term is %lu",
        request_vote.ip.c_str(), request_vote.port, request_vote.last_log_term, options_.local_ip.c_str(), options_.local_port,
        my_last_log_term, request_vote.last_log_index, my_last_log_index,context_->current_term);
    BuildRequestVoteResponse(context_->current_term, granted, response);
    return -1;
  }

  if (!context_->voted_for_ip.empty() || context_->voted_for_port != 0) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyRequestVote: I %s:%d have voted for %s:%d in this term %lu",
        options_.local_ip.c_str(), options_.local_port, context_->voted_for_ip.c_str(), 
        context_->voted_for_port, request_vote.term);
    BuildRequestVoteResponse(context_->current_term, granted, response);
    return -1;
  }
  LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyRequestVote: Receive Request Vote from %s:%d, "
      "Become Follower with current_term_(%lu) and new_term(%lu)"
      " commit_index(%lu) last_applied(%lu)", request_vote.ip.c_str(), request_vote.port,
      context_->current_term, request_vote.last_log_term, my_last_log_index, context_->last_applied.load());

  // Peer ask my vote with it's ip, port, log_term and log_index
  // Got my vote
  granted = true;
  context_->voted_for_ip = request_vote.ip;
  context_->voted_for_port = request_vote.port;
  raft_meta_->SetVotedForIp(context_->voted_for_ip);
  raft_meta_->SetVotedForPort(context_->voted_for_port);

  LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyRequestVote: Grant my vote to %s:%d at term %lu",
      context_->voted_for_ip.c_str(), context_->voted_for_port, context_->current_term);
  context_->last_op_time = slash::NowMicros();
  BuildRequestVoteResponse(context_->current_term, granted, response);
  return 0;
}

bool FloydImpl::AdvanceFollowerCommitIndex(uint64_t leader_commit) {
  // Update log commit index
  /*
   * If leaderCommit > commitIndex, set commitIndex =
   *   min(leaderCommit, index of last new entry)
   */
  context_->commit_index = std::min(leader_commit, raft_log_->GetLastLogIndex());
  raft_meta_->SetCommitIndex(context_->commit_index);
  return true;
}

int FloydImpl::ReplyAppendEntries(const CmdRequest& request, CmdResponse* response) {
  bool success = false;
  const auto &append_entries = request.append_entries;
  std::lock_guard l(context_->global_mu);
  // update last_op_time to avoid another leader election
  context_->last_op_time = slash::NowMicros();
  // Ignore stale term
  // if the append entries leader's term is smaller than my current term, then the caller must an older leader
  if (append_entries.term < context_->current_term) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries: Leader %s:%d term %lu is smaller than my %s:%d current term %lu",
        append_entries.ip.c_str(), append_entries.port, append_entries.term, options_.local_ip.c_str(), options_.local_port,
        context_->current_term);
    BuildAppendEntriesResponse(success, context_->current_term, raft_log_->GetLastLogIndex(), response);
    return -1;
  } else if ((append_entries.term > context_->current_term) 
      || (append_entries.term == context_->current_term && 
        (context_->role == kCandidate || (context_->role == kFollower && context_->leader_ip == "")))) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries: Leader %s:%d term %lu is larger than my %s:%d current term %lu, "
        "or leader term is equal to my current term, my role is %d, leader is [%s:%d]",
        append_entries.ip.c_str(), append_entries.port, append_entries.term, options_.local_ip.c_str(), options_.local_port,
        context_->current_term, context_->role, context_->leader_ip.c_str(), context_->leader_port);
    context_->BecomeFollower(append_entries.term,
        append_entries.ip, append_entries.port);
    context_->voted_for_ip = append_entries.ip;
    context_->voted_for_port = append_entries.port;
    raft_meta_->SetCurrentTerm(context_->current_term);
    raft_meta_->SetVotedForIp(context_->voted_for_ip);
    raft_meta_->SetVotedForPort(context_->voted_for_port);
  }

  if (append_entries.prev_log_index > raft_log_->GetLastLogIndex()) {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries: Leader %s:%d prev_log_index %lu is larger than my %s:%d last_log_index %lu",
        append_entries.ip.c_str(), append_entries.port, append_entries.prev_log_index, options_.local_ip.c_str(), options_.local_port,
        raft_log_->GetLastLogIndex());
    BuildAppendEntriesResponse(success, context_->current_term, raft_log_->GetLastLogIndex(), response);
    return -1;
  }

  // Append entry
  if (append_entries.prev_log_index < raft_log_->GetLastLogIndex()) {
    LOGV(WARN_LEVEL, info_log_, "FloydImpl::ReplyAppendEtries: Leader %s:%d prev_log_index(%lu, %lu) is smaller than"
        " my last_log_index %lu, truncate suffix from %lu", append_entries.ip.c_str(), append_entries.port,
        append_entries.prev_log_term, append_entries.prev_log_index, raft_log_->GetLastLogIndex(),
        append_entries.prev_log_index + 1);
    raft_log_->TruncateSuffix(append_entries.prev_log_index + 1);
  }

  // we compare peer's prev index and term with my last log index and term
  uint64_t my_last_log_term = 0;
  LOGV(DEBUG_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries "
      "prev_log_index: %lu\n", append_entries.prev_log_index);
  if (append_entries.prev_log_index == 0) {
    my_last_log_term = 0;
  } else if (auto entry = raft_log_->GetEntry(append_entries.prev_log_index); entry) {
    my_last_log_term = entry->term;
  } else {
    LOGV(WARN_LEVEL, info_log_, "FloydImple::ReplyAppentries: can't "
        "get Entry from raft_log prev_log_index %llu", append_entries.prev_log_index);
    BuildAppendEntriesResponse(success, context_->current_term, raft_log_->GetLastLogIndex(), response);
    return -1;
  }

  if (append_entries.prev_log_term != my_last_log_term) {
    LOGV(WARN_LEVEL, info_log_, "FloydImpl::ReplyAppentries: leader %s:%d pre_log(%lu, %lu)'s term don't match with"
         " my log(%lu, %lu) term, truncate my log from %lu", append_entries.ip.c_str(), append_entries.port,
         append_entries.prev_log_term, append_entries.prev_log_index, my_last_log_term, raft_log_->GetLastLogIndex(),
         append_entries.prev_log_index);
    // TruncateSuffix [prev_log_index, last_log_index)
    raft_log_->TruncateSuffix(append_entries.prev_log_index);
    BuildAppendEntriesResponse(success, context_->current_term, raft_log_->GetLastLogIndex(), response);
    return -1;
  }

  auto entries = append_entries.entries;
  if (append_entries.entries.size() > 0) {
    LOGV(DEBUG_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries: Leader %s:%d will append %u entries from "
         " prev_log_index %lu", append_entries.ip.c_str(), append_entries.port,
         append_entries.entries.size(), append_entries.prev_log_index);
    if (raft_log_->Append(entries) <= 0) {
      LOGV(ERROR_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries: Leader %s:%d ppend %u entries from "
          " prev_log_index %lu error at term %lu", append_entries.ip.c_str(), append_entries.port,
          append_entries.entries.size(), append_entries.prev_log_index, append_entries.term);
      BuildAppendEntriesResponse(success, context_->current_term, raft_log_->GetLastLogIndex(), response);
      return -1;
    }
  } else {
    LOGV(INFO_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries: Receive PingPong AppendEntries from %s:%d at term %lu",
        append_entries.ip.c_str(), append_entries.port, append_entries.term);
  }
  if (append_entries.leader_commit != context_->commit_index) {
    AdvanceFollowerCommitIndex(append_entries.leader_commit);
    apply_->ScheduleApply();
  }
  success = true;
  // only when follower successfully do appendentries, we will update commit index
  LOGV(DEBUG_LEVEL, info_log_, "FloydImpl::ReplyAppendEntries server %s:%d Apply %d entries from Leader %s:%d"
      " prev_log_index %lu, leader commit %lu at term %lu", options_.local_ip.c_str(),
      options_.local_port, append_entries.entries.size(), append_entries.ip.c_str(),
      append_entries.port, append_entries.prev_log_index, append_entries.leader_commit,
      append_entries.term);
  BuildAppendEntriesResponse(success, context_->current_term, raft_log_->GetLastLogIndex(), response);
  return 0;
}

}  // namespace floyd
