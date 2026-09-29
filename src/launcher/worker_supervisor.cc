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

#include "launcher/worker_supervisor.hh"
#include "common/error.hh"

#include <cstring>

#include <fcntl.h>

#include <mutex>

#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace capmgr {

namespace {

uint64_t Get(const uint8_t* b, size_t n) {
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) v |= uint64_t{b[i]} << (8 * i);
  return v;
}

void Put(uint8_t* b, uint64_t v, size_t n) {
  for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i));
}
[[noreturn]] void Bad(const char* message) {
  throw Error(ErrorCode::kIo, message);
}
}  // namespace

std::array<uint8_t, 32> EncodeWorkerReady(uint64_t generation,
                                          uint64_t revision) {
  if (!generation || revision > INT64_MAX)
    throw Error(ErrorCode::kInvalid, "Invalid bootstrap generation/revision");
  std::array<uint8_t, 32> b{};
  std::memcpy(b.data(), "CWB1", 4);
  Put(b.data() + 4, 1, 2);
  Put(b.data() + 6, 32, 2);
  Put(b.data() + 8, generation, 8);
  Put(b.data() + 16, revision, 8);
  return b;
}

struct WorkerSupervisor::Impl {
  std::unique_ptr<WorkerSession> session;
  OwnedChildren& children;
  uint64_t worker, revision = 0;
  mutable std::mutex mutex;
  int fd = -1;
  Clock::time_point deadline;
  std::array<uint8_t, 33> bytes{};
  size_t size = 0;
  bool ready = false, stopping = false, failed = false, finished = false;
  Impl(std::unique_ptr<WorkerSession> s, OwnedChildren& c, uint64_t w, int pipe,
       Clock::time_point now)
      : session(std::move(s)),
        children(c),
        worker(w),
        deadline(now + std::chrono::seconds(5)) {
    if (!session || !worker) Bad("Missing startup ownership");
    fd = fcntl(pipe, F_DUPFD_CLOEXEC, 3);
    struct stat st{};
    int flags = fd < 0 ? -1 : fcntl(fd, F_GETFL);
    if (fd < 0 || fstat(fd, &st) || !S_ISFIFO(st.st_mode) || flags < 0 ||
        (flags & O_ACCMODE) != O_RDONLY ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      if (fd >= 0) close(fd);
      fd = -1;
      session->Abort();
      Bad("Invalid bootstrap pipe");
    }
  }

  ~Impl() {
    if (fd >= 0) close(fd);
  }

  void Abort() noexcept {
    if (finished) return;
    failed = true;
    session->Abort();
    if (fd >= 0) close(fd);
    fd = -1;
  }

  void Open() {
    if (failed || finished)
      throw Error(ErrorCode::kBusy, "Worker supervisor closed");
  }

  void Live() {
    auto status = children.Inspect(worker);
    if (status.state != ChildState::Running || status.system_error)
      Bad("Worker not live at admission");
  }

  bool Poll(Clock::time_point now) {
    Open();
    if (ready) return true;
    try {
      if (now >= deadline) Bad("Worker bootstrap deadline");
      pollfd p{fd, POLLIN, 0};
      int n = poll(&p, 1, 0);
      if (n < 0) {
        if (errno == EINTR) return false;
        Bad("Worker bootstrap poll failed");
      }
      if (p.revents & (POLLERR | POLLNVAL)) Bad("Worker bootstrap pipe failed");
      // HUP can accompany the one final READY. Drain it before checking EOF.
      if (n == 0) {
        Live();
        return false;
      }
      ssize_t count = read(fd, bytes.data() + size, bytes.size() - size);
      if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
          return false;
        Bad("Worker bootstrap read failed");
      }
      if (count > 0) {
        size += static_cast<size_t>(count);
        if (size > 32) Bad("Extra worker bootstrap bytes");
        Live();
        return false;
      }
      if (size != 32 || std::memcmp(bytes.data(), "CWB1", 4) ||
          Get(bytes.data() + 4, 2) != 1 || Get(bytes.data() + 6, 2) != 32 ||
          Get(bytes.data() + 8, 8) != session->Generation() ||
          Get(bytes.data() + 16, 8) > INT64_MAX || Get(bytes.data() + 24, 8))
        Bad("Invalid worker READY");
      Live();
      revision = Get(bytes.data() + 16, 8);
      ready = true;
      close(fd);
      fd = -1;
      return true;
    } catch (...) {
      Abort();
      throw;
    }
  }
};

WorkerSupervisor::WorkerSupervisor(std::unique_ptr<WorkerSession> s,
                                   OwnedChildren& c, uint64_t worker, int fd,
                                   Clock::time_point now)
    : impl_(std::make_unique<Impl>(std::move(s), c, worker, fd, now)) {}
WorkerSupervisor::~WorkerSupervisor() { impl_->Abort(); }
bool WorkerSupervisor::PollStartup(Clock::time_point now) {
  std::lock_guard lock(impl_->mutex);
  return impl_->Poll(now);
}

uint64_t WorkerSupervisor::CatalogRevision() const {
  std::lock_guard lock(impl_->mutex);
  impl_->Open();
  if (!impl_->ready) throw Error(ErrorCode::kBusy, "Worker not READY");
  return impl_->revision;
}

uint64_t WorkerSupervisor::Start(const std::string& request) {
  auto& s = *impl_;
  std::lock_guard lock(s.mutex);
  s.Open();
  if (!s.ready || s.stopping)
    throw Error(ErrorCode::kBusy, "Worker admission not READY");
  try {
    s.Live();
    return s.session->Start(request);
  } catch (...) {
    // Invalid input/capacity before session admission is recoverable; worker
    // liveness or a poisoned session is not. Recheck without losing exceptions.
    try {
      s.Live();
      if (s.session->Failed()) s.Abort();
    } catch (...) {
      s.Abort();
    }
    throw;
  }
}

void WorkerSupervisor::Cancel(uint64_t token) {
  auto& s = *impl_;
  std::lock_guard lock(s.mutex);
  s.Open();
  try {
    s.session->Cancel(token);
  } catch (...) {
    if (s.session->Failed()) s.Abort();
    throw;
  }
}

std::optional<WorkerEvent> WorkerSupervisor::Step(Clock::time_point now) {
  auto& s = *impl_;
  std::lock_guard lock(s.mutex);
  s.Open();
  if (!s.ready) throw Error(ErrorCode::kBusy, "Worker not READY");
  // Do not discard buffered Complete just because the worker has already exited.
  try {
    return s.session->Step(now);
  } catch (...) {
    s.Abort();
    throw;
  }
}

void WorkerSupervisor::PrepareStop() {
  auto& s = *impl_;
  std::lock_guard lock(s.mutex);
  s.Open();
  s.session->PrepareStop();
  s.stopping = true;
}

bool WorkerSupervisor::ConfirmNormalExit() {
  auto& s = *impl_;
  std::lock_guard lock(s.mutex);
  s.Open();
  try {
    auto status = s.children.Inspect(s.worker);
    if (status.state == ChildState::Running ||
        status.state == ChildState::ReapPending)
      return false;
    if (!s.ready || status.state != ChildState::Complete ||
        status.exit_code != 0 || status.signal)
      Bad("Abnormal worker exit");
    s.session->ConfirmNormalExit();
    s.children.Release(s.worker);
    s.finished = true;
    return true;
  } catch (...) {
    s.Abort();
    throw;
  }
}

void WorkerSupervisor::Abort() noexcept {
  std::lock_guard lock(impl_->mutex);
  impl_->Abort();
}

bool WorkerSupervisor::Failed() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->failed;
}
}  // namespace capmgr
