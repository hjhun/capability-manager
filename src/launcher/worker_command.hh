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

#ifndef CAPABILITY_MANAGER_LAUNCHER_WORKER_COMMAND_HH_
#define CAPABILITY_MANAGER_LAUNCHER_WORKER_COMMAND_HH_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <array>

namespace capmgr {

// Private anonymous-pipe coordination for a fixed parent/worker pair, not a
// public protocol/UDS endpoint or authorization mechanism. No path/UID/FD selection
// is carried here. START's body is the original bounded tools/call request.
enum class WorkerCommandKind : uint16_t { Start = 1, Cancel = 2, Status = 3 };
struct WorkerCommand {
  WorkerCommandKind kind;
  uint64_t generation;
  uint64_t sequence;
  uint64_t token;
  std::string request;
};

std::vector<uint8_t> EncodeWorkerCommand(const WorkerCommand& command);
// A separate priority reader accepts only CANCEL. The single-threaded worker
// must poll it and anchored parent liveness before the regular reader/queued START
// and GO. A partial START never consumes bytes from the priority pipe. This class
// does not itself implement scheduling, parent identity, job cleanup or replies.
class WorkerCommandReader {
 public:
  using Clock = std::chrono::steady_clock;
  using ResizeBody = void (*)(std::string&,
                              size_t);  // private allocation-failure test seam
  WorkerCommandReader(int trusted_pipe, uint64_t generation,
                      bool priority_cancel = false, uint64_t first_sequence = 1,
                      ResizeBody resize_body = nullptr);
  ~WorkerCommandReader();
  WorkerCommandReader(const WorkerCommandReader&) = delete;
  WorkerCommandReader& operator=(const WorkerCommandReader&) = delete;
  // At most one bounded nonblocking read (<=8192 bytes). nullopt means incomplete
  // input. EOF/HUP, malformed input or five-second partial-frame deadline poison
  // this channel permanently; EVERY escaping exception does the same, including
  // allocation failure. Caller closes admission and starts all-job cleanup.
  std::optional<WorkerCommand> ReadOne(Clock::time_point now = Clock::now());
  int fd() const noexcept { return fd_; }
  bool Incomplete() const noexcept { return started_.has_value(); }

 private:
  [[noreturn]] void Reject(const char* message);
  std::optional<WorkerCommand> ReadImpl(Clock::time_point now);
  ResizeBody resize_body_;
  int fd_ = -1;
  uint64_t generation_, next_sequence_;
  bool priority_, failed_ = false;
  std::array<uint8_t, 40> header_{};
  size_t header_size_ = 0, body_size_ = 0;
  std::string body_;
  std::optional<Clock::time_point> started_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_WORKER_COMMAND_HH_
