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

#include "../fixtures/read_policy_survivor_publication.hh"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdarg>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>

namespace {

namespace fs = std::filesystem;
using capmgr::fixture::realpolicy::PublishSurvivorRecord;
enum class Fault { kNone, kBeforeWrite, kPartialWrite, kPublication };
std::mutex mutex;
std::condition_variable condition;
std::string pending;
std::atomic<int> owned_fd{-1};
Fault fault = Fault::kNone;
bool paused = false, resumed = false, injected = false;

class Scope {
 public:
  Scope() {
    char path[] = "/tmp/capmgr-survivor-publication-XXXXXX";
    if (!mkdtemp(path)) throw std::runtime_error("test temporary scope");
    root = path;
  }
  ~Scope() { fs::remove_all(root); }
  fs::path root;
};

void Configure(const std::string& path, Fault value) {
  pending = path + ".next";
  owned_fd = -1;
  fault = value;
  paused = resumed = injected = false;
}

std::string Bytes(const fs::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

}  // namespace

extern "C" int __real_open(const char*, int, ...);
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" int __real_renameat2(int, const char*, int, const char*,
                                unsigned int);

// Link interposition belongs only to this ordinary test executable.
extern "C" int __wrap_open(const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list arguments;
    va_start(arguments, flags);
    mode = va_arg(arguments, int);
    va_end(arguments);
  }
  int fd = __real_open(path, flags, mode);
  if (pending == path) owned_fd = fd;
  return fd;
}

extern "C" ssize_t __wrap_write(int fd, const void* bytes, size_t size) {
  if (fd != owned_fd || injected ||
      (fault != Fault::kBeforeWrite && fault != Fault::kPartialWrite))
    return __real_write(fd, bytes, size);
  injected = true;
  ssize_t count = 0;
  if (fault == Fault::kPartialWrite) count = __real_write(fd, bytes, 2);
  {
    std::unique_lock lock(mutex);
    paused = true;
    condition.notify_all();
    condition.wait(lock, [] { return resumed; });
  }
  return fault == Fault::kPartialWrite ? count : __real_write(fd, bytes, size);
}

extern "C" int __wrap_renameat2(int old_dir, const char* old_name, int new_dir,
                                const char* new_name, unsigned int flags) {
  if (fault == Fault::kPublication && pending == old_name) {
    errno = EIO;
    return -1;
  }
  return __real_renameat2(old_dir, old_name, new_dir, new_name, flags);
}

namespace {

TEST(SurvivorPublication,
     PausedEmptyOrPartialPendingRecordIsNeverFinalPublication) {
  for (auto value : {Fault::kBeforeWrite, Fault::kPartialWrite}) {
    Scope scope;
    auto path = scope.root / "ready";
    Configure(path, value);
    const std::string record = "{\"stage\":\"ready\"}\n";
    auto writer = std::async(std::launch::async,
                             [&] { PublishSurvivorRecord(path, record); });
    {
      std::unique_lock lock(mutex);
      EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(3),
                                     [] { return paused; }));
      EXPECT_FALSE(fs::exists(path));
      EXPECT_TRUE(fs::exists(pending));
      if (paused) {
        EXPECT_EQ(Bytes(pending).size(),
                  value == Fault::kBeforeWrite ? 0U : 2U);
      }
      resumed = true;
      condition.notify_all();
    }
    EXPECT_NO_THROW(writer.get());  // Join before any scope cleanup.
    EXPECT_EQ(Bytes(path), record);
    EXPECT_FALSE(fs::exists(pending));
    struct stat info{};
    ASSERT_EQ(lstat(path.c_str(), &info), 0);
    EXPECT_EQ(info.st_nlink, 1U);
    EXPECT_EQ(info.st_mode & 07777, 0600U);
  }
}

TEST(SurvivorPublication, FailedAtomicPublicationRetainsCompletedPendingOnly) {
  Scope scope;
  auto path = scope.root / "body";
  Configure(path, Fault::kPublication);
  EXPECT_THROW(PublishSurvivorRecord(path, "{}\n"), std::runtime_error);
  EXPECT_FALSE(fs::exists(path));
  EXPECT_EQ(Bytes(pending), "{}\n");
  EXPECT_EQ(fcntl(owned_fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST(SurvivorPublication, UnexpectedFinalNameIsNotOverwritten) {
  Scope scope;
  auto path = scope.root / "body";
  Configure(path, Fault::kNone);
  std::ofstream(path) << "original";
  EXPECT_THROW(PublishSurvivorRecord(path, "{}\n"), std::runtime_error);
  EXPECT_EQ(Bytes(path), "original");
  EXPECT_EQ(Bytes(pending), "{}\n");
}

}  // namespace
