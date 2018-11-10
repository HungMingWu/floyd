// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/floyd_worker.h"

#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/binary.hpp>

#include "floyd/src/floyd_impl.h"
#include "floyd/src/logger.h"
#include "slash/include/env.h"

namespace floyd {

FloydWorker::FloydWorker(int port, int cron_interval, FloydImpl* floyd)
  : conn_factory_(floyd),
    handle_(floyd) {
    thread_ = pink::NewHolyThread(port, &conn_factory_, cron_interval, &handle_);
    thread_->set_thread_name("W:" + std::to_string(port));
}

FloydWorkerConn::FloydWorkerConn(int fd, const std::string& ip_port,
    pink::ServerThread* thread, FloydImpl* floyd)
  : PbConn(fd, ip_port, thread),
    floyd_(floyd) {
}

FloydWorkerConn::~FloydWorkerConn() {}

int FloydWorkerConn::DealMessage() {
  std::string value;
  std::istringstream is(value);
  cereal::BinaryInputArchive archive(is);
  archive(request_);

  response_.type = Type::kRead;
  set_is_reply(true);

  // why we still need to deal with message that is not these type
  switch (request_.type) {
    case Type::kWrite:
      response_.type = Type::kWrite;
      response_.code = CmdResponse::StatusCode::kError;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kDelete:
      response_.type = Type::kDelete;
      response_.code = CmdResponse::StatusCode::kError;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kRead:
      response_.type = Type::kRead;
      response_.code = CmdResponse::StatusCode::kError;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kTryLock:
      response_.type = Type::kTryLock;
      response_.code = CmdResponse::StatusCode::kError;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kUnLock:
      response_.type = Type::kUnLock;
      response_.code = CmdResponse::StatusCode::kError;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kServerStatus:
      response_.type = Type::kRead;
      break;
      response_.type = Type::kServerStatus;
      response_.code = CmdResponse::StatusCode::kError;
      LOGV(WARN_LEVEL, floyd_->info_log_, "obsolete command kServerStatus");
      break;
    case Type::kAddServer:
      response_.type = Type::kAddServer;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kRemoveServer:
      response_.type = Type::kRemoveServer;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kGetAllServers:
      response_.type = Type::kGetAllServers;
      floyd_->DoCommand(request_, &response_);
      break;
    case Type::kRequestVote:
      response_.type = Type::kRequestVote;
      floyd_->ReplyRequestVote(request_, &response_);
      response_.code = CmdResponse::StatusCode::kOk;
      break;
    case Type::kAppendEntries:
      response_.type = Type::kAppendEntries;
      floyd_->ReplyAppendEntries(request_, &response_);
      response_.code = CmdResponse::StatusCode::kOk;
      break;
    default:
      response_.type = Type::kRead;
      LOGV(WARN_LEVEL, floyd_->info_log_, "unknown cmd type");
      break;
  }
  return 0;
}

FloydWorkerHandle::FloydWorkerHandle(FloydImpl* f)
  : floyd_(f) {
  }

// Only connection from other members should be accepted
bool FloydWorkerHandle::AccessHandle(std::string& ip_port) const {
  return true;
}

}  // namespace floyd
