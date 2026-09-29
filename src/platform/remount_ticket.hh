// SPDX-License-Identifier: Apache-2.0
/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CAPABILITY_MANAGER_PLATFORM_REMOUNT_TICKET_HH_
#define CAPABILITY_MANAGER_PLATFORM_REMOUNT_TICKET_HH_

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

// Issue/Consume are disabled with NOT_SUPPORTED without a verified live-task API.
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

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_REMOUNT_TICKET_HH_
