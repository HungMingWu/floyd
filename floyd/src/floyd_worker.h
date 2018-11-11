// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_FLOYD_WORKER_H_
#define FLOYD_SRC_FLOYD_WORKER_H_

#include <string>

#include "floyd/src/floyd_ds.h"

namespace floyd {

class FloydImpl;
class FloydWorkerConnFactory;
class FloydWorkerHandle;

class FloydWorkerConn {
 public:
  FloydWorkerConn(int fd, const std::string& ip_port, FloydImpl* floyd);
  virtual ~FloydWorkerConn();

  virtual int DealMessage();

 private:
  FloydImpl* const floyd_;
  CmdRequest request_;
  CmdResponse response_;
};

class FloydWorker {
 public:
  FloydWorker(int port, int cron_interval, FloydImpl* floyd);

  ~FloydWorker() {
    // thread_->StopThread();
  }

 private:
};

}  // namespace floyd
#endif  // FLOYD_SRC_FLOYD_WORKER_H_
