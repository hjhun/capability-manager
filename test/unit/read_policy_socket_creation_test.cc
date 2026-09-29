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

#include "../fixtures/read_policy_socket_creation.hh"
#include "../fixtures/read_policy_route_observation.hh"

#include <gtest/gtest.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace capmgr::fixture::realpolicy {
namespace {

void Require(bool value) {
  if (!value) throw std::runtime_error("isolated socket creation assertion");
}

void ExclusiveBoundary() {
  struct sigaction action{};
  Require(sigaction(SIGCHLD, nullptr, &action) == 0 &&
          action.sa_handler == SIG_DFL && !(action.sa_flags & SA_NOCLDWAIT));
  DIR* tasks = opendir("/proc/self/task");
  Require(tasks != nullptr);
  unsigned count = 0;
  errno = 0;
  while (const auto* entry = readdir(tasks)) {
    if (entry->d_name[0] != '.') ++count;
  }
  const int error = errno;
  Require(closedir(tasks) == 0 && !error && count == 1);
  siginfo_t status{};
  errno = 0;
  Require(waitid(P_ALL, 0, &status, WEXITED | WNOHANG | WNOWAIT) == -1 &&
          errno == ECHILD);
}

// No retry can hide an unknown wait/timeout. The caller retains its scratch
// directory on any exception. Never reap an arbitrary P_ALL observation.
struct ReapOperations {
  auto Now() { return std::chrono::steady_clock::now(); }
  pid_t Wait(pid_t pid, int* status) { return waitpid(pid, status, WNOHANG); }
  void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
};

// Test-TU-only clock/wait seam. Elapsed budget rejects even an actual late reap;
// the caller retains its scope and never re-waits/signals that completed PID.
template <class Operations>
void ExactReap(pid_t pid, Operations& operations) {
  const auto end = operations.Now() + std::chrono::seconds(3);
  for (;;) {
    Require(operations.Now() < end);
    int status = 0;
    const auto found = operations.Wait(pid, &status);
    Require(operations.Now() < end);
    Require(found >= 0);
    if (found == pid) {
      Require(WIFEXITED(status) && WEXITSTATUS(status) == 0);
      return;
    }
    operations.Pause();
  }
}

void ExactReap(pid_t pid) {
  ReapOperations operations;
  ExactReap(pid, operations);
}

struct FakeReapOperations {
  enum class Mode { kOnTime, kLateBeforeRetry, kLatePositive };
  Mode mode;
  std::chrono::steady_clock::time_point now{};
  unsigned attempts = 0;

  auto Now() { return now; }
  pid_t Wait(pid_t pid, int* status) {
    ++attempts;
    *status = 0;
    if (mode == Mode::kLateBeforeRetry) return 0;
    if (mode == Mode::kLatePositive) now += std::chrono::seconds(4);
    return pid;
  }
  void Pause() { now += std::chrono::seconds(4); }
};

TEST(ReferenceSocketReapBudget, ExpiryBeforeRetryNeverReachesRemoval) {
  FakeReapOperations operations{FakeReapOperations::Mode::kLateBeforeRetry};
  bool removal = false;
  EXPECT_THROW((ExactReap(42, operations), removal = true), std::runtime_error);
  EXPECT_EQ(operations.attempts, 1U);
  EXPECT_FALSE(removal);
}

TEST(ReferenceSocketReapBudget, LatePositiveReapNeverReachesRemoval) {
  FakeReapOperations operations{FakeReapOperations::Mode::kLatePositive};
  bool removal = false;
  EXPECT_THROW((ExactReap(42, operations), removal = true), std::runtime_error);
  EXPECT_EQ(operations.attempts, 1U);  // Completed mock PID is never re-waited.
  EXPECT_FALSE(removal);
}

TEST(ReferenceSocketReapBudget, OnTimePositiveReapReachesCompletion) {
  FakeReapOperations operations{FakeReapOperations::Mode::kOnTime};
  bool removal = false;
  EXPECT_NO_THROW((ExactReap(42, operations), removal = true));
  EXPECT_EQ(operations.attempts, 1U);
  EXPECT_TRUE(removal);
}

mode_t IsolatedMask() {
  int ends[2];
  Require(pipe2(ends, O_CLOEXEC | O_NONBLOCK) == 0);
  const auto pid = fork();
  if (pid == 0) {
    close(ends[0]);
    const auto inherited = umask(0000);
    const bool sent = write(ends[1], &inherited, sizeof(inherited)) ==
                      static_cast<ssize_t>(sizeof(inherited));
    close(ends[1]);
    _exit(sent ? 0 : 81);
  }
  close(ends[1]);
  if (pid < 0) {
    close(ends[0]);
    throw std::runtime_error("isolated mask probe fork refused");
  }
  try {
    ExactReap(pid);
    mode_t inherited = 0;
    const auto size = read(ends[0], &inherited, sizeof(inherited));
    const int closed = close(ends[0]);
    ends[0] = -1;
    Require(size == static_cast<ssize_t>(sizeof(inherited)) && !closed);
    return inherited;
  } catch (...) {
    if (ends[0] >= 0) close(ends[0]);
    throw;
  }
}

void ChildCase(const std::string& root, int experiment) {
  umask(experiment == 3 ? 0022 : 0077);  // Isolated ordinary child only.
  if (experiment == 1) EstablishReferenceServerCreationMask();
  if (experiment == 3) {
    bool refused = false;
    try {
      EstablishReferenceServerCreationMask();
    } catch (const std::runtime_error&) {
      refused = true;
    }
    Require(refused);
    return;  // No module/socket creation after unexpected inherited mask.
  }
  if (experiment == 2) {
    // Reader path never calls the server-only transition.
    const auto before = umask(0077);
    Require(before == 0077);
  }
  const auto name = root + "/socket";
  const int socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  Require(socket_fd >= 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  Require(name.size() < sizeof(address.sun_path));
  std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
  Require(bind(socket_fd, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) == 0);
  struct stat info{};
  Require(lstat(name.c_str(), &info) == 0 && S_ISSOCK(info.st_mode) &&
          (info.st_mode & 07777) == (experiment == 1 ? 0777 : 0700));
  Require(close(socket_fd) == 0);
  const int file =
      open((root + "/record").c_str(),
           O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_WRONLY, 0600);
  Require(file >= 0 && fstat(file, &info) == 0 &&
          (info.st_mode & 07777) == 0600 && close(file) == 0);
  Require(mkdir((root + "/private").c_str(), 0700) == 0 &&
          lstat((root + "/private").c_str(), &info) == 0 &&
          (info.st_mode & 07777) == 0700);
}

void IsolatedCase(int experiment) {
  ExclusiveBoundary();
  const auto parent_mask = IsolatedMask();
  char path[] = "/tmp/capmgr-socket-XXXXXX";
  Require(mkdtemp(path) != nullptr);  // Atomic private0700; no chmod window.
  bool removable = false;
  try {
    const std::string root(path);
    struct stat identity{};
    Require(lstat(root.c_str(), &identity) == 0 && S_ISDIR(identity.st_mode) &&
            (identity.st_mode & 07777) == 0700 && identity.st_uid == getuid());
    const auto pid = fork();
    Require(pid >= 0);
    if (pid == 0) {
      try {
        ChildCase(root, experiment);
        _exit(0);
      } catch (...) {
        _exit(82);
      }
    }
    ExactReap(pid);
    ExclusiveBoundary();
    Require(IsolatedMask() == parent_mask);
    ExclusiveBoundary();
    struct stat named{};
    Require(
        lstat(root.c_str(), &named) == 0 && named.st_dev == identity.st_dev &&
        named.st_ino == identity.st_ino && named.st_mode == identity.st_mode &&
        named.st_uid == identity.st_uid);
    removable = true;  // Only exact normal reaps and exclusive boundaries.
    if (experiment != 3) {
      Require(unlink((root + "/socket").c_str()) == 0);
      Require(unlink((root + "/record").c_str()) == 0);
      Require(rmdir((root + "/private").c_str()) == 0);
    }
    Require(rmdir(root.c_str()) == 0);
  } catch (...) {
    // No automatic cleanup, including partially failed removal.
    std::cerr << "RETAINED_SOCKET_CREATION_SCOPE=" << path
              << " OWNED_NORMAL_REAPS=" << removable << '\n';
    throw;
  }
}

TEST(ReferenceSocketCreation, Inherited077Creates0700Socket) {
  ASSERT_NO_THROW(IsolatedCase(0));
}
TEST(ReferenceSocketCreation,
     EarlyServer000Creates0777AndPrivateDataStaysPrivate) {
  ASSERT_NO_THROW(IsolatedCase(1));
}
TEST(ReferenceSocketCreation, ReaderAndParentMasksStayUnchanged) {
  ASSERT_NO_THROW(IsolatedCase(2));
}
TEST(ReferenceSocketCreation, UnexpectedPriorMaskRefusesBeforeSocketCreation) {
  ASSERT_NO_THROW(IsolatedCase(3));
}

TEST(ReferenceSocketPrerequisite, RequiresExactNamedRootSocketAndLiteral) {
  const auto path = CaptureRouteText("/expected");
  nlohmann::json metadata = {{"attempted", true}, {"result", 0},
                             {"errno", 0},        {"uid", 0},
                             {"gid", 0},          {"mode", S_IFSOCK | 0777}};
  EXPECT_TRUE(ReferenceServerSocketReady(path, 0, "/expected", metadata));
  for (const auto mode : {S_IFSOCK | 0700, S_IFREG | 0777, S_IFSOCK | 01777}) {
    metadata["mode"] = mode;
    EXPECT_FALSE(ReferenceServerSocketReady(path, 0, "/expected", metadata));
  }
  metadata["mode"] = S_IFSOCK | 0777;
  metadata["uid"] = 301;
  EXPECT_FALSE(ReferenceServerSocketReady(path, 0, "/expected", metadata));
  metadata["uid"] = 0;
  metadata["gid"] = 10212;
  EXPECT_FALSE(ReferenceServerSocketReady(path, 0, "/expected", metadata));
  metadata["gid"] = 0;
  EXPECT_FALSE(ReferenceServerSocketReady(path, -1, "/expected", metadata));
  EXPECT_FALSE(ReferenceServerSocketReady(path, 0, "/other", metadata));
  metadata["result"] = -1;
  metadata["errno"] = EACCES;
  EXPECT_FALSE(ReferenceServerSocketReady(path, 0, "/expected", metadata));
}

}  // namespace
}  // namespace capmgr::fixture::realpolicy
