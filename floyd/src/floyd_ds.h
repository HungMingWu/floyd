#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
namespace floyd {
  uint64_t NowMicros()
  {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  }
  /*
   * Lock is used storing data in database
   * since the Lock should contain more than one variable as the value
   * so we need a struct to serialize the value
   */
  struct Lock {
    std::string holder;
    uint64_t lease_end;
    template <class Archive>
    void serialize(Archive & ar)
    {
      ar(holder, lease_end);
    }
  };

  /*
   * Membership record current membership config of floyd
   */
  struct Membership {
    std::vector<std::string> nodes;
    template <class Archive>
    void serialize(Archive & ar)
    {
      ar(nodes);
    }
    const std::vector<std::string>& getnodes() const
    {
      return nodes;
    }
    void add_nodes(const std::string& value)
    {
      nodes.push_back(value);
    }
    void remove_nodes(const std::string& value)
    {
      auto it = std::find(begin(nodes), end(nodes), value);
      nodes.erase(it);
    }
    bool exists(const std::string& value) const
    {
      return std::find(begin(nodes), end(nodes), value) != end(nodes);
    }
    Membership() = default;
    ~Membership() = default;
    Membership(const std::vector<std::string>& value) : nodes(value) {}
  };

  /*
   * Entry is used storing data in raft log
   */
  struct Entry {
    enum class OpType : uint8_t {
      kRead = 0,
      kWrite = 1,
      kDelete = 2,
      kTryLock = 4,
      kUnLock = 5,
      kAddServer = 6,
      kRemoveServer = 7,
      kGetAllServers = 8
    };
    // used in key value operator
    uint64_t term;
    std::string key;
    std::string value;
    OpType optype;
    // used in lock and unlock
    std::string holder;
    uint64_t lease_end;
    std::string server;

    template <class Archive>
    void serialize(Archive & ar)
    {
      ar(term, key, value, optype, holder, lease_end, server);
    }
  };

  // Raft RPC is the RPC presented in raft paper
  // User cmd RPC the cmd build upon the raft protocol
  enum class Type : uint8_t {
    // User cmd
    kRead = 0,
    kWrite = 1,
    kDelete = 3,
    kTryLock = 5,
    kUnLock = 6,
    kAddServer = 11,
    kRemoveServer = 12,
    kGetAllServers = 13,

    // Raft RPC
    kRequestVote = 8,
    kAppendEntries = 9,
    kServerStatus = 10
  };

  struct CmdRequest {
    struct RequestVote {
      uint64_t term;
      std::string ip;
      int32_t port;
      uint64_t last_log_index;
      uint64_t last_log_term;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(term, ip, port, last_log_index, last_log_term);
      }
    };
    struct AppendEntries {
      uint64_t term;
      std::string ip;
      int32_t port;
      uint64_t prev_log_index;
      uint64_t prev_log_term;
      uint64_t leader_commit;
      std::vector<Entry> entries;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(term, ip, port, prev_log_index, prev_log_term, leader_commit, entries);
      }
    };
    struct KvRequest {
      std::string key;
      std::string value;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(key, value);
      }
    };
    struct LockRequest {
      std::string name;
      std::string holder;
      uint64_t lease_end;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(name, holder, lease_end);
      }
    };

    struct AddServerRequest {
      std::string new_server;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(new_server);
      }
    };

    struct RemoveServerRequest {
      std::string old_server;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(old_server);
      }
    };

    Type type;
    RequestVote request_vote;
    AppendEntries append_entries;
    KvRequest kv_request;
    LockRequest lock_request;
    AddServerRequest add_server_request;
    RemoveServerRequest remove_server_request;
    template <class Archive>
    void serialize(Archive & ar)
    {
      ar(type, request_vote, append_entries, kv_request, lock_request, add_server_request, remove_server_request);
    }
  };

  struct CmdResponse {
    enum class StatusCode : uint8_t {
      kOk = 0,
      kNotFound = 1,
      kError = 2,
      kLocked = 3
    };
    struct RequestVoteResponse {
      uint64_t term;
      bool vote_granted;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(term, vote_granted);
      }
    };
    struct AppendEntriesResponse {
      uint64_t term;
      bool success;
      uint64_t last_log_index;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(term, success, last_log_index);
      }
    };
    struct KvResponse {
      std::string value;
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(value);
      }
    };
    struct ServerStatus {
      uint64_t term;
      uint64_t commit_index;
      std::string role;
      std::string leader_ip;
      int32_t leader_port;
      std::string voted_for_ip;
      int32_t voted_for_port;
      uint64_t last_log_term;
      uint64_t last_log_index;
      uint64_t last_applied;
    };
    Type type;
    StatusCode code;
    RequestVoteResponse request_vote_res;
    AppendEntriesResponse append_entries_res;
    std::string msg;
    KvResponse kv_response;
    ServerStatus server_status;
    Membership all_servers;
    template <class Archive>
    void serialize(Archive & ar)
    {
      ar(type, code, request_vote_res, append_entries_res, msg, kv_response, server_status, all_servers);
    }
  };
};
