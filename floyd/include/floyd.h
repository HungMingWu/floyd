// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef FLOYD_INCLUDE_FLOYD_H_
#define FLOYD_INCLUDE_FLOYD_H_

#include <string>
#include <set>
#include <system_error>

#include "floyd/include/floyd_options.h"

namespace floyd {

class Floyd  {
 public:
  static std::error_code Open(const Options& options, Floyd** floyd);

  Floyd() { }
  virtual ~Floyd();

  virtual std::error_code Write(const std::string& key, const std::string& value) = 0;
  virtual std::error_code Delete(const std::string& key) = 0;
  virtual std::error_code Read(const std::string& key, std::string* value) = 0;
  virtual std::error_code DirtyRead(const std::string& key, std::string* value) = 0;
  // ttl is millisecond
  virtual std::error_code TryLock(const std::string& name, const std::string& holder, uint64_t ttl) = 0;
  virtual std::error_code UnLock(const std::string& name, const std::string& holder) = 0;

  // membership change interface
  virtual std::error_code AddServer(const std::string& new_server) = 0;
  virtual std::error_code RemoveServer(const std::string& out_server) = 0;

  // return true if leader has been elected
  virtual bool GetLeader(std::string* ip_port) = 0;
  virtual bool GetLeader(std::string* ip, int* port) = 0;
  virtual bool HasLeader() = 0;
  virtual bool IsLeader() = 0;
  virtual std::error_code GetAllServers(std::set<std::string>* nodes) = 0;

  // used for debug
  virtual bool GetServerStatus(std::string* msg) = 0;

  // log level can be modified
  virtual void set_log_level(const int log_level) = 0;

 private:
  // No coping allowed
  Floyd(const Floyd&);
  void operator=(const Floyd&);
};

} // namespace floyd
#endif  // FLOYD_INCLUDE_FLOYD_H_
