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

#ifndef CAPABILITY_MANAGER_LAUNCHER_NAMESPACE_INIT_HH_
#define CAPABILITY_MANAGER_LAUNCHER_NAMESPACE_INIT_HH_

#include <cstdint>

#include <sys/types.h>

namespace capmgr {

// Single-threaded trusted broker ONLY (not arbitrary multithreaded raw clone).
// Trusted in-process configuration prepared BEFORE clone. Never deserialize this
// from IPC. Future broker must resolve catalog, policy, app_fw identity and label.
// clone flags MUST be CLONE_NEWPID|CLONE_NEWNS|SIGCHLD (no CLONE_VM/NEWUSER).
// All descriptors are distinct, CLOEXEC and >=3; stdin becomes /dev/null.
// Broker opens its own /proc/self and /proc/self/ns/mnt before clone, on an
// image-trusted procfs. The first pins the creating process object, independently
// of a retained GO writer; the second proves mount isolation before any mount.
// Only the creator normally owns the GO writer. A delegated writer must not keep
// a dead creator authorized. No caller-provided proc/ns FD is accepted here.
struct NamespaceInitConfig {
  uid_t uid;
  gid_t gid;
  const char* smack_label;
  const char* executable;
  const char* request;
  int control_read;
  int status_write;
  int stdout_write;
  int stderr_write;
  int parent_process;
  int parent_mount_namespace;
};

enum class InitMessageKind : uint32_t {
  Ready = 1,
  Started = 2,
  Exited = 3,
  Failed = 4
};

enum class InitStage : uint32_t {
  Context = 1,
  Signals,
  Mount,
  Descriptors,
  Label,
  Bounding,
  Credentials,
  Capabilities,
  Parent,
  Go,
  Fork,
  Exec,
  Wait
};

struct InitMessage {
  InitMessageKind kind;
  InitStage stage;
  int32_t error;
  int32_t code;
  int32_t signal;
};

static_assert(sizeof(InitMessage) ==
              20);  // Fixed pipe record on 32/64-bit targets.
// Non-exec namespace PID1. Uses no heap allocation, C++ runtime locks, or logging
// after clone. Returns only on failure or after main workload exit; the kernel
// then kills ALL remaining namespace members, including setsid descendants.
// Parent owns direct child via OwnedChildren; this function does not provide a
// broker service, cgroup limits, catalog authorization, or hard teardown bound.
int NamespaceInit(void* configuration) noexcept;
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_NAMESPACE_INIT_HH_
