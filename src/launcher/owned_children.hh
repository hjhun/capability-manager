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

#ifndef CAPABILITY_MANAGER_LAUNCHER_OWNED_CHILDREN_HH_
#define CAPABILITY_MANAGER_LAUNCHER_OWNED_CHILDREN_HH_

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <sys/types.h>

namespace capmgr {

// Private broker state. These are not C API error values.
enum class ChildState {
  Running,
  CleanupPending,
  ReapPending,
  Complete,
  Uncertain,
  Reserved
};

struct ChildStatus {
  ChildState state = ChildState::Running;
  int exit_code = -1;
  int signal = 0;
  int system_error = 0;
};

struct ChildExit {
  bool exited = false;
  int code = -1;
  int signal = 0;
};

// Return zero or a positive errno. Observe MUST use WNOWAIT; Reap MUST be
// nonblocking. Injection is for failure-path tests, not an IPC/plugin surface.
class ChildOperations {
 public:
  virtual ~ChildOperations() = default;
  virtual int Observe(pid_t pid, ChildExit& result) noexcept = 0;
  virtual int Kill(pid_t pid) noexcept = 0;
  virtual int Reap(pid_t pid) noexcept = 0;
};

ChildOperations& LinuxChildOperations();

// Owns ONLY exclusive, unreaped direct children created by the broker's
// long-lived spawning thread. SIGCHLD must not be ignored/SA_NOCLDWAIT; no other
// thread/handler may reap these children. Numeric PIDs from requests are forbidden.
// Namespace creation/PDEATHSIG/authentication are separate prerequisites.
// Capacity includes pending and uncertain cleanup. Completed records must be
// explicitly released; IDs are never reused. Destruction with unreaped children
// is a fail-stop programming error, NOT a background cleanup strategy. The broker
// must keep this table alive and retry cleanup for its entire process lifetime.
class OwnedChildren {
 public:
  explicit OwnedChildren(size_t capacity = 4,
                         ChildOperations& operations = LinuxChildOperations(),
                         uint64_t first_id = 1);
  ~OwnedChildren();
  OwnedChildren(const OwnedChildren&) = delete;
  OwnedChildren& operator=(const OwnedChildren&) = delete;
  // Preferred clone path: reserve capacity/token BEFORE creating a child, then
  // attach the exact successful clone return immediately. Attach cannot allocate,
  // inspect/reap or return a failure which loses the child. Invalid call ordering
  // is a fail-stop programming error. Signals still require a fresh Observe.
  uint64_t Reserve();
  void AttachReserved(uint64_t id, pid_t direct_child) noexcept;
  // Only before clone or after clone returned failure: never abandon a live child.
  void AbandonUnspawned(uint64_t id);
  uint64_t Adopt(
      pid_t direct_child);  // Failure leaves ownership with the caller.
  ChildStatus Inspect(uint64_t id);
  ChildStatus Stop(uint64_t id);
  ChildStatus StopAndWait(uint64_t id, std::chrono::milliseconds budget);
  void Release(
      uint64_t id);  // Complete only; never discard uncertain/pending work.
  size_t Size() const;

 private:
  struct Job {
    uint64_t id = 0;
    pid_t pid = -1;
    ChildStatus status;
    bool no_signal = false;
  };
  Job& Find(uint64_t id);
  bool Refresh(Job& job);  // True only for a live child observed in this call.
  const size_t capacity_;
  ChildOperations& operations_;
  uint64_t next_id_;
  mutable std::mutex mutex_;
  std::array<Job, 64> jobs_{};
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_OWNED_CHILDREN_HH_
