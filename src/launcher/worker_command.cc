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

#include "launcher/worker_command.hh"
#include "launcher/runner.hh"
#include "common/error.hh"

#include <algorithm>
#include <cerrno>

#include <fcntl.h>

#include <limits>

#include <poll.h>

#include <set>

#include <sys/stat.h>
#include <unistd.h>

namespace capmgr {

namespace {

constexpr size_t kMaxBody = 64 * 1024;
uint64_t Get(const uint8_t* bytes, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i)
    value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
  return value;
}

void Put(uint8_t* bytes, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i)
    bytes[i] = static_cast<uint8_t>(value >> (8 * i));
}
[[noreturn]] void Invalid(const char* message) {
  throw Error(ErrorCode::kInvalid, message);
}

void Body(WorkerCommandKind kind, const std::string& body) {
  if (body.size() > kMaxBody) Invalid("Worker command exceeds request limit");
  if (kind != WorkerCommandKind::Start) {
    if ((kind != WorkerCommandKind::Cancel &&
         kind != WorkerCommandKind::Status) ||
        !body.empty())
      Invalid("Invalid worker command kind/body");
    return;
  }
  std::vector<std::set<std::string>> keys;
  auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
    if (depth > 64) Invalid("Worker request nesting limit");
    if (event == Json::parse_event_t::object_start)
      keys.emplace_back();
    else if (event == Json::parse_event_t::object_end)
      keys.pop_back();
    else if (event == Json::parse_event_t::key &&
             !keys.back().insert(value.get<std::string>()).second)
      Invalid("Duplicate worker request key");
    return true;
  };
  try {
    auto parsed = Json::parse(body, callback);
    if (!parsed.is_object()) Invalid("Worker request must be an object");
  } catch (const Json::exception&) {
    Invalid("Malformed worker request");
  }

  auto request = ParseRequest(body);
  if (!request.capability_id.starts_with("cli:") ||
      request.capability_id.size() == 4)
    Invalid("Worker only accepts registered CLI identifiers");
}
}  // namespace

std::vector<uint8_t> EncodeWorkerCommand(const WorkerCommand& command) {
  if (!command.generation || !command.sequence || !command.token)
    Invalid("Zero worker generation/sequence/token");
  Body(command.kind, command.request);
  std::vector<uint8_t> bytes(40 + command.request.size());
  bytes[0] = 'C';
  bytes[1] = 'M';
  bytes[2] = 'W';
  bytes[3] = '1';
  Put(bytes.data() + 4, 1, 2);
  Put(bytes.data() + 6, static_cast<uint16_t>(command.kind), 2);
  Put(bytes.data() + 8, command.generation, 8);
  Put(bytes.data() + 16, command.sequence, 8);
  Put(bytes.data() + 24, command.token, 8);
  Put(bytes.data() + 32, command.request.size(), 4);  // 36..39 reserved zero
  std::copy(command.request.begin(), command.request.end(), bytes.begin() + 40);
  return bytes;
}

WorkerCommandReader::WorkerCommandReader(int pipe, uint64_t generation,
                                         bool priority, uint64_t sequence,
                                         ResizeBody resize_body)
    : resize_body_(resize_body),
      generation_(generation),
      next_sequence_(sequence),
      priority_(priority) {
  if (!generation || !sequence)
    Invalid("Zero worker channel generation/sequence");
  fd_ = fcntl(pipe, F_DUPFD_CLOEXEC, 3);
  struct stat info{};
  int flags = fd_ < 0 ? -1 : fcntl(fd_, F_GETFL);
  if (fd_ < 0 || fstat(fd_, &info) || !S_ISFIFO(info.st_mode) || flags < 0 ||
      (flags & O_ACCMODE) != O_RDONLY ||
      fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    Invalid("Worker channel requires a trusted read pipe");
  }
}

WorkerCommandReader::~WorkerCommandReader() {
  if (fd_ >= 0) close(fd_);
}
[[noreturn]] void WorkerCommandReader::Reject(const char* message) {
  failed_ = true;
  body_.clear();
  Invalid(message);
}

std::optional<WorkerCommand> WorkerCommandReader::ReadOne(
    Clock::time_point now) {
  try {
    return ReadImpl(now);
  } catch (...) {
    failed_ = true;
    body_.clear();
    throw;
  }
}

std::optional<WorkerCommand> WorkerCommandReader::ReadImpl(
    Clock::time_point now) {
  if (failed_) Invalid("Worker channel already failed");
  if (started_ && now - *started_ >= std::chrono::seconds(5))
    Reject("Partial worker command deadline");
  pollfd descriptor{fd_, POLLIN, 0};
  int ready = poll(&descriptor, 1, 0);
  if (ready < 0) {
    if (errno == EINTR) return {};
    Reject("Worker command poll failed");
  }
  // A closed parent endpoint invalidates buffered START too. A leaked writer is
  // a separate anchored-parent-liveness check required in the worker loop.
  if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))
    Reject("Worker control endpoint closed");
  if (!ready) return {};
  bool header = header_size_ < header_.size();
  size_t remaining =
      header ? header_.size() - header_size_ : body_.size() - body_size_;
  void* destination = header ? static_cast<void*>(header_.data() + header_size_)
                             : static_cast<void*>(body_.data() + body_size_);
  ssize_t count = read(fd_, destination, std::min<size_t>(remaining, 8192));
  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return {};
    Reject("Worker command read failed");
  }

  if (!count) Reject("Worker command truncated/closed");
  if (!started_) started_ = now;
  if (header) {
    header_size_ += static_cast<size_t>(count);
    if (header_size_ < header_.size()) return {};
    if (header_[0] != 'C' || header_[1] != 'M' || header_[2] != 'W' ||
        header_[3] != '1' || Get(header_.data() + 4, 2) != 1 ||
        Get(header_.data() + 36, 4) != 0 ||
        Get(header_.data() + 8, 8) != generation_ || !next_sequence_ ||
        Get(header_.data() + 16, 8) != next_sequence_ ||
        !Get(header_.data() + 24, 8))
      Reject("Invalid worker command header");
    auto kind = static_cast<WorkerCommandKind>(Get(header_.data() + 6, 2));
    auto size = Get(header_.data() + 32, 4);
    if (size > kMaxBody || (priority_ && kind != WorkerCommandKind::Cancel) ||
        (!priority_ && kind != WorkerCommandKind::Start &&
         kind != WorkerCommandKind::Status) ||
        (kind == WorkerCommandKind::Start ? size == 0 : size != 0))
      Reject("Invalid worker channel kind/length");
    if (resize_body_)
      resize_body_(body_, static_cast<size_t>(size));
    else
      body_.resize(static_cast<size_t>(size));
    if (body_.size() != size)
      Reject("Worker buffer allocation contract failed");
  } else
    body_size_ += static_cast<size_t>(count);
  if (body_size_ != body_.size()) return {};
  try {
    Body(static_cast<WorkerCommandKind>(Get(header_.data() + 6, 2)), body_);
  } catch (...) {
    failed_ = true;
    body_.clear();
    throw;
  }
  WorkerCommand command{
      static_cast<WorkerCommandKind>(Get(header_.data() + 6, 2)), generation_,
      next_sequence_, Get(header_.data() + 24, 8), std::move(body_)};
  next_sequence_ = next_sequence_ == std::numeric_limits<uint64_t>::max()
                       ? 0
                       : next_sequence_ + 1;
  header_size_ = body_size_ = 0;
  body_.clear();
  started_.reset();
  return command;
}
}  // namespace capmgr
