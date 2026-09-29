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
 * SPDX-License-Identifier: Apache-2.0
 */

#include "platform/remount_ticket.hh"
#include "platform/credential_packet.hh"
#include "common/error.hh"

#include <array>
#include <cerrno>

#include <sys/random.h>

namespace capmgr {

namespace {

std::string RandomTicket() {
  std::array<unsigned char, 32> bytes{};
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0)
      throw Error(ErrorCode::kIo, "Secure ticket entropy unavailable");
    offset += static_cast<size_t>(count);
  }
  const char hex[] = "0123456789abcdef";
  std::string ticket;
  ticket.reserve(64);
  for (auto byte : bytes) {
    ticket += hex[byte >> 4];
    ticket += hex[byte & 15];
  }
  return ticket;
}

void ValidateDestination(const std::string& path) {
  if (path.empty() || path[0] != '/' || path.size() > 4095 ||
      path.find('\0') != std::string::npos || path == "/")
    throw Error(ErrorCode::kInvalid, "Invalid remount destination");
  size_t start = 1;
  while (start < path.size()) {
    auto end = path.find('/', start);
    auto segment = path.substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..")
      throw Error(ErrorCode::kInvalid, "Noncanonical remount destination");
    if (end == std::string::npos) break;
    start = end + 1;
  }

  if (path.back() == '/')
    throw Error(ErrorCode::kInvalid, "Trailing destination separator");
}
}  // namespace

RemountTickets::RemountTickets(Clock clock) : clock_(std::move(clock)) {
  if (!clock_) throw Error(ErrorCode::kInvalid, "Missing ticket clock");
}

void RemountTickets::Prune(Time now) {
  for (auto it = tickets_.begin(); it != tickets_.end();) {
    if (now >= it->second.expires ||
        !it->second.request.principal->HasVerifiedLiveTask())
      it = tickets_.erase(it);
    else
      ++it;
  }
}

std::string RemountTickets::Issue(std::shared_ptr<Peer> peer,
                                  const std::string& destination) {
  ValidateDestination(destination);
  if (!peer) throw Error(ErrorCode::kPermission, "Missing MAIN principal");
  if (!peer->HasVerifiedLiveTask())
    throw Error(ErrorCode::kUnsupported, "Live-task proof API unavailable");
  auto token = RandomTicket();
  std::lock_guard lock(mutex_);
  auto now = clock_();
  Prune(now);
  if (tickets_.size() >= 64)
    throw Error(ErrorCode::kLimit, "Remount ticket capacity exhausted");
  auto [it, inserted] = tickets_.emplace(
      token,
      Ticket{{destination, std::move(peer)}, now + std::chrono::seconds(5)});
  if (!inserted) throw Error(ErrorCode::kIo, "Ticket entropy collision");
  return it->first;
}

RemountRequest RemountTickets::Consume(const Peer& packet_peer) {
  auto token = ReceiveCredentialPacket(packet_peer);
  if (token.size() != 64 ||
      token.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw Error(ErrorCode::kPermission, "Invalid remount ticket");
  std::lock_guard lock(mutex_);
  Prune(clock_());
  auto found = tickets_.find(token);
  if (found == tickets_.end() ||
      !found->second.request.principal->SameCredentials(packet_peer))
    throw Error(ErrorCode::kPermission,
                "Remount ticket does not match live MAIN principal");
  auto request = std::move(found->second.request);
  tickets_.erase(found);
  return request;
}

void RemountTickets::Revoke(const Peer& peer) {
  std::lock_guard lock(mutex_);
  for (auto it = tickets_.begin(); it != tickets_.end();) {
    if (it->second.request.principal.get() == &peer)
      it = tickets_.erase(it);
    else
      ++it;
  }
}
}  // namespace capmgr
