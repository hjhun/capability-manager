// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "platform/peer.hh"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
namespace capmgr {
struct RemountRequest {
  std::string destination;
  std::shared_ptr<Peer> principal;
};
// Internal correlation only: Issue requires already-authorized TIDL MAIN peer.
// Consume receives/verifies a whole credential packet, matches the same live
// process/namespace and removes a ticket atomically. Caller must recheck Cynara
// and namespace liveness before a separately verified mount transaction.
class RemountTickets {
 public:
  using Time = std::chrono::steady_clock::time_point;
  using Clock = std::function<Time()>;
  explicit RemountTickets(Clock clock = [] {
    return std::chrono::steady_clock::now();
  });
  std::string Issue(std::shared_ptr<Peer> main_peer,
                    const std::string& destination);
  RemountRequest Consume(const Peer& packet_peer);
  void Revoke(const Peer& main_peer);

 private:
  struct Ticket {
    RemountRequest request;
    Time expires;
  };
  void Prune(Time now);
  Clock clock_;
  std::mutex mutex_;
  std::map<std::string, Ticket> tickets_;
};
}
