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

#include "launcher/worker_spawn.hh"
#include "common/error.hh"
#include "launcher/worker_supervisor.hh"

#include <filesystem>
#include <fstream>

#include <sys/stat.h>
#include <gtest/gtest.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace capmgr;
using namespace std::chrono_literals;

namespace {

struct Pipe {
  int fds[2]{-1, -1};
  Pipe() {
    if (pipe2(fds, O_CLOEXEC)) throw std::runtime_error("pipe");
  }

  ~Pipe() {
    for (int fd : fds)
      if (fd >= 0) close(fd);
  }

  void Close(int i) {
    if (fds[i] >= 0) close(fds[i]);
    fds[i] = -1;
  }
};

struct Fixture {
  Pipe command, cancel, reply, ready;
  int parent = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  int catalog = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  OwnedChildren children{1};
  struct sigaction saved{};
  Fixture() {
    sigaction(SIGCHLD, nullptr, &saved);
    struct sigaction a{};
    a.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &a, nullptr);
  }

  ~Fixture() {
    close(parent);
    close(catalog);
    sigaction(SIGCHLD, &saved, nullptr);
  }

  WorkerInheritedFds Fds() {
    return {command.fds[0], cancel.fds[0], reply.fds[1],
            parent,         catalog,       ready.fds[1]};
  }

  void Mode(char mode) { ASSERT_EQ(write(command.fds[1], &mode, 1), 1); }
  ChildStatus Wait(uint64_t token) {
    ChildStatus status;
    for (int i = 0; i < 2000; ++i) {
      status = children.Inspect(token);
      if (status.state == ChildState::Complete) return status;
      usleep(1000);
    }
    status = children.StopAndWait(token, 2s);
    ADD_FAILURE() << "worker fixture exceeded normal-exit budget";
    return status;
  }
};

}  // namespace

TEST(WorkerSpawn, FixedImageGetsOnlyMappedDescriptorsAndFixedEnvironment) {
  Fixture f;
  int leak = open("/dev/null", O_RDONLY);
  ASSERT_GE(leak, 0);
  int high = fcntl(leak, F_DUPFD, 300);
  ASSERT_GE(high, 300);
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  setenv("CAPMGR_TEST_LEAK", "must-not-inherit", 1);
  sigset_t block, prior;
  sigemptyset(&block);
  sigaddset(&block, SIGTERM);
  sigprocmask(SIG_BLOCK, &block, &prior);
  f.Mode('N');
  auto token = SpawnFixedWorker(f.children, UINT64_MAX, f.Fds());
  sigprocmask(SIG_SETMASK, &prior, nullptr);
  unsetenv("CAPMGR_TEST_LEAK");
  auto status = f.Wait(token);
  EXPECT_EQ(status.state, ChildState::Complete);
  EXPECT_EQ(status.exit_code, 0);
  f.children.Release(token);
  uint64_t report[2]{};
  EXPECT_EQ(read(f.reply.fds[0], report, sizeof(report)),
            static_cast<ssize_t>(sizeof(report)));
  EXPECT_EQ(report[0], UINT64_MAX);
  EXPECT_GT(report[1], 0u);
  close(leak);
  close(high);
  close(sockets[0]);
  close(sockets[1]);
}

TEST(WorkerSpawn, PositiveSpawnIsOwnedImmediatelyAndCleanupIsObserved) {
  Fixture f;
  f.Mode('L');
  auto token = SpawnFixedWorker(f.children, 1, f.Fds());
  EXPECT_EQ(f.children.Size(), 1u);
  // Stop may precede fixture main; direct child is already owned, never Adopted.
  auto status = f.children.StopAndWait(token, 2s);
  EXPECT_EQ(status.state, ChildState::Complete);
  EXPECT_EQ(status.signal, SIGKILL);
  f.children.Release(token);
  EXPECT_EQ(f.children.Size(), 0u);
}

TEST(WorkerSpawn, NonzeroWorkerExitIsNotSuccessfulExitProof) {
  Fixture f;
  f.Mode('E');
  auto token = SpawnFixedWorker(f.children, 2, f.Fds());
  auto status = f.Wait(token);
  EXPECT_EQ(status.state, ChildState::Complete);
  EXPECT_EQ(status.exit_code, 7);
  f.children.Release(token);
}

TEST(WorkerSpawn, InvalidGenerationDirectionAndDirectoriesCreateNoChild) {
  Fixture f;
  EXPECT_THROW(SpawnFixedWorker(f.children, 0, f.Fds()), Error);
  auto input = f.Fds();
  input.reply_write = f.reply.fds[0];
  EXPECT_THROW(SpawnFixedWorker(f.children, 1, input), Error);
  input = f.Fds();
  input.cancel_read = input.command_read;
  EXPECT_THROW(SpawnFixedWorker(f.children, 1, input), Error);
  input = f.Fds();
  input.catalog_directory = f.command.fds[0];
  EXPECT_THROW(SpawnFixedWorker(f.children, 1, input), Error);
  input = f.Fds();
  input.ready_write = f.ready.fds[0];
  EXPECT_THROW(SpawnFixedWorker(f.children, 1, input), Error);
  input = f.Fds();
  input.ready_write = input.reply_write;
  EXPECT_THROW(SpawnFixedWorker(f.children, 1, input), Error);
  EXPECT_EQ(f.children.Size(), 0u);
}

TEST(WorkerSpawn, ClosedStdioDoesNotCollideWithSourcesOrFixedTargets) {
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    close(0);
    close(1);
    close(2);
    try {
      Fixture f;
      f.Mode('N');
      auto token = SpawnFixedWorker(f.children, 7, f.Fds());
      auto status = f.Wait(token);
      f.children.Release(token);
      _exit(status.exit_code == 0 ? 0 : 71);
    } catch (...) {
      _exit(72);
    }
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(WorkerSpawn, Exit127RequiresFailureHandlingDespitePositiveSpawn) {
  Fixture f;
  f.Mode('X');
  auto token = SpawnFixedWorker(f.children, 2, f.Fds());
  auto status = f.Wait(token);
  EXPECT_EQ(status.state, ChildState::Complete);
  EXPECT_EQ(status.exit_code, 127);
  f.children.Release(token);
}

namespace {

// Uses the separate fixed fixture image, real posix_spawn/OwnedChildren and
// anonymous FD8 transport. No NamespaceInit, catalog load or workload is run.
struct ReadyFixture {
  Fixture f;
  std::string root;
  int directory = -1;
  uint64_t worker = 0;
  std::unique_ptr<BrokerJournal> journal;
  std::unique_ptr<WorkerSupervisor> supervisor;
  struct sigaction previous{};
  ReadyFixture() {
    char path[] = "/tmp/capmgr-ready-XXXXXX";
    auto* made = mkdtemp(path);
    if (!made) throw std::runtime_error("mkdtemp");
    root = made;
    chmod(root.c_str(), 0700);
    directory = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    std::ofstream(root + "/state.json")
        << R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})";
    chmod((root + "/state.json").c_str(), 0600);
    journal = std::make_unique<BrokerJournal>(directory, geteuid());
    struct sigaction a{};
    a.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &a, &previous);
  }

  ~ReadyFixture() {
    supervisor.reset();
    if (worker && f.children.Size()) {
      auto status = f.children.StopAndWait(worker, 2s);
      if (status.state != ChildState::Complete)
        std::abort();  // cannot discard ownership/scope
      f.children.Release(worker);
    }
    journal.reset();
    close(directory);
    sigaction(SIGPIPE, &previous, nullptr);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error) std::abort();
  }

  void Spawn(bool retain_writer = false) {
    auto session = std::make_unique<WorkerSession>(
        *journal, f.command.fds[1], f.cancel.fds[1], f.reply.fds[0]);
    auto generation =
        journal->Generation();  // BeginGeneration fsynced by Session
    f.command.Close(1);
    f.cancel.Close(1);
    f.reply.Close(0);
    worker = SpawnFixedWorker(f.children, generation, f.Fds());
    f.command.Close(0);
    f.cancel.Close(0);
    f.reply.Close(1);
    if (!retain_writer) f.ready.Close(1);
    supervisor = std::make_unique<WorkerSupervisor>(
        std::move(session), f.children, worker, f.ready.fds[0]);
    f.ready.Close(0);
  }

  bool Ready() {
    for (int i = 0; i < 2000; ++i) {
      if (supervisor->PollStartup()) return true;
      usleep(1000);
    }
    return false;
  }
};

}  // namespace

TEST(WorkerSpawn, RealFd8ReadyAndCleanOwnedExitCompleteTheCoordinator) {
  ReadyFixture f;
  f.f.Mode('S');
  f.Spawn();
  ASSERT_TRUE(f.Ready());
  EXPECT_EQ(f.supervisor->CatalogRevision(), 12u);
  f.supervisor->PrepareStop();
  bool stopped = false;
  for (int i = 0; i < 2000 && !stopped; ++i) {
    f.supervisor->Step();
    stopped = f.supervisor->ConfirmNormalExit();
    if (!stopped) usleep(1000);
  }

  EXPECT_TRUE(stopped);
  EXPECT_EQ(f.f.children.Size(), 0u);
  EXPECT_FALSE(f.journal->Blocked());
}

TEST(WorkerSpawn, RetainedReadyWriterPreventsAdmissionEvenAfterRealRecord) {
  ReadyFixture f;
  int report = fcntl(f.f.reply.fds[0], F_DUPFD_CLOEXEC, 20);
  ASSERT_GE(report, 20);
  f.f.Mode('L');
  f.Spawn(true);
  // This report is emitted only after the real image writes READY and closes 8.
  struct pollfd p{report, POLLIN, 0};
  int available = poll(&p, 1, 2000);
  uint64_t bytes[2]{};
  ssize_t count = available > 0 ? read(report, bytes, sizeof(bytes)) : -1;
  close(report);
  ASSERT_EQ(count, static_cast<ssize_t>(sizeof(bytes)));
  EXPECT_EQ(bytes[0], 1u);
  EXPECT_FALSE(f.supervisor->PollStartup());
  EXPECT_FALSE(f.supervisor->PollStartup());
  EXPECT_THROW(f.supervisor->PollStartup(WorkerSupervisor::Clock::now() + 6s),
               Error);
  EXPECT_TRUE(f.journal->Blocked());
  EXPECT_TRUE(f.journal->Reservations().empty());
}

TEST(WorkerSpawn, RealWorkerDeathAfterReadyBlocksStartAndRetainsGeneration) {
  ReadyFixture f;
  f.f.Mode('L');
  f.Spawn();
  ASSERT_TRUE(f.Ready());
  auto status = f.f.children.StopAndWait(f.worker, 2s);
  ASSERT_EQ(status.state, ChildState::Complete);
  EXPECT_THROW(
      f.supervisor->Start(
          R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})"),
      Error);
  EXPECT_TRUE(f.journal->Blocked());
  EXPECT_TRUE(f.journal->Reservations().empty());
}
