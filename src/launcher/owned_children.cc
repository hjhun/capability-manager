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

#include "launcher/owned_children.hh"

#include <cerrno>
#include <exception>
#include <limits>

#include <signal.h>

#include <stdexcept>

#include <sys/wait.h>

#include <system_error>
#include <thread>

namespace capmgr {

namespace {

class LinuxOperations final : public ChildOperations {
 public:
  int Observe(pid_t pid, ChildExit& result) noexcept override {
    siginfo_t info{};
    if (waitid(P_PID, static_cast<id_t>(pid), &info,
               WEXITED | WNOHANG | WNOWAIT) < 0)
      return errno;
    result = {};
    if (info.si_pid) {
      result.exited = true;
      if (info.si_code == CLD_EXITED)
        result.code = info.si_status;
      else
        result.signal = info.si_status;
    }
    return 0;
  }

  int Kill(pid_t pid) noexcept override {
    return kill(pid, SIGKILL) < 0 ? errno : 0;
  }

  int Reap(pid_t pid) noexcept override {
    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);
    return result == pid ? 0 : (result < 0 ? errno : EAGAIN);
  }
};

}  // namespace

ChildOperations& LinuxChildOperations() {
  static LinuxOperations instance;
  return instance;
}

OwnedChildren::OwnedChildren(size_t capacity, ChildOperations& operations,
                             uint64_t first_id)
    : capacity_(capacity), operations_(operations), next_id_(first_id) {
  if (!capacity || capacity > jobs_.size() || !first_id)
    throw std::invalid_argument("child table bounds");
}

OwnedChildren::~OwnedChildren() {
  for (const auto& job : jobs_)
    if (job.id && job.status.state != ChildState::Complete) std::terminate();
}

uint64_t OwnedChildren::Reserve() {
  std::lock_guard lock(mutex_);
  Job* free = nullptr;
  for (size_t i = 0; i < capacity_; ++i)
    if (!jobs_[i].id) {
      free = &jobs_[i];
      break;
    }
  if (!free || !next_id_)
    throw std::runtime_error(
        "child admission closed: capacity or ID exhaustion");
  *free = Job{next_id_, -1, {ChildState::Reserved, -1, 0, 0}, true};
  next_id_ =
      next_id_ == std::numeric_limits<uint64_t>::max() ? 0 : next_id_ + 1;
  return free->id;
}

void OwnedChildren::AttachReserved(uint64_t id, pid_t pid) noexcept {
  std::lock_guard lock(mutex_);
  Job* slot = nullptr;
  for (auto& job : jobs_) {
    if (job.id && job.pid == pid && job.status.state != ChildState::Complete)
      std::terminate();
    if (id && job.id == id) slot = &job;
  }

  if (pid <= 0 || !slot || slot->status.state != ChildState::Reserved)
    std::terminate();
  slot->pid = pid;
  slot->status = {};
  slot->no_signal = false;
}

void OwnedChildren::AbandonUnspawned(uint64_t id) {
  std::lock_guard lock(mutex_);
  auto& job = Find(id);
  if (job.status.state != ChildState::Reserved)
    throw std::logic_error("child may exist; reservation cannot be abandoned");
  job = Job{};
}

uint64_t OwnedChildren::Adopt(pid_t pid) {
  std::lock_guard lock(mutex_);
  if (pid <= 0) throw std::invalid_argument("direct child PID");
  Job* free = nullptr;
  for (size_t i = 0; i < capacity_; ++i) {
    auto& job = jobs_[i];
    if (job.id && job.pid == pid && job.status.state != ChildState::Complete)
      throw std::invalid_argument("child already owned");
    if (!job.id) free = &job;
  }

  if (!free || !next_id_)
    throw std::runtime_error(
        "child admission closed: capacity or ID exhaustion");
  ChildExit observed;
  int error = operations_.Observe(pid, observed);
  if (error)
    throw std::system_error(error, std::generic_category(),
                            "adopt direct child");
  // All throwing validation precedes ownership transfer. The fixed table cannot
  // fail allocation after the child becomes ours. Refresh later records exit.
  *free = Job{next_id_, pid, {}, false};
  next_id_ =
      next_id_ == std::numeric_limits<uint64_t>::max() ? 0 : next_id_ + 1;
  return free->id;
}

OwnedChildren::Job& OwnedChildren::Find(uint64_t id) {
  for (auto& job : jobs_)
    if (id && job.id == id) return job;
  throw std::out_of_range("unknown child job");
}

bool OwnedChildren::Refresh(Job& job) {
  if (job.status.state == ChildState::Reserved ||
      job.status.state == ChildState::Complete ||
      job.status.state == ChildState::Uncertain)
    return false;
  if (!job.no_signal) {
    ChildExit observed;
    int error = operations_.Observe(job.pid, observed);
    if (error) {
      job.status.system_error = error;
      if (error == ECHILD || error == ESRCH) {
        // Ownership was violated. Numeric identity is no longer safe to signal.
        job.no_signal = true;
        job.status.state = ChildState::Uncertain;
      }
      return false;
    }
    if (!observed.exited) return true;
    job.no_signal =
        true;  // BEFORE waitpid releases numeric PID, under the same lock.
    job.status = {ChildState::ReapPending, observed.code, observed.signal, 0};
  }

  int error = operations_.Reap(job.pid);
  job.status.system_error = error;
  if (!error)
    job.status.state = ChildState::Complete;
  else if (error == ECHILD || error == ESRCH)
    job.status.state = ChildState::Uncertain;
  return false;
}

ChildStatus OwnedChildren::Inspect(uint64_t id) {
  std::lock_guard lock(mutex_);
  auto& job = Find(id);
  Refresh(job);
  return job.status;
}

ChildStatus OwnedChildren::Stop(uint64_t id) {
  std::lock_guard lock(mutex_);
  auto& job = Find(id);
  bool live = Refresh(job);
  if (!job.no_signal) {
    job.status.state = ChildState::CleanupPending;
    // Do not signal without a successful ownership observation in this call.
    if (live) job.status.system_error = operations_.Kill(job.pid);
  }
  return job.status;
}

ChildStatus OwnedChildren::StopAndWait(uint64_t id,
                                       std::chrono::milliseconds budget) {
  if (budget.count() < 0 || budget > std::chrono::seconds(60))
    throw std::invalid_argument("cleanup wait budget");
  const auto deadline = std::chrono::steady_clock::now() + budget;
  auto status = Stop(id);
  while (status.state != ChildState::Complete &&
         status.state != ChildState::Uncertain &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_until(std::min(
        deadline,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2)));
    status = Stop(
        id);  // Retry transient observe/signal failures within the same budget.
  }
  return status;
}

void OwnedChildren::Release(uint64_t id) {
  std::lock_guard lock(mutex_);
  auto& job = Find(id);
  if (job.status.state != ChildState::Complete)
    throw std::logic_error("cannot release unconfirmed cleanup");
  job = Job{};
}

size_t OwnedChildren::Size() const {
  std::lock_guard lock(mutex_);
  size_t size = 0;
  for (const auto& job : jobs_)
    if (job.id) ++size;
  return size;
}
}  // namespace capmgr
