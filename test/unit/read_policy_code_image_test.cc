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

#include "../fixtures/read_policy_code_image.hh"
#include "../fixtures/read_policy_context.hh"

#include <gtest/gtest.h>
#include <sys/wait.h>

#include <array>
#include <fstream>
#include <chrono>
#include <thread>

#include <signal.h>

using capmgr::fixture::realpolicy::CodeImage;

namespace {

struct Owned {
  int fd;
  struct stat identity{};
  explicit Owned(const char* path, int flags = O_RDONLY | O_CLOEXEC)
      : fd(open(path, flags)) {
    if (fd < 3 || fstat(fd, &identity)) throw std::runtime_error("test open");
  }

  ~Owned() {
    if (fd >= 0) close(fd);
  }

  int Take() {
    int value = fd;
    fd = -1;
    return value;
  }
};

// Each loader experiment has a fresh process: retained mappings and libc's
// name-based dlopen cache cannot turn a later negative into an earlier image.
void Isolated(void (*body)()) {
  pid_t child = fork();
  ASSERT_GT(child, -1);
  if (!child) {
    int prior = ::testing::Test::HasFailure();
    try {
      body();
    } catch (...) {
      _exit(92);
    }
    _exit(prior || ::testing::Test::HasFailure() ? 93 : 0);
  }
  int status = 0;
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  pid_t observed = 0;
  while ((observed = waitpid(child, &status, WNOHANG)) == 0 &&
         std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (observed == 0) {
    // Exclusive unreaped direct child; timeout is a test failure, not success.
    kill(child, SIGKILL);
    ASSERT_EQ(waitpid(child, &status, 0), child);
    FAIL() << "isolated loader experiment timeout";
    return;
  }

  ASSERT_EQ(observed, child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ReadPolicyCodeImage, ClosesOwnedDescriptorBeforeEntry) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    bool checked = false;
    EXPECT_EQ(image.Invoke(
                  [&](int before) {
                    EXPECT_EQ(before, fd);
                    EXPECT_NE(fcntl(before, F_GETFD), -1);
                    checked = true;
                  },
                  "test", "test", "test", std::to_string(fd).c_str()),
              27);
    EXPECT_TRUE(checked);
    EXPECT_EQ(fcntl(fd, F_GETFD), -1);
  });
}

TEST(ReadPolicyCodeImage, ContextFailurePrecedesLoading) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    const auto name = std::string("/proc/self/fd/") + std::to_string(fd);
    EXPECT_THROW(image.Invoke([](int) { throw std::runtime_error("context"); },
                              "test", "test", "test", "test"),
                 std::runtime_error);
    EXPECT_EQ(dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD), nullptr);
    EXPECT_NE(fcntl(fd, F_GETFD), -1);
  });
}

TEST(ReadPolicyCodeImage, MissingEntryRetainsMappingAndDescriptorUntilExit) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_NO_ENTRY_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    auto name = std::string("/proc/self/fd/") + std::to_string(fd);
    EXPECT_THROW(image.Invoke([](int) {}, "test", "test", "test", "test"),
                 std::runtime_error);
    EXPECT_NE(dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD), nullptr);
    EXPECT_NE(fcntl(fd, F_GETFD), -1);
  });
}

TEST(ReadPolicyCodeImage, InvalidElfKeepsOwnership) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_INVALID_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    EXPECT_THROW(image.Invoke([](int) {}, "test", "test", "test", "test"),
                 std::runtime_error);
    EXPECT_NE(fcntl(fd, F_GETFD), -1);
  });
}

TEST(ReadPolicyCodeImage, MissingDescriptorRejectsBeforeContext) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    ASSERT_EQ(close(fd), 0);
    bool called = false;
    EXPECT_THROW(image.Invoke([&](int) { called = true; }, "test", "test",
                              "test", "test"),
                 std::runtime_error);
    EXPECT_FALSE(called);
  });
}

TEST(ReadPolicyCodeImage, SubstitutedDescriptorRejectsBeforeContext) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_TEST_IMAGE), other(CAPMGR_LOADER_NO_ENTRY_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    ASSERT_EQ(dup2(other.fd, fd), fd);
    ASSERT_EQ(fcntl(fd, F_SETFD, FD_CLOEXEC), 0);
    bool called = false;
    EXPECT_THROW(image.Invoke([&](int) { called = true; }, "test", "test",
                              "test", "test"),
                 std::runtime_error);
    EXPECT_FALSE(called);
  });
}

TEST(ReadPolicyCodeImage, NonCloexecRejectsBeforeContext) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    ASSERT_EQ(fcntl(fd, F_SETFD, 0), 0);
    bool called = false;
    EXPECT_THROW(image.Invoke([&](int) { called = true; }, "test", "test",
                              "test", "test"),
                 std::runtime_error);
    EXPECT_FALSE(called);
  });
}

TEST(ReadPolicyCodeImage, WritableAndPathDescriptorsRejected) {
  Isolated([] {
    Owned writable(CAPMGR_LOADER_TEST_IMAGE, O_RDWR | O_CLOEXEC);
    int rw = writable.Take();
    EXPECT_THROW(CodeImage(rw, writable.identity), std::runtime_error);
    EXPECT_EQ(fcntl(rw, F_GETFD), -1);
    Owned path(CAPMGR_LOADER_TEST_IMAGE, O_PATH | O_CLOEXEC);
    int pin = path.Take();
    EXPECT_THROW(CodeImage(pin, path.identity), std::runtime_error);
    EXPECT_EQ(fcntl(pin, F_GETFD), -1);
  });
}

TEST(ReadPolicyCodeImage, ExtraAliasRejectedBeforeLoader) {
  Isolated([] {
    long limit = sysconf(_SC_OPEN_MAX);
    ASSERT_GT(limit, 3);
    ASSERT_LE(limit, 1048576);
    for (int fd = 3; fd < limit; ++fd) close(fd);
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    int extra = fcntl(fd, F_DUPFD_CLOEXEC, fd + 1);
    ASSERT_GT(extra, fd);
    const char* stage = "test";
    EXPECT_THROW(image.Invoke(
                     [&](int pin) {
                       capmgr::fixture::realpolicy::OwnInitialTable(stage, pin);
                     },
                     "test", "test", "test", "test"),
                 std::runtime_error);
    EXPECT_EQ(std::string(stage), "own-fd-scan");
    EXPECT_NE(fcntl(extra, F_GETFD), -1);
    close(extra);
    auto name = std::string("/proc/self/fd/") + std::to_string(fd);
    EXPECT_EQ(dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD), nullptr);
  });
}

TEST(ReadPolicyCodeImage, MissingStdioRejectedBeforeLoader) {
  Isolated([] {
    long limit = sysconf(_SC_OPEN_MAX);
    ASSERT_GT(limit, 3);
    ASSERT_LE(limit, 1048576);
    for (int fd = 3; fd < limit; ++fd) close(fd);
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    ASSERT_EQ(close(0), 0);
    const char* stage = "test";
    EXPECT_THROW(image.Invoke(
                     [&](int pin) {
                       capmgr::fixture::realpolicy::OwnInitialTable(stage, pin);
                     },
                     "test", "test", "test", "test"),
                 std::runtime_error);
    auto name = std::string("/proc/self/fd/") + std::to_string(fd);
    EXPECT_EQ(dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD), nullptr);
  });
}

TEST(ReadPolicyCodeImage, RechecksPinAfterPreloadValidation) {
  Isolated([] {
    Owned owned(CAPMGR_LOADER_TEST_IMAGE);
    int fd = owned.fd;
    CodeImage image(owned.Take(), owned.identity);
    EXPECT_THROW(image.Invoke(
                     [](int pin) {
                       if (fcntl(pin, F_SETFD, 0))
                         throw std::runtime_error("test fcntl");
                     },
                     "test", "test", "test", "test"),
                 std::runtime_error);
    auto name = std::string("/proc/self/fd/") + std::to_string(fd);
    EXPECT_EQ(dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD), nullptr);
  });
}

}  // namespace
