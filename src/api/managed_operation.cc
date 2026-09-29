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

#include "api/managed_operation.hh"
#include "common/error.hh"

namespace capmgr {

ManagedOperation::ManagedOperation(ManagedPublicationOperations* operations)
    : operations_(operations),
      confirmed_(std::make_shared<const Publication>(
          Publication{Cleanup::kConfirmedComplete, {}, 1})),
      uncertain_(std::make_shared<const Publication>(
          Publication{Cleanup::kUncertain, {}, 1})),
      terminal_publication_(std::make_shared<Publication>(
          Publication{Cleanup::kConfirmedComplete, {}, 2})),
      publication_(std::make_shared<const Publication>()) {}
ManagedOperation::~ManagedOperation() {
  if (coordinator_.joinable() || joining_) std::terminate();
}

void ManagedOperation::Run() {
  std::lock_guard lock(thread_mutex_);
  if (attempted_ || !ClientToken())
    throw Error(ErrorCode::kConflict,
                "Managed coordinator already started or unbound");
  attempted_ = true;
  coordinator_ = std::thread([this] {
    try {
      Coordinate();
    } catch (...) { /* Retain last publication; never infer absence. */
    }
    cancel_state_.fetch_or(2, std::memory_order_acq_rel);
    try {
      exit_promise_.set_value_at_thread_exit();
    } catch (...) { /* Retain missing exit proof. */
    }
  });
}

ManagedOperation::Snapshot ManagedOperation::PollCleanup() noexcept {
  Snapshot result{publication_.load(std::memory_order_acquire), false};
  std::unique_lock lock(thread_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return result;
  if (attempted_ && !joined_ && !joining_ && coordinator_.joinable() &&
      exited_.wait_for(std::chrono::milliseconds(0)) ==
          std::future_status::ready) {
    joining_ = true;
    auto thread = std::move(coordinator_);
    lock.unlock();
    thread.join();
    lock.lock();
    joined_ = true;
    joining_ = false;
  }
  result.quiescent = joined_;
  // Capture final publication after join, rather than an older pre-exit sample.
  result.publication = publication_.load(std::memory_order_acquire);
  return result;
}

void ManagedOperation::Publish(Cleanup cleanup,
                               std::shared_ptr<const std::string> terminal) {
  auto old = publication_.load(std::memory_order_acquire);
  if (old->cleanup == cleanup && old->terminal == terminal) return;
  if ((old->cleanup == Cleanup::kConfirmedComplete &&
       cleanup != old->cleanup) ||
      old->cleanup == Cleanup::kUncertain || old->terminal ||
      (terminal && cleanup != Cleanup::kConfirmedComplete) ||
      cleanup == Cleanup::kPending)
    throw Error(ErrorCode::kConflict, "Invalid managed cleanup publication");
  if (cleanup == Cleanup::kUncertain) {
    publication_.store(uncertain_, std::memory_order_release);
    return;
  }
  // No allocation follows durable proof until that proof is already visible.
  publication_.store(confirmed_, std::memory_order_release);
  if (!terminal) return;
  if (operations_) operations_->BeforeTerminalPublication();
  terminal_publication_->terminal =
      std::move(terminal);  // single write, BEFORE publication
  publication_.store(terminal_publication_, std::memory_order_release);
}
}  // namespace capmgr
