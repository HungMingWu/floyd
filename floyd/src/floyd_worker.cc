// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/floyd_worker.h"

#include <google/protobuf/text_format.h>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/binary.hpp>

#include "floyd/src/floyd_impl.h"
#include "floyd/src/logger.h"
#include "slash/include/env.h"

namespace floyd {
using slash::Status;

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

  response_.Clear();
  response_.set_type(Type::kRead);
  set_is_reply(true);

  // why we still need to deal with message that is not these type
  switch (request_.gettype()) {
    case Type1::kWrite:
      response_.set_type(Type::kWrite);
      response_.set_code(StatusCode::kError);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kDelete:
      response_.set_type(Type::kDelete);
      response_.set_code(StatusCode::kError);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kRead:
      response_.set_type(Type::kRead);
      response_.set_code(StatusCode::kError);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kTryLock:
      response_.set_type(Type::kTryLock);
      response_.set_code(StatusCode::kError);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kUnLock:
      response_.set_type(Type::kUnLock);
      response_.set_code(StatusCode::kError);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kServerStatus:
      response_.set_type(Type::kRead);
      break;
      response_.set_type(Type::kServerStatus);
      response_.set_code(StatusCode::kError);
      LOGV(WARN_LEVEL, floyd_->info_log_, "obsolete command kServerStatus");
      break;
    case Type1::kAddServer:
      response_.set_type(Type::kAddServer);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kRemoveServer:
      response_.set_type(Type::kRemoveServer);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kGetAllServers:
      response_.set_type(Type::kGetAllServers);
      floyd_->DoCommand(request_, &response_);
      break;
    case Type1::kRequestVote:
      response_.set_type(Type::kRequestVote);
      floyd_->ReplyRequestVote(request_, &response_);
      response_.set_code(StatusCode::kOk);
      break;
    case Type1::kAppendEntries:
      response_.set_type(Type::kAppendEntries);
      floyd_->ReplyAppendEntries(request_, &response_);
      response_.set_code(StatusCode::kOk);
      break;
    default:
      response_.set_type(Type::kRead);
      LOGV(WARN_LEVEL, floyd_->info_log_, "unknown cmd type");
      break;
  }
  res_ = &response_;
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
