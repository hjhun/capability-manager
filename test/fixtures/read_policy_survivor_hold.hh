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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SURVIVOR_HOLD_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SURVIVOR_HOLD_HH_

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <string>

namespace capmgr::fixture::realpolicy {

using SurvivorClock = std::chrono::steady_clock;
inline bool ReferenceModuleTopology(std::string_view root,
                                    std::string_view endpoint, pid_t parent) {
  constexpr std::string_view prefix = "/opt/usr/capmgr-reference-survivor-";
  if (parent <= 0 || !root.starts_with(prefix) ||
      root.size() != prefix.size() + 6)
    return false;
  for (const auto c : root.substr(prefix.size()))
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9')))
      return false;
  return endpoint == "d::org.capmgr.referencesurvivor." +
                         std::to_string(parent) + ".system301-platform";
}
enum class SurvivorDrain { kContinue, kComplete, kFail };

// A stop request precedes the reply/disconnect and is not a drain observation.
inline SurvivorDrain SurvivorDrainDecision(bool hold, bool stop, bool empty,
                                           bool zero, bool error,
                                           bool expired) {
  if (error || (hold && !zero)) return SurvivorDrain::kFail;
  if (expired)
    return hold && empty ? SurvivorDrain::kComplete : SurvivorDrain::kFail;
  if (!hold && stop && empty) return SurvivorDrain::kComplete;
  return SurvivorDrain::kContinue;
}

// One sampled now; no negative poll timeout, including after descheduling/EINTR.
inline int SurvivorPollMillis(SurvivorClock::time_point end,
                              SurvivorClock::time_point now) {
  if (now >= end) return 0;
  const auto millis = std::chrono::ceil<std::chrono::milliseconds>(end - now);
  return static_cast<int>(std::clamp<int64_t>(millis.count(), 1, 20000));
}
inline void SurvivorHold(SurvivorClock::time_point end) {
  for (;;) {
    const int millis = SurvivorPollMillis(end, SurvivorClock::now());
    if (!millis) return;
    if (poll(nullptr, 0, millis) < 0 && errno != EINTR)
      throw std::runtime_error("survivor hold poll");
  }
}
inline void SurvivorEvidence(std::string_view bytes) {
  const int flags = fcntl(1, F_GETFL);
  if (flags < 0 || !(flags & O_NONBLOCK) || bytes.empty() ||
      bytes.size() > 4096 || bytes.back() != '\n')
    throw std::runtime_error("survivor bounded nonblocking evidence");
  const auto end = SurvivorClock::now() + std::chrono::seconds(1);
  size_t offset = 0;
  while (offset != bytes.size()) {
    const auto millis = SurvivorPollMillis(end, SurvivorClock::now());
    if (!millis) throw std::runtime_error("survivor evidence deadline");
    pollfd ready{1, POLLOUT, 0};
    const int result = poll(&ready, 1, millis);
    if (result < 0 && errno == EINTR) continue;
    if (result <= 0 || ready.revents != POLLOUT || SurvivorClock::now() >= end)
      throw std::runtime_error("survivor evidence readiness");
    const auto count = write(1, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (count <= 0) throw std::runtime_error("survivor evidence write");
    offset += static_cast<size_t>(count);
    if (SurvivorClock::now() >= end)
      throw std::runtime_error("survivor evidence completion deadline");
  }
}
}  // namespace capmgr::fixture::realpolicy

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SURVIVOR_HOLD_HH_
