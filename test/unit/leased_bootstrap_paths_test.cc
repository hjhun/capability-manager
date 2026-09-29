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

#include "../fixtures/leased_bootstrap_paths.hh"
#include "../fixtures/leased_bootstrap_children.hh"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace capmgr::fixture::leasedbootstrap {

TEST(LeasedBootstrapPaths, ExactFixedComponents) {
  const auto paths = ParseCatalogLink(
      "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/catalog");
  EXPECT_EQ(paths.scope, "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x");
  EXPECT_EQ(paths.catalog, paths.scope + "/catalog");
  EXPECT_EQ(paths.lock_parent, paths.scope + "/lock-parent");
  EXPECT_EQ(paths.lock, paths.lock_parent + "/generation.lock");
}

TEST(LeasedBootstrapPaths, RejectsDifferentAncestorAndScope) {
  for (const auto* path :
       {"opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/catalog",
        "/tmp/capmgr-leased-bootstrap-fixture-aZ019x/catalog",
        "/opt/usr/other-bootstrap-fixture-aZ019x/catalog",
        "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019/catalog",
        "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019xx/catalog"})
    EXPECT_THROW(ParseCatalogLink(path), std::runtime_error);
}

TEST(LeasedBootstrapPaths, RejectsNoncanonicalAndDeletedLinks) {
  for (const auto* path :
       {"/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/catalog/",
        "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/catalog (deleted)",
        "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/./catalog",
        "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x//catalog",
        "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/../catalog",
        "/opt/usr/capmgr-leased-bootstrap-fixture-a/019x/catalog",
        "/opt/usr/capmgr-leased-bootstrap-fixture-a.019x/catalog"})
    EXPECT_THROW(ParseCatalogLink(path), std::runtime_error);
}

TEST(LeasedBootstrapPaths, RejectsEmbeddedNullAndInvalidBytes) {
  std::string path = "/opt/usr/capmgr-leased-bootstrap-fixture-aZ019x/catalog";
  const auto position = path.find("aZ019x");
  for (char c : {'\0', '\n', '\r', static_cast<char>(0xff)}) {
    path[position] = c;
    EXPECT_THROW(ParseCatalogLink(path), std::runtime_error);
  }
}

TEST(LeasedBootstrapPaths, InvalidBorrowedFdRejects) {
  EXPECT_THROW(ResolveCatalog(-1), std::runtime_error);
}

TEST(LeasedBootstrapPaths, BorrowedRegularFileStaysOpen) {
  char path[] = "/tmp/capmgr-bootstrap-path-XXXXXX";
  const int created = mkstemp(path);
  ASSERT_GE(created, 0);
  ASSERT_EQ(close(created), 0);
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(fcntl(fd, F_GETFL) & O_ACCMODE, O_RDONLY);
  struct stat before{}, after{};
  ASSERT_EQ(fstat(fd, &before), 0);
  EXPECT_THROW(ResolveCatalog(fd), std::runtime_error);
  EXPECT_EQ(fstat(fd, &after), 0);
  EXPECT_EQ(after.st_dev, before.st_dev);
  EXPECT_EQ(after.st_ino, before.st_ino);
  EXPECT_EQ(close(fd), 0);
  EXPECT_EQ(unlink(path), 0);
}

TEST(LeasedBootstrapPaths, OPathDirectoryStaysOpen) {
  const int fd = open("/tmp", O_PATH | O_DIRECTORY | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  EXPECT_THROW(ResolveCatalog(fd), std::runtime_error);
  EXPECT_NE(fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(close(fd), 0);
}

namespace {

// No kernel child is launched in these ownership fault cases.
struct ChildReplies : ChildOperations {
  bool first_exited = false;
  int first_error = 0;
  int reaps = 0;
  int Observe(pid_t pid, ChildExit& result) noexcept override {
    result = {pid == 112 || first_exited, 0, 0};
    return pid == 111 ? first_error : 0;
  }

  int Kill(pid_t) noexcept override { return 0; }
  int Reap(pid_t) noexcept override {
    ++reaps;
    return 0;
  }
};

void Attach(Children& children, size_t slot, pid_t pid) {
  children.records[slot] = children.owned.Reserve();
  children.owned.AttachReserved(children.records[slot], pid);
}
}  // namespace

TEST(LeasedBootstrapChildren, EarlierTimeoutSurvivesLaterSuccessfulProbe) {
  ChildReplies replies;
  Children children(replies);
  Attach(children, 0, 111);
  const auto first = children.records[0];
  EXPECT_THROW(children.Wait(0, std::chrono::seconds(0)), std::runtime_error);
  Attach(children, 1, 112);
  EXPECT_EQ(children.Wait(1, std::chrono::seconds(1)).exit_code, 0);
  EXPECT_EQ(children.records[0], first);
  EXPECT_FALSE(children.CleanupEligible());
  replies.first_exited = true;
  children.CleanupKnown();
  EXPECT_TRUE(children.Empty());
  EXPECT_TRUE(children.uncertain);
  EXPECT_FALSE(children.CleanupEligible());
  EXPECT_EQ(replies.reaps, 2);
}

TEST(LeasedBootstrapChildren, ObservationErrorPoisonsLaterKnownCleanup) {
  ChildReplies replies;
  Children children(replies);
  Attach(children, 0, 111);
  replies.first_error = EINTR;
  EXPECT_EQ(children.Observe(0).system_error, EINTR);
  EXPECT_TRUE(children.uncertain);
  replies.first_error = 0;
  replies.first_exited = true;
  children.CleanupKnown();
  EXPECT_TRUE(children.Empty());
  EXPECT_FALSE(children.CleanupEligible());
}

TEST(LeasedBootstrapChildren, OnlyExactReapsResolveAllRecords) {
  ChildReplies replies;
  Children children(replies);
  replies.first_exited = true;
  Attach(children, 0, 111);
  Attach(children, 2, 112);
  EXPECT_FALSE(children.CleanupEligible());
  EXPECT_EQ(children.Wait(0, std::chrono::seconds(1)).exit_code, 0);
  EXPECT_FALSE(children.CleanupEligible());
  EXPECT_EQ(children.Wait(2, std::chrono::seconds(1)).exit_code, 0);
  EXPECT_TRUE(children.CleanupEligible());
}

namespace {

struct RetriedReplies : ChildOperations {
  int observe_calls = 0, reap_calls = 0, kill_calls = 0;
  int observe_error = 0, reap_error = 0, kill_error = 0;
  int Observe(pid_t, ChildExit& result) noexcept override {
    ++observe_calls;
    result = {observe_calls > 1 || !kill_error, 0, 0};
    return observe_calls == 1 ? observe_error : 0;
  }

  int Kill(pid_t) noexcept override {
    return ++kill_calls == 1 ? kill_error : 0;
  }

  int Reap(pid_t) noexcept override {
    return ++reap_calls == 1 ? reap_error : 0;
  }
};

}  // namespace

TEST(LeasedBootstrapChildren,
     DirectTableInspectCannotHideTransientObserveError) {
  RetriedReplies replies;
  replies.observe_error = EINTR;
  Children children(replies);
  Attach(children, 0, 111);
  // The supervisor receives this exact table, without the Observe wrapper.
  EXPECT_EQ(children.owned.Inspect(children.records[0]).system_error, EINTR);
  const auto terminal = children.owned.Inspect(children.records[0]);
  EXPECT_EQ(terminal.state, ChildState::Complete);
  EXPECT_EQ(terminal.system_error, 0);
  children.owned.Release(children.records[0]);
  children.records[0] = 0;
  EXPECT_TRUE(children.Empty());
  EXPECT_FALSE(children.CleanupEligible());
}

TEST(LeasedBootstrapChildren, InternalStopRetryCannotHideTransientFailures) {
  for (int seam = 0; seam < 3; ++seam) {
    RetriedReplies replies;
    if (seam == 0) replies.observe_error = EINTR;
    if (seam == 1) replies.reap_error = EINTR;
    if (seam == 2) replies.kill_error = EPERM;
    Children children(replies);
    Attach(children, 0, 111);
    children.CleanupKnown();
    EXPECT_TRUE(children.Empty()) << seam;
    EXPECT_TRUE(children.uncertain) << seam;
    EXPECT_FALSE(children.CleanupEligible()) << seam;
    EXPECT_GE(replies.observe_calls + replies.reap_calls, 3) << seam;
  }
}

TEST(LeasedBootstrapChildren, StartupAndDrainExpiryRetainAfterExactCleanup) {
  for (const char* stage :
       {"fixture startup deadline", "fixture drain deadline"}) {
    ChildReplies replies;
    Children children(replies);
    Attach(children, 0, 111);
    const auto deadline = std::chrono::steady_clock::now();
    EXPECT_THROW(children.CheckDeadline(deadline, deadline, stage),
                 std::runtime_error);
    replies.first_exited = true;
    children.CleanupKnown();
    EXPECT_TRUE(children.Empty());
    EXPECT_FALSE(children.CleanupEligible());
    int absent = 0, removed = 0, phase_pass = 0;
    if (CleanupScope(children, [&] { ++absent; }, [&] { ++removed; }))
      ++phase_pass;
    EXPECT_EQ(absent, 1);  // Injected later successful ECHILD-equivalent proof.
    EXPECT_EQ(removed, 0);
    EXPECT_EQ(phase_pass, 0);
  }
}

TEST(LeasedBootstrapChildren,
     InternalBootstrapDeadlineCannotBecomeNegativePass) {
  ChildReplies replies;
  Children children(replies);
  Attach(children, 0, 111);
  children.DeadlineFailure("Worker bootstrap deadline");
  replies.first_exited = true;
  children.CleanupKnown();
  EXPECT_TRUE(children.Empty());
  EXPECT_FALSE(children.CleanupEligible());
  int removed = 0;
  EXPECT_FALSE(CleanupScope(children, [] {}, [&] { ++removed; }));
  EXPECT_EQ(removed, 0);
}

TEST(LeasedBootstrapChildren, EveryInternalSessionDeadlineRetainsScope) {
  for (const char* reason :
       {"Frontend worker-close drain deadline",
        "Frontend partial reply deadline", "Frontend CANCEL write deadline",
        "Frontend START write deadline"}) {
    ChildReplies replies;
    Children children(replies);
    Attach(children, 0, 111);
    const auto now = std::chrono::steady_clock::now();
    EXPECT_THROW(
        children.WithinDeadline(
            now + std::chrono::seconds(7), "fixture drain deadline",
            [&] { throw std::runtime_error(reason); }, [&] { return now; }),
        std::runtime_error);
    replies.first_exited = true;
    children.CleanupKnown();
    EXPECT_TRUE(children.Empty());
    int removed = 0, phase_pass = 0;
    if (CleanupScope(children, [] {}, [&] { ++removed; })) ++phase_pass;
    EXPECT_EQ(removed, 0);
    EXPECT_EQ(phase_pass, 0);
  }
}

TEST(LeasedBootstrapChildren, ThrowAfterBudgetPreservesErrorAndPoisonsScope) {
  for (const char* stage :
       {"fixture startup deadline", "fixture drain deadline"}) {
    ChildReplies replies;
    Children children(replies);
    Attach(children, 0, 111);
    const auto initial = std::chrono::steady_clock::now();
    auto now = initial;
    try {
      children.WithinDeadline(
          initial + std::chrono::seconds(7), stage,
          [&] {
            now = initial + std::chrono::seconds(8);
            throw std::runtime_error("original functional exception");
          },
          [&] { return now; });
      FAIL() << "expected original exception";
    } catch (const std::runtime_error& error) {
      EXPECT_STREQ(error.what(), "original functional exception");
    }
    replies.first_exited = true;
    children.CleanupKnown();
    int removed = 0, phase_pass = 0;
    if (CleanupScope(children, [] {}, [&] { ++removed; })) ++phase_pass;
    EXPECT_TRUE(children.Empty());
    EXPECT_EQ(removed, 0);
    EXPECT_EQ(phase_pass, 0);
  }
}
}  // namespace capmgr::fixture::leasedbootstrap
