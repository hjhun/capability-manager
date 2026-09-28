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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_BOOTSTRAP_POLICY_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_BOOTSTRAP_POLICY_HH_

#include <cerrno>
#include <stdexcept>
#include <grp.h>
#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
namespace capmgr::fixture {
// SYS_PTRACE is fixture-only namespace introspection of the full-cap root
// parent/PID1. This is not a production capability grant or minimal policy.
inline constexpr uint64_t kCaps =
    (1ULL << CAP_KILL) | (1ULL << CAP_SETGID) | (1ULL << CAP_SETUID) |
    (1ULL << CAP_SETPCAP) | (1ULL << CAP_SYS_ADMIN) | (1ULL << CAP_SYS_PTRACE) |
    (1ULL << CAP_MAC_ADMIN);
inline void Require(bool ok) {
  if (!ok) throw std::runtime_error("fixture privilege setup");
}
inline void Reduce(bool no_new_privs = true) {
  Require(geteuid() == 0 && setgroups(0, nullptr) == 0 && chdir("/") == 0);
  for (int cap = 0; cap < 64; ++cap) {
    int present = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (present < 0 && errno == EINVAL) break;
    Require(present >= 0);
    if (!(kCaps & (1ULL << cap)))
      Require(!prctl(PR_CAPBSET_DROP, cap, 0, 0, 0));
  }
  Require(!prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0));
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  __user_cap_data_struct caps[2]{};
  for (int i = 0; i < 2; ++i)
    caps[i].permitted = caps[i].effective =
        static_cast<uint32_t>(kCaps >> (32 * i));
  Require(!syscall(SYS_capset, &header, caps));
  if (no_new_privs) Require(!prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
}
}

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_BOOTSTRAP_POLICY_HH_
