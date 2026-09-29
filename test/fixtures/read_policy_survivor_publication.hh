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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SURVIVOR_PUBLICATION_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SURVIVOR_PUBLICATION_HH_

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <string>
#include <string_view>

#include "read_policy_survivor_hold.hh"

namespace capmgr::fixture::realpolicy {

// Fixed trusted fixture paths only. The final name is the publication boundary:
// partial/failed writes remain at .next, never accepted as a completed record.
// Deadline checks classify elapsed IO; they cannot preempt arbitrary kernel IO.
inline void PublishSurvivorRecord(const std::string& path,
                                  std::string_view bytes) {
  if (bytes.empty() || bytes.size() > 4096 || bytes.back() != '\n')
    throw std::runtime_error("bounded survivor record");
  const auto end = SurvivorClock::now() + std::chrono::seconds(1);
  const auto check_time = [&] {
    if (SurvivorClock::now() >= end)
      throw std::runtime_error("survivor publication deadline");
  };
  const auto pending = path + ".next";
  int fd = open(pending.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) throw std::runtime_error("survivor pending record creation");
  try {
    size_t offset = 0;
    while (offset != bytes.size()) {
      check_time();
      const auto count =
          write(fd, bytes.data() + offset, bytes.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) throw std::runtime_error("survivor pending record write");
      offset += static_cast<size_t>(count);
      check_time();
    }
    if (fchmod(fd, 0600) || fsync(fd))
      throw std::runtime_error("survivor pending record persistence");
    check_time();
    const int owned = fd;
    fd = -1;
    if (close(owned)) throw std::runtime_error("survivor pending record close");
    check_time();
    // NOREPLACE rejects an unexpected old final name without overwriting it.
    // Atomic rename keeps single-link validation valid at the publication edge.
    if (renameat2(AT_FDCWD, pending.c_str(), AT_FDCWD, path.c_str(),
                  RENAME_NOREPLACE))
      throw std::runtime_error("survivor atomic record publication");
    check_time();
  } catch (...) {
    if (fd >= 0) close(fd);
    throw;  // Retain .next/final evidence; no speculative unlink or success.
  }
}

}  // namespace capmgr::fixture::realpolicy

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SURVIVOR_PUBLICATION_HH_
