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

#include "launcher/broker_journal.hh"
#include "common/error.hh"

#include <algorithm>
#include <array>
#include <cerrno>

#include <fcntl.h>

#include <limits>
#include <set>

#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

namespace capmgr {

namespace {

using Json = nlohmann::json;
class LinuxOperations final : public JournalOperations {
 public:
  ssize_t Write(int fd, const void* data, size_t size) noexcept override {
    return write(fd, data, size);
  }

  int Sync(int fd) noexcept override { return fsync(fd); }
  int Replace(int directory) noexcept override {
    return renameat(directory, "state.next", directory, "state.json");
  }
};

[[noreturn]] void Invalid() {
  throw Error(ErrorCode::kDatabase,
              "Broker recovery state requires external reconciliation");
}
[[noreturn]] void Io() {
  throw Error(ErrorCode::kIo,
              "Broker recovery storage failed; admission closed");
}

struct Fd {
  int value;
  ~Fd() {
    if (value >= 0) close(value);
  }
};

void File(int fd, uid_t owner) {
  struct stat info{};
  if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != owner ||
      (info.st_mode & 07777) != 0600 || info.st_nlink != 1)
    Invalid();
}

uint64_t Number(const Json& value) {
  if (!value.is_number_unsigned()) Invalid();
  return value.get<uint64_t>();
}

Json Read(int directory, uid_t owner) {
  Fd file{openat(directory, "state.json", O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
  if (file.value < 0) Invalid();
  File(file.value, owner);
  std::array<char, 8193> bytes{};
  size_t total = 0;
  for (;;) {
    ssize_t count =
        read(file.value, bytes.data() + total, bytes.size() - total);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) Io();
    if (count == 0) break;
    total += static_cast<size_t>(count);
    if (total == bytes.size()) Invalid();
  }
  bool duplicate = false;
  std::vector<std::set<std::string>> keys;
  auto callback = [&](int, Json::parse_event_t event, Json& parsed) {
    if (event == Json::parse_event_t::object_start)
      keys.emplace_back();
    else if (event == Json::parse_event_t::key) {
      if (keys.empty() || !keys.back().insert(parsed.get<std::string>()).second)
        duplicate = true;
    } else if (event == Json::parse_event_t::object_end)
      keys.pop_back();
    return true;
  };
  Json result;
  try {
    result = Json::parse(bytes.data(), bytes.data() + total, callback);
  } catch (const Json::exception&) {
    Invalid();
  }

  if (duplicate || !result.is_object() || result.size() != 5 ||
      !result.contains("version") || !result.contains("generation") ||
      !result.contains("next") || !result.contains("state") ||
      !result.contains("jobs") || Number(result["version"]) != 1)
    Invalid();
  return result;
}
}  // namespace

JournalOperations& LinuxJournalOperations() {
  static LinuxOperations operations;
  return operations;
}

BrokerJournal::BrokerJournal(int trusted_directory, uid_t owner,
                             JournalOperations& operations)
    : operations_(operations) {
  directory_ = fcntl(trusted_directory, F_DUPFD_CLOEXEC, 3);
  try {
    struct stat info{};
    if (directory_ < 0 || fstat(directory_, &info) || !S_ISDIR(info.st_mode) ||
        info.st_uid != owner || (info.st_mode & 07777) != 0700)
      Invalid();
    lock_ = openat(directory_, "lock",
                   O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0) Io();
    File(lock_, owner);
    if (flock(lock_, LOCK_EX | LOCK_NB))
      throw Error(ErrorCode::kBusy, "Broker recovery store already owned");
    if (fstatat(directory_, "state.next", &info, AT_SYMLINK_NOFOLLOW) == 0 ||
        errno != ENOENT)
      Invalid();
    auto value = Read(directory_, owner);
    generation_ = Number(value["generation"]);
    next_ = Number(value["next"]);
    if (!value["state"].is_string() || !value["jobs"].is_array() ||
        value["jobs"].size() > 4)
      Invalid();
    state_ = value["state"].get<std::string>();
    if (state_ != "clean" && state_ != "active" && state_ != "uncertain")
      Invalid();
    for (const auto& item : value["jobs"]) {
      auto token = Number(item);
      if (!token || (next_ && token >= next_) ||
          std::find(jobs_.begin(), jobs_.end(), token) != jobs_.end())
        Invalid();
      jobs_.push_back(token);
    }
    if (state_ == "clean" && !jobs_.empty()) Invalid();
    blocked_ =
        state_ != "clean";  // restart never treats a reaped worker as cleanup
  } catch (...) {
    if (lock_ >= 0) close(lock_);
    if (directory_ >= 0) close(directory_);
    lock_ = directory_ = -1;
    throw;
  }
}

BrokerJournal::~BrokerJournal() {
  if (lock_ >= 0) close(lock_);
  if (directory_ >= 0) close(directory_);
}

void BrokerJournal::Persist(const std::string& state, uint64_t generation,
                            uint64_t next, const std::vector<uint64_t>& jobs) {
  // Any failure poisons admission. A partial state.next is preserved as explicit
  // uncertainty; no startup or destructor removes it to guess a clean state.
  blocked_ = true;
  auto bytes = Json{{"version", 1},
                    {"generation", generation},
                    {"next", next},
                    {"state", state},
                    {"jobs", jobs}}
                   .dump() +
               "\n";
  Fd next_file{openat(directory_, "state.next",
                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                      0600)};
  if (next_file.value < 0) Io();
  size_t offset = 0;
  while (offset < bytes.size()) {
    auto count = operations_.Write(next_file.value, bytes.data() + offset,
                                   bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) Io();
    offset += static_cast<size_t>(count);
  }

  if (operations_.Sync(next_file.value) || operations_.Replace(directory_) ||
      operations_.Sync(directory_))
    Io();
  state_ = state;
  generation_ = generation;
  next_ = next;
  jobs_ = jobs;
  blocked_ = state == "uncertain";
}

bool BrokerJournal::Blocked() const {
  std::lock_guard lock(mutex_);
  return blocked_;
}

uint64_t BrokerJournal::Generation() const {
  std::lock_guard lock(mutex_);
  return generation_;
}

std::vector<uint64_t> BrokerJournal::Reservations() const {
  std::lock_guard lock(mutex_);
  return jobs_;
}

void BrokerJournal::RequireActive() const {
  if (blocked_ || state_ != "active")
    throw Error(ErrorCode::kBusy,
                "Broker recovery proof required before admission");
}

uint64_t BrokerJournal::BeginGeneration() {
  std::lock_guard lock(mutex_);
  if (blocked_ || state_ != "clean" ||
      generation_ == std::numeric_limits<uint64_t>::max())
    throw Error(ErrorCode::kBusy, "Broker generation cannot start");
  Persist("active", generation_ + 1, next_, {});
  return generation_;
}

uint64_t BrokerJournal::Reserve() {
  std::lock_guard lock(mutex_);
  RequireActive();
  if (jobs_.size() == 4 || !next_)
    throw Error(ErrorCode::kBusy,
                "Broker durable reservation capacity exhausted");
  auto token = next_;
  auto jobs = jobs_;
  jobs.push_back(token);
  Persist("active", generation_,
          token == std::numeric_limits<uint64_t>::max() ? 0 : token + 1, jobs);
  return token;
}

void BrokerJournal::ConfirmJobGone(uint64_t token) {
  std::lock_guard lock(mutex_);
  RequireActive();
  auto jobs = jobs_;
  auto found = std::find(jobs.begin(), jobs.end(), token);
  if (found == jobs.end())
    throw Error(ErrorCode::kNotFound, "Unknown broker reservation");
  jobs.erase(found);
  Persist("active", generation_, next_, jobs);
}

void BrokerJournal::MarkUncertain() {
  std::lock_guard lock(mutex_);
  if (blocked_) return;
  Persist("uncertain", generation_, next_, jobs_);
}

void BrokerJournal::ConfirmNormalWorkerExit() {
  std::lock_guard lock(mutex_);
  RequireActive();
  if (!jobs_.empty())
    throw Error(ErrorCode::kBusy, "Broker jobs still reserved");
  Persist("clean", generation_, next_, {});
}
}  // namespace capmgr
