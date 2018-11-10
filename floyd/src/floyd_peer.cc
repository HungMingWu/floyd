// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "floyd/src/floyd_peer.h"

#include <boost/asio/ts/executor.hpp>

#include <algorithm>
#include <climits>
#include <vector>
#include <string>

#include "slash/include/env.h"
#include "slash/include/xdebug.h"

#include "floyd/src/floyd_primary.h"
#include "floyd/src/floyd_context.h"
#include "floyd/src/floyd_client_pool.h"
#include "floyd/src/raft_log.h"
#include "floyd/src/logger.h"
#include "floyd/src/raft_meta.h"
#include "floyd/src/floyd_apply.h"
#include "floyd/src/floyd_ds.h"

namespace floyd {

Peer::Peer(boost::asio::io_context& ctx_, std::string server, PeersSet& peers, FloydContext& context, FloydPrimary& primary, RaftMeta& raft_meta,
    RaftLog& raft_log, ClientPool &pool, FloydApply& apply, const Options& options, Logger* info_log)
  : ctx(ctx_),
    peer_addr_(server),
    peers_(peers),
    context_(context),
    primary_(primary),
    raft_meta_(raft_meta),
    raft_log_(raft_log),
    pool_(pool),
    apply_(apply),
    options_(options),
    info_log_(info_log),
    next_index_(1),
    match_index_(0),
    peer_last_op_time(0) {
      next_index_ = raft_log_.GetLastLogIndex() + 1;
      match_index_ = raft_meta_.GetLastApplied();
}

bool Peer::CheckAndVote(uint64_t vote_term) {
  return (++context_.vote_quorum) > (options_.members.size() / 2);
}

void Peer::UpdatePeerInfo() {
  for (auto& pt : peers_) {
    pt.second->set_next_index(raft_log_.GetLastLogIndex() + 1);
    pt.second->set_match_index(0);
  }
}

void Peer::AddRequestVoteTask() {
  boost::asio::post(ctx, [this] { RequestVoteRPC(); });
}

void Peer::RequestVoteRPC() {
  uint64_t last_log_term;
  uint64_t last_log_index;
  CmdRequest req;
  {
  std::lock_guard l(context_.global_mu);
  raft_log_.GetLastLogTermAndIndex(&last_log_term, &last_log_index);

  req.type = Type::kRequestVote;
  auto &request_vote = req.request_vote;
  request_vote.ip = options_.local_ip;
  request_vote.port = options_.local_port;
  request_vote.term = context_.current_term;
  request_vote.last_log_term = last_log_term;
  request_vote.last_log_index = last_log_index;
  LOGV(INFO_LEVEL, info_log_, "Peer::RequestVoteRPC server %s:%d Send RequestVoteRPC message to %s at term %d",
      options_.local_ip.c_str(), options_.local_port, peer_addr_.c_str(), context_.current_term);
  }

  CmdResponse res;
  std::error_code result = pool_.SendAndRecv(peer_addr_, req, &res);

  if (!result) {
    LOGV(DEBUG_LEVEL, info_log_, "Peer::RequestVoteRPC: RequestVote to %s failed %s",
         peer_addr_.c_str(), result.message().c_str());
    return;
  }

  {
  std::lock_guard l(context_.global_mu);
  if (!result) {
    LOGV(WARN_LEVEL, info_log_, "Peer::RequestVoteRPC: Candidate %s:%d SendAndRecv to %s failed %s",
         options_.local_ip.c_str(), options_.local_port, peer_addr_.c_str(), result.message().c_str());
    return;
  }
  if (res.request_vote_res.term > context_.current_term) {
    // RequestVote fail, maybe opposite has larger term, or opposite has
    // longer log. if opposite has larger term, this node will become follower
    // otherwise we will do nothing
    LOGV(INFO_LEVEL, info_log_, "Peer::RequestVoteRPC: Become Follower, Candidate %s:%d vote request denied by %s,"
        " request_vote_res.term()=%lu, current_term=%lu", options_.local_ip.c_str(), options_.local_port,
        peer_addr_.c_str(), res.request_vote_res.term, context_.current_term);
    context_.BecomeFollower(res.request_vote_res.term);
    context_.voted_for_ip.clear();
    context_.voted_for_port = 0;
    raft_meta_.SetCurrentTerm(context_.current_term);
    raft_meta_.SetVotedForIp(context_.voted_for_ip);
    raft_meta_.SetVotedForPort(context_.voted_for_port);
    return;
  } else if (res.request_vote_res.term < context_.current_term) {
    // Ingore old term rsp
    return;
  }
  if (context_.role == Role::kCandidate) {
    // kOk means RequestVote success, opposite vote for me
    if (res.request_vote_res.vote_granted == true) {    // granted
      LOGV(INFO_LEVEL, info_log_, "Peer::RequestVoteRPC: Candidate %s:%d get vote from node %s at term %d",
          options_.local_ip.c_str(), options_.local_port, peer_addr_.c_str(), context_.current_term);
      // However, we need check whether this vote is vote for old term
      // we need ignore these type of vote
      if (CheckAndVote(res.request_vote_res.term)) {
        context_.BecomeLeader();
        UpdatePeerInfo();
        LOGV(INFO_LEVEL, info_log_, "Peer::RequestVoteRPC: %s:%d become leader at term %d",
            options_.local_ip.c_str(), options_.local_port, context_.current_term);
        primary_.AddTask(kHeartBeat, false);
      }
    } else { 
      LOGV(INFO_LEVEL, info_log_, "Peer::RequestVoteRPC: Candidate %s:%d deny vote from node %s at term %d, "  
          "transfer from candidate to follower", 
          options_.local_ip.c_str(), options_.local_port, peer_addr_.c_str(), context_.current_term);
      context_.BecomeFollower(res.request_vote_res.term);
      context_.voted_for_ip.clear();
      context_.voted_for_port = 0;
      raft_meta_.SetCurrentTerm(context_.current_term);
      raft_meta_.SetVotedForIp(context_.voted_for_ip);
      raft_meta_.SetVotedForPort(context_.voted_for_port);
    }
  } else if (context_.role == Role::kFollower) {
    LOGV(INFO_LEVEL, info_log_, "Peer::RequestVotePPC: Server %s:%d have transformed to follower when doing RequestVoteRPC, " 
        "The leader is %s:%d, new term is %lu", options_.local_ip.c_str(), options_.local_port, context_.leader_ip.c_str(),
        context_.leader_port, context_.current_term);
  } else if (context_.role == Role::kLeader) {
    LOGV(INFO_LEVEL, info_log_, "Peer::RequestVotePPC: Server %s:%d is already a leader at term %lu, " 
        "get vote from node %s at term %d", 
        options_.local_ip.c_str(), options_.local_port, context_.current_term,
        peer_addr_.c_str(), res.request_vote_res.term);
  }
  }
  return;
}

uint64_t Peer::QuorumMatchIndex() {
  std::vector<uint64_t> values;
  for (auto iter = peers_.begin(); iter != peers_.end(); iter++) {
    if (iter->first == peer_addr_) {
      values.push_back(match_index_);
      continue;
    }
    values.push_back(iter->second->match_index());
  }
  LOGV(DEBUG_LEVEL, info_log_, "Peer::QuorumMatchIndex: Get peers match_index %d %d %d %d",
      values[0], values[1], values[2], values[3]);
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2);
}

// only leader will call AdvanceCommitIndex
// follower only need set commit as leader's
void Peer::AdvanceLeaderCommitIndex() {
  uint64_t new_commit_index = QuorumMatchIndex();
  if (context_.commit_index < new_commit_index) {
    context_.commit_index = new_commit_index;
    raft_meta_.SetCommitIndex(context_.commit_index);
  }
  return;
}

void Peer::AddAppendEntriesTask() {
  boost::asio::post(ctx, [this] { AppendEntriesRPC(); });
}

void Peer::AppendEntriesRPC() {
  uint64_t prev_log_index = 0;
  uint64_t num_entries = 0;
  uint64_t prev_log_term = 0;
  uint64_t last_log_index = 0;
  uint64_t current_term = 0;
  CmdRequest req;
  auto &append_entries = req.append_entries;
  {
  std::lock_guard l(context_.global_mu);
  prev_log_index = next_index_ - 1;
  last_log_index = raft_log_.GetLastLogIndex();
  /*
   * LOGV(INFO_LEVEL, info_log_, "Peer::AppendEntriesRPC: next_index_ %d last_log_index %d peer_last_op_time %lu nowmicros %lu",
   *     next_index_.load(), last_log_index, peer_last_op_time, slash::NowMicros());
   */
  if (next_index_ > last_log_index && peer_last_op_time + options_.heartbeat_us > slash::NowMicros()) {
    return;
  }
  peer_last_op_time = slash::NowMicros();

  if (prev_log_index != 0) {
    if (auto entry = raft_log_.GetEntry(prev_log_index); !entry) {
      LOGV(WARN_LEVEL, info_log_, "Peer::AppendEntriesRPC: Get my(%s:%d) Entry index %llu "
          "not found", options_.local_ip.c_str(), options_.local_port, prev_log_index);
    } else {
      prev_log_term = entry->term;
    }
  }
  current_term = context_.current_term;

  req.type = Type::kAppendEntries;
  append_entries.ip = options_.local_ip;
  append_entries.port = options_.local_port;
  append_entries.term = current_term;
  append_entries.prev_log_index = prev_log_index;
  append_entries.prev_log_term = prev_log_term;
  append_entries.leader_commit = context_.commit_index;
  }

  for (uint64_t index = next_index_; index <= last_log_index; index++) {
    if (auto tmp_entry = raft_log_.GetEntry(index); tmp_entry) {
      append_entries.entries.push_back(tmp_entry.value());
    } else {
      LOGV(WARN_LEVEL, info_log_, "Peer::AppendEntriesRPC: peer_addr %s can't get Entry "
          "from raft_log, index %lld", peer_addr_.c_str(), index);
      break;
    }

    num_entries++;
    if (num_entries >= options_.append_entries_count_once) {
#if 0
        || (uint64_t)append_entries->ByteSize() >= options_.append_entries_size_once) {
#endif
      break;
    }
  }
  LOGV(DEBUG_LEVEL, info_log_, "Peer::AppendEntriesRPC: peer_addr(%s)'s next_index_ %llu, my last_log_index %llu"
      " AppendEntriesRPC will send %d iterm", peer_addr_.c_str(), next_index_.load(), last_log_index, num_entries);
  // if the AppendEntries don't contain any log item
  if (num_entries == 0) {
    LOGV(INFO_LEVEL, info_log_, "Peer::AppendEntryRpc server %s:%d Send pingpong appendEntries message to %s at term %d",
        options_.local_ip.c_str(), options_.local_port, peer_addr_.c_str(), current_term);
  }

  CmdResponse res;
  std::error_code result = pool_.SendAndRecv(peer_addr_, req, &res);

  {
  std::lock_guard l(context_.global_mu);
  if (!result) {
    LOGV(WARN_LEVEL, info_log_, "Peer::AppendEntries: Leader %s:%d SendAndRecv to %s failed, result is %s\n",
         options_.local_ip.c_str(), options_.local_port, peer_addr_.c_str(), result.message().c_str());
    return;
  }

  // here we may get a larger term, and transfer to follower
  if (res.append_entries_res.term > context_.current_term) {
    LOGV(INFO_LEVEL, info_log_, "Peer::AppendEntriesRPC: %s:%d Transfer from Leader to Follower since get A larger term"
        "from peer %s, local term is %d, peer term is %d", options_.local_ip.c_str(), options_.local_port,
        peer_addr_.c_str(), context_.current_term, res.append_entries_res.term);
    context_.BecomeFollower(res.append_entries_res.term);
    context_.voted_for_ip.clear();
    context_.voted_for_port = 0;
    raft_meta_.SetCurrentTerm(context_.current_term);
    raft_meta_.SetVotedForIp(context_.voted_for_ip);
    raft_meta_.SetVotedForPort(context_.voted_for_port);
    return;
  } else if (res.append_entries_res.term < context_.current_term) {
    // Ignore old term msg
    return;
  }

  // so we need to judge the role here
  if (context_.role == Role::kLeader) {
    /*
     * receiver has higer term than myself, so turn from candidate to follower
     */
    if (res.append_entries_res.success == true) {
      if (num_entries > 0) {
        match_index_ = prev_log_index + num_entries;
        // only log entries from the leader's current term are committed
        // by counting replicas
        if (append_entries.entries[num_entries - 1].term == context_.current_term) {
          AdvanceLeaderCommitIndex();
          apply_.ScheduleApply();
        }
        next_index_ = prev_log_index + num_entries + 1;
      }
    } else {
      LOGV(INFO_LEVEL, info_log_, "Peer::AppEntriesRPC: peer_addr %s Send AppEntriesRPC failed,"
          "peer's last_log_index %lu, peer's next_index_ %lu",
          peer_addr_.c_str(), res.append_entries_res.last_log_index, next_index_.load());
      uint64_t adjust_index = std::min(res.append_entries_res.last_log_index + 1,
                                       next_index_ - 1);
      if (adjust_index > 0) {
        // Prev log don't match, so we retry with more prev one according to
        // response
        next_index_ = adjust_index;
        LOGV(INFO_LEVEL, info_log_, "Peer::AppEntriesRPC: peer_addr %s Adjust peer next_index_, Now next_index_ is %lu",
            peer_addr_.c_str(), next_index_.load());
        AddAppendEntriesTask();
      }
    }
  } else if (context_.role == Role::kFollower) {
    LOGV(INFO_LEVEL, info_log_, "Peer::AppEntriesRPC: Server %s:%d have transformed to follower when doing AppEntriesRPC, "
        "new leader is %s:%d, new term is %lu", options_.local_ip.c_str(), options_.local_port, context_.leader_ip.c_str(),
        context_.leader_port, context_.current_term);
  } else if (context_.role == Role::kCandidate) {
    LOGV(INFO_LEVEL, info_log_, "Peer::AppEntriesRPC: Server %s:%d have transformed to candidate when doing AppEntriesRPC, "
        "new term is %lu", options_.local_ip.c_str(), options_.local_port, context_.current_term);
  }
  }
  return;
}

}  // namespace floyd
