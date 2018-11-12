// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_SRC_FLOYD_CLIENT_POOL_H_
#define FLOYD_SRC_FLOYD_CLIENT_POOL_H_

#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <system_error>

#include "floyd/src/expected.hpp"
#include "floyd/src/floyd_ds.h"

namespace floyd {

class Logger;
class CmdResponse;

struct Client {
  //pink::PinkCli* cli;
  std::mutex mu;

  Client(const std::string& ip, int port) {
    //cli = pink::NewPbCli(ip, port);
  }
};
class ClientPool {
 public:
  explicit ClientPool(Logger* info_log_, int timeout_ms = 2000, int retry = 0);
  ~ClientPool();

  // Each try consists of Connect, Send and Recv;
  nonstd::expected<CmdResponse, std::error_code> SendAndRecv(const std::string& server, const CmdRequest& req);

  std::error_code UpHoldCli(Client* client);

 private:
  Logger* const info_log_;
  int timeout_ms_;
  int retry_;
  std::mutex mu_;
  std::map<std::string, Client*> client_map_;

  Client* GetClient(const std::string& server);

  ClientPool(const ClientPool&);
  bool operator=(const ClientPool&);
};


} // namespace floyd
#endif  // FLOYD_SRC_FLOYD_CLIENT_POOL_H_
