#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
namespace floyd {
  /*
   * Lock is used storing data in database
   * since the Lock should contain more than one variable as the value
   * so we need a struct to serialize the value
   */
  class Lock {
     std::string holder;
     uint64_t lease_end;
  public:
     template <class Archive>
     void serialize(Archive & ar)
     {
       ar(holder, lease_end);
     }
     void setholder(const std::string &value)
     {
       holder = value;
     }
     const std::string& getholder() const
     {
       return holder;
     }
     uint64_t getlease_end() const
     {
       return lease_end;
     }
     void setlease_end(uint64_t value)
     {
       lease_end = value;
     }
  };

  /*
   * Membership record current membership config of floyd
   */
  class Membership123 {
     std::vector<std::string> nodes;
  public:
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
     Membership123() = default;
     ~Membership123() = default;
     Membership123(const std::vector<std::string>& value) : nodes(value) {}
  };

  /*
   * Entry is used storing data in raft log
   */
  class Entry {
  public:
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

  public:
     template <class Archive>
     void serialize(Archive & ar)
     {
       ar(term, key, value, optype, holder, lease_end, server);
     }
     uint64_t getterm() const
     {
       return term;
     }
     void setterm(uint64_t value)
     {
       term = value;
     }
     OpType getoptype() const
     {
       return optype;
     }
     void setoptype(OpType type)
     {
       optype = type;
     }
     const std::string& getkey() const
     {
       return key;
     }
     void setkey(const std::string& value)
     {
       key = value;
     }
     const std::string& getvalue() const
     {
       return value;
     }
     void setvalue(const std::string&  v)
     {
       value = v;
     }
     const std::string& getserver() const
     {
       return server;
     }
     void setserver(const std::string &value)
     {
       server = value;
     }
     const std::string& getholder() const
     {
       return holder;
     }
     void setholder(const std::string& value)
     {
       holder = value;
     }
     uint64_t getlease_end() const
     {
       return lease_end;
     }
     void setlease_end(uint64_t value)
     {
       lease_end = value;
     }
  };

  // Raft RPC is the RPC presented in raft paper
  // User cmd RPC the cmd build upon the raft protocol
  enum class Type1 : uint8_t {
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

  class CmdRequest {
  public:
    class RequestVote {
      uint64_t term;
      std::string ip;
      int32_t port;
      uint64_t last_log_index;
      uint64_t last_log_term;
    public:
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(term, ip, port, last_log_index, last_log_term);
      }
      const std::string& getip() const
      {
        return ip;
      }
      void setip(const std::string& value)
      {
	ip = value;
      }
      int32_t getport() const
      {
        return port;
      }
      void setport(int32_t value)
      {
	port = value;
      }
      uint64_t getterm() const
      {
        return term;
      }
      void setterm(uint64_t value)
      {
	term = value;
      }
      uint64_t getlast_log_index() const
      {
	return last_log_index;
      }
      void setlast_log_index(uint64_t value)
      {
        last_log_index = value;
      }
      uint64_t getlast_log_term() const
      {
        return last_log_term;
      }
      void setlast_log_term(uint64_t value)
      {
        last_log_term = value;
      }
    };
    class AppendEntries {
      uint64_t term;
      std::string ip;
      int32_t port;
      uint64_t prev_log_index;
      uint64_t prev_log_term;
      uint64_t leader_commit;
      std::vector<Entry> entries;
    public:
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(term, ip, port, prev_log_index, prev_log_term, leader_commit, entries);
      }
      const std::vector<Entry>& getentries() const
      {
        return entries;
      }
      uint64_t getleader_commit() const
      {
        return leader_commit;
      }
      void setleader_commit(uint64_t value)
      {
	leader_commit = value;
      }
      uint64_t getterm() const
      {
        return term;
      }
      void setterm(uint64_t value)
      {
        term = value;
      }
      uint64_t getprev_log_term() const
      {
        return prev_log_term;
      }
      void setprev_log_term(uint64_t value)
      {
	prev_log_term = value;
      }
      uint64_t getprev_log_index() const
      {
        return prev_log_index;
      }
      void setprev_log_index(uint64_t value)
      {
	prev_log_index = value;
      }
      const std::string& getip() const
      {
	return ip;
      }
      void setip(const std::string& value)
      {
        ip = value;
      }
      int32_t getport() const
      {
	return port;
      }
      void setport(int32_t value)
      {
        port = value;
      }
      void appendEntry(const Entry &v)
      {
	entries.push_back(v);
      }
    };
    class KvRequest {
      std::string key;
      std::string value;
    public:
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(key, value);
      }
      const std::string& getkey() const
      {
	return key;
      }
      void setkey(const std::string& value)
      {
        key = value;
      }
      const std::string& getvalue() const
      {
	return value;
      }
      void setvalue(const std::string& v)
      {
        value = v;
      }
    };
    class LockRequest {
      std::string name;
      std::string holder;
      uint64_t lease_end;
    public:
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(name, holder, lease_end);
      }
      const std::string& getname() const
      {
        return name;
      }
      void setname(const std::string& value)
      {
	name = value;
      }
      const std::string& getholder() const
      {
	return holder;
      }
      void setholder(const std::string& value)
      {
        holder = value;
      }
      uint64_t getlease_end() const
      {
        return lease_end;
      }
      void setlease_end(uint64_t value)
      {
	lease_end = value;
      }
    };

    class AddServerRequest {
      std::string new_server;
    public:
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(new_server);
      }
      const std::string& getnew_server() const
      {
        return new_server;
      }
      void setnew_server(const std::string& value)
      {
	new_server = value;
      }
    };

    class RemoveServerRequest {
      std::string old_server;
    public:
      template <class Archive>
      void serialize(Archive & ar)
      {
        ar(old_server);
      }
      const std::string& getold_server() const
      {
        return old_server;
      }
      void setold_server(const std::string& value)
      {
	old_server = value;
      }
    };

  private:
    Type1 type_;
    RequestVote request_vote;
    AppendEntries append_entries;
    KvRequest kv_request;
    LockRequest lock_request;
    AddServerRequest add_server_request;
    RemoveServerRequest remove_server_request;
  public:
    template <class Archive>
    void serialize(Archive & ar)
    {
      ar(type_, request_vote, append_entries, kv_request, lock_request, add_server_request, remove_server_request);
    }
    Type1 gettype() const
    {
      return type_;
    }
    void settype(Type1 value)
    {
      type_ = value;
    }
    const RequestVote& getrequest_vote() const
    {
      return request_vote;
    }
    RequestVote& getrequest_vote()
    {
      return request_vote;
    }
    const AppendEntries& getappend_entries() const
    {
      return append_entries;
    }
    AppendEntries& getappend_entries()
    {
      return append_entries;
    }
    const KvRequest& getkv_request() const
    {
      return kv_request;
    }
    KvRequest& getkv_request()
    {
      return kv_request;
    }
    const LockRequest& getlock_request() const
    {
      return lock_request;
    }
    LockRequest& getlock_request()
    {
      return lock_request;
    }
    const AddServerRequest& getadd_server_request() const
    {
      return add_server_request;
    }
    AddServerRequest& getadd_server_request()
    {
      return add_server_request;
    }
    const RemoveServerRequest& getremove_server_request() const
    {
      return remove_server_request;
    }
    RemoveServerRequest& getremove_server_request()
    {
      return remove_server_request;
    }
  };
};
