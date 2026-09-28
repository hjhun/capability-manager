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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_LEASED_BOOTSTRAP_CHILDREN_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_LEASED_BOOTSTRAP_CHILDREN_HH_

#include "launcher/owned_children.hh"

#include <unistd.h>
#include <array>
#include <chrono>
#include <iostream>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace capmgr::fixture::leasedbootstrap {
// Fixed private fixture records. No operational child admission or recovery API.
// A timeout/failed ownership observation permanently forbids scope deletion.
struct Children {
  // Supervisor calls and OwnedChildren internal retries share this exact seam.
  // A later terminal success cannot erase an earlier syscall failure.
  struct Operations final : ChildOperations {
    ChildOperations& underlying;
    bool& uncertain;
    Operations(ChildOperations& value, bool& latch)
        : underlying(value), uncertain(latch) {}
    int Observe(pid_t pid, ChildExit& result) noexcept override {
      const int error = underlying.Observe(pid, result);
      if (error) uncertain = true;
      return error;
    }
    int Kill(pid_t pid) noexcept override {
      const int error = underlying.Kill(pid);
      if (error) uncertain = true;
      return error;
    }
    int Reap(pid_t pid) noexcept override {
      const int error = underlying.Reap(pid);
      if (error) uncertain = true;
      return error;
    }
  };
  bool uncertain = false;
  Operations operations;
  explicit Children(ChildOperations& underlying = LinuxChildOperations())
      : operations(underlying, uncertain), owned(3, operations) {}
  OwnedChildren owned;
  // One fixed slot for each launch, never overwrite an earlier failed probe.
  std::array<uint64_t, 3> records{};
  void CheckDeadline(std::chrono::steady_clock::time_point deadline,
                     std::chrono::steady_clock::time_point now,
                     const char* reason) {
    if (now >= deadline) {
      uncertain = true;
      throw std::runtime_error(reason);
    }
  }
  void DeadlineFailure(std::string_view reason) {
    // Fixed existing Supervisor/Session messages; include every Session budget
    // even though START/CANCEL are structurally excluded by this fixture.
    for (const auto deadline :
         {"Worker bootstrap deadline", "Frontend worker-close drain deadline",
          "Frontend partial reply deadline", "Frontend CANCEL write deadline",
          "Frontend START write deadline"})
      if (reason == deadline) uncertain = true;
  }
  template <typename Call, typename Now>
  void WithinDeadline(std::chrono::steady_clock::time_point deadline,
                      const char* reason, Call&& call, Now&& now) {
    CheckDeadline(deadline, now(), reason);
    try {
      call();
    } catch (const std::exception& error) {
      DeadlineFailure(error.what());
      if (now() >= deadline) uncertain = true;
      throw;  // Preserve the ORIGINAL exception, not a replacement timeout.
    } catch (...) {
      if (now() >= deadline) uncertain = true;
      throw;
    }
    CheckDeadline(deadline, now(), reason);
  }
  ChildStatus Observe(size_t slot) {
    try {
      const auto status = owned.Inspect(records.at(slot));
      if (status.state == ChildState::Uncertain || status.system_error)
        uncertain = true;
      return status;
    } catch (...) {
      uncertain = true;
      throw;
    }
  }
  ChildStatus Wait(size_t slot, std::chrono::seconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      const auto status = Observe(slot);
      if (status.state == ChildState::Complete) {
        owned.Release(records[slot]);
        records[slot] = 0;
        return status;
      }
      if (uncertain)
        throw std::runtime_error("fixture child ownership uncertain");
      usleep(1000);
    }
    uncertain = true;
    throw std::runtime_error("fixture child wait deadline");
  }
  void CleanupKnown() noexcept {
    for (auto& record : records) {
      if (!record) continue;
      try {
        const auto status = owned.StopAndWait(record, std::chrono::seconds(5));
        if (status.state != ChildState::Complete || status.system_error) {
          uncertain = true;
          continue;
        }
        owned.Release(record);
        record = 0;
      } catch (...) {
        uncertain = true;
      }
    }
  }
  bool Empty() const {
    return !owned.Size() && records == std::array<uint64_t, 3>{};
  }
  bool CleanupEligible() const { return !uncertain && Empty(); }
  ~Children() {
    CleanupKnown();
    if (!Empty()) {
      std::cerr << "RETAINED_LEASED_BOOTSTRAP_SCOPE child ownership unresolved"
                << std::endl;
      std::terminate();
    }
  }
};

// The parent uses this same gate for normal and failure cleanup. Verified later
// absence never restores eligibility lost at an earlier syscall/deadline.
template <typename VerifyAbsence, typename RemoveScope>
bool CleanupScope(Children& children, VerifyAbsence&& verify_absence,
                  RemoveScope&& remove_scope) {
  try {
    verify_absence();
  } catch (...) {
    children.uncertain = true;
    throw;
  }
  if (!children.CleanupEligible()) return false;
  remove_scope();
  return true;
}

}  // namespace capmgr::fixture::leasedbootstrap

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_LEASED_BOOTSTRAP_CHILDREN_HH_
