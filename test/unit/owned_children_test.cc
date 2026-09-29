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

#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <future>

#include <signal.h>
#include <sys/wait.h>

#include <thread>

#include <unistd.h>

using namespace capmgr;
using namespace std::chrono_literals;

namespace {

struct FakeChildren final : ChildOperations {
  ChildExit exit{};
  int observe_error = 0, reap_error = 0, kill_error = 0;
  int kills = 0, reaps = 0, observes = 0;
  bool released = false;
  int Observe(pid_t, ChildExit& result) noexcept override {
    ++observes;
    result = exit;
    return observe_error;
  }

  int Kill(pid_t) noexcept override {
    ++kills;
    return released ? ESRCH : kill_error;
  }

  int Reap(pid_t) noexcept override {
    ++reaps;
    if (!reap_error) released = true;
    return reap_error;
  }
};

}  // namespace

TEST(OwnedChildren, PendingCleanupRetainsCapacityAndCanBeRetried) {
  FakeChildren fake;
  OwnedChildren owner(1, fake);
  auto id = owner.Adopt(123);
  auto before = std::chrono::steady_clock::now();
  EXPECT_EQ(owner.StopAndWait(id, 5ms).state, ChildState::CleanupPending);
  EXPECT_LT(std::chrono::steady_clock::now() - before, 1s);
  EXPECT_GE(fake.kills, 1);
  EXPECT_EQ(fake.reaps, 0);
  const int sent = fake.kills;
  EXPECT_THROW(owner.Adopt(124), std::runtime_error);
  EXPECT_THROW(owner.Release(id), std::logic_error);
  EXPECT_EQ(owner.Size(), 1u);
  fake.exit = {true, -1, SIGKILL};
  auto done = owner.Inspect(id);
  EXPECT_EQ(done.state, ChildState::Complete);
  EXPECT_EQ(done.signal, SIGKILL);
  EXPECT_EQ(owner.Stop(id).state, ChildState::Complete);
  EXPECT_EQ(fake.kills, sent);
  owner.Release(id);
  EXPECT_EQ(owner.Size(), 0u);
  EXPECT_THROW(owner.Stop(id), std::out_of_range);
}

TEST(OwnedChildren, ExitBeforeCancelNeverSignalsAndReapRetryStaysTerminal) {
  FakeChildren fake;
  OwnedChildren owner(1, fake);
  auto id = owner.Adopt(111);
  fake.exit = {true, 7, 0};
  fake.reap_error = EINTR;
  EXPECT_EQ(owner.Stop(id).state, ChildState::ReapPending);
  EXPECT_EQ(fake.kills, 0);
  EXPECT_EQ(owner.Stop(id).state, ChildState::ReapPending);
  EXPECT_EQ(fake.kills, 0);
  fake.reap_error = 0;
  auto status = owner.Stop(id);
  EXPECT_EQ(status.state, ChildState::Complete);
  EXPECT_EQ(status.exit_code, 7);
  EXPECT_EQ(owner.Stop(id).state, ChildState::Complete);
  EXPECT_EQ(fake.kills, 0);
  EXPECT_EQ(fake.reaps, 3);
}

TEST(OwnedChildren, FailedObserveDoesNotSignalUnverifiedPid) {
  FakeChildren fake;
  OwnedChildren owner(1, fake);
  auto id = owner.Adopt(111);
  fake.observe_error = EINTR;
  EXPECT_EQ(owner.Stop(id).system_error, EINTR);
  EXPECT_EQ(fake.kills, 0);
  fake.observe_error = 0;
  fake.kill_error = EPERM;
  EXPECT_EQ(owner.Stop(id).system_error, EPERM);
  EXPECT_EQ(fake.kills, 1);
  fake.exit = {true, 0, 0};
  EXPECT_EQ(owner.Inspect(id).state, ChildState::Complete);
}

TEST(OwnedChildren, IdExhaustionAndValidationDoNotTransferOwnership) {
  FakeChildren fake;
  OwnedChildren owner(2, fake, UINT64_MAX);
  EXPECT_THROW(owner.Adopt(-1), std::invalid_argument);
  fake.observe_error = ECHILD;
  EXPECT_THROW(owner.Adopt(9), std::system_error);
  fake.observe_error = 0;
  auto id = owner.Adopt(9);
  EXPECT_EQ(id, UINT64_MAX);
  EXPECT_THROW(owner.Adopt(9), std::invalid_argument);
  EXPECT_THROW(owner.Adopt(10), std::runtime_error);
  EXPECT_THROW(owner.StopAndWait(id, -1ms), std::invalid_argument);
  fake.exit = {true, 0, 0};
  owner.Inspect(id);
  owner.Release(id);
  EXPECT_THROW(owner.Adopt(10), std::runtime_error);  // Never wraps/reuses ID.
}

TEST(OwnedChildren, ExternalReapingDisablesSignalsAndCannotBeForgotten) {
  ASSERT_EXIT(
      ([] {
        FakeChildren fake;
        OwnedChildren owner(1, fake);
        auto id = owner.Adopt(123);
        fake.observe_error = ECHILD;
        if (owner.Stop(id).state != ChildState::Uncertain || fake.kills)
          _exit(1);
        try {
          owner.Release(id);
          _exit(2);
        } catch (const std::logic_error&) {
        }
        if (owner.Stop(id).state != ChildState::Uncertain || fake.kills)
          _exit(3);
        _exit(
            0);  // Real broker must fail stop/restart, not destroy an active table.
      }()),
      ::testing::ExitedWithCode(0), "");
}

TEST(OwnedChildren, CannotDestroyTableWithUnconfirmedCleanup) {
  ASSERT_DEATH(([] {
                 FakeChildren fake;
                 OwnedChildren owner(1, fake);
                 owner.Adopt(123);
               }()),
               "");
}

TEST(OwnedChildren, ConcurrentCancelCannotSignalAcrossReap) {
  struct BlockingReap final : ChildOperations {
    std::promise<void> reaping, release;
    std::shared_future<void> allowed = release.get_future().share();
    bool exited = false;
    std::atomic<int> kills{0};
    int Observe(pid_t, ChildExit& result) noexcept override {
      result = {exited, 0, 0};
      return 0;
    }
    int Kill(pid_t) noexcept override {
      ++kills;
      return 0;
    }
    int Reap(pid_t) noexcept override {
      reaping.set_value();
      allowed.wait();
      return 0;
    }
  } fake;
  OwnedChildren owner(1, fake);
  auto id = owner.Adopt(123);
  fake.exited = true;
  auto inspect =
      std::async(std::launch::async, [&] { return owner.Inspect(id); });
  ASSERT_EQ(fake.reaping.get_future().wait_for(1s), std::future_status::ready);
  auto cancel = std::async(std::launch::async, [&] { return owner.Stop(id); });
  fake.release.set_value();
  EXPECT_EQ(inspect.get().state, ChildState::Complete);
  EXPECT_EQ(cancel.get().state, ChildState::Complete);
  EXPECT_EQ(fake.kills, 0);
}

TEST(OwnedChildren, RealDirectChildKillWaitAndReap) {
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    for (;;) pause();
  }
  OwnedChildren owner;
  auto id = owner.Adopt(child);
  auto done = owner.StopAndWait(id, 2s);
  ASSERT_EQ(done.state, ChildState::Complete);
  EXPECT_EQ(done.signal, SIGKILL);
  int status = 0;
  EXPECT_EQ(waitpid(child, &status, WNOHANG), -1);
  EXPECT_EQ(errno, ECHILD);
  EXPECT_EQ(owner.Stop(id).state, ChildState::Complete);
  owner.Release(id);
}

TEST(OwnedChildren, RealNormalExitIsObservedWithoutLosingStatus) {
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) _exit(17);
  OwnedChildren owner;
  auto id = owner.Adopt(child);
  ChildStatus status;
  for (int i = 0; i < 1000; ++i) {
    status = owner.Inspect(id);
    if (status.state == ChildState::Complete) break;
    std::this_thread::sleep_for(1ms);
  }

  ASSERT_EQ(status.state, ChildState::Complete);
  EXPECT_EQ(status.exit_code, 17);
  EXPECT_EQ(status.signal, 0);
  owner.Release(id);
}

TEST(OwnedChildren, BoundedStopRetriesObservationAndSignalFailures) {
  struct Transient final : ChildOperations {
    int observes = 0, kills = 0;
    int Observe(pid_t, ChildExit& exit) noexcept override {
      ++observes;
      if (observes == 2) return EINTR;  // First Stop, after successful Adopt.
      exit = {kills >= 2, 0, 0};
      return 0;
    }
    int Kill(pid_t) noexcept override { return ++kills == 1 ? EINTR : 0; }
    int Reap(pid_t) noexcept override { return 0; }
  } fake;
  OwnedChildren owner(1, fake);
  auto id = owner.Adopt(123);
  auto done = owner.StopAndWait(id, 100ms);
  EXPECT_EQ(done.state, ChildState::Complete);
  EXPECT_EQ(done.system_error, 0);
  EXPECT_EQ(fake.kills, 2);
}

TEST(OwnedChildren, PersistentSignalErrorSurvivesInspectionAndKeepsSlot) {
  FakeChildren fake;
  OwnedChildren owner(1, fake);
  auto id = owner.Adopt(123);
  fake.kill_error = EPERM;
  auto pending = owner.StopAndWait(id, 5ms);
  EXPECT_EQ(pending.state, ChildState::CleanupPending);
  EXPECT_EQ(pending.system_error, EPERM);
  EXPECT_GT(fake.kills, 1);
  EXPECT_EQ(owner.Inspect(id).system_error, EPERM);
  EXPECT_THROW(owner.Adopt(124), std::runtime_error);
  fake.kill_error = 0;
  EXPECT_EQ(owner.Stop(id).system_error,
            0);  // Successful signal clears its failure.
  fake.exit = {true, 0, 0};
  EXPECT_EQ(owner.Inspect(id).state, ChildState::Complete);
}

TEST(OwnedChildren,
     ReserveBeforeCloneKeepsCapacityAndNeverReusesFailedSpawnToken) {
  FakeChildren fake;
  OwnedChildren owner(1, fake);
  auto first = owner.Reserve();
  EXPECT_EQ(owner.Inspect(first).state, ChildState::Reserved);
  EXPECT_EQ(owner.Stop(first).state, ChildState::Reserved);
  EXPECT_EQ(fake.observes, 0);
  EXPECT_EQ(fake.kills, 0);
  EXPECT_THROW(owner.Reserve(), std::runtime_error);
  EXPECT_THROW(owner.Adopt(123), std::runtime_error);
  owner.AbandonUnspawned(first);
  auto next = owner.Reserve();
  EXPECT_GT(next, first);
  owner.AttachReserved(next, 123);
  EXPECT_EQ(fake.observes, 0);
  fake.observe_error = EINTR;
  EXPECT_EQ(owner.Stop(next).system_error, EINTR);
  EXPECT_EQ(fake.kills, 0);
  EXPECT_THROW(owner.AbandonUnspawned(next), std::logic_error);
  fake.observe_error = 0;
  EXPECT_EQ(owner.Stop(next).state, ChildState::CleanupPending);
  EXPECT_EQ(fake.kills, 1);
  fake.exit = {true, 0, 0};
  EXPECT_EQ(owner.Inspect(next).state, ChildState::Complete);
  owner.Release(next);
}

TEST(OwnedChildren, InvalidAttachOrderingFailsStopWithoutSignals) {
  EXPECT_DEATH(([] {
                 FakeChildren fake;
                 OwnedChildren owner(1, fake);
                 owner.AttachReserved(1, 123);
               }()),
               "");
  EXPECT_DEATH(([] {
                 FakeChildren fake;
                 OwnedChildren owner(1, fake);
                 auto id = owner.Reserve();
                 owner.AttachReserved(id, -1);
               }()),
               "");
  EXPECT_DEATH(([] {
                 FakeChildren fake;
                 OwnedChildren owner(1, fake);
                 auto id = owner.Reserve();
                 owner.AttachReserved(id, 123);
                 owner.AttachReserved(id, 124);
               }()),
               "");
  EXPECT_DEATH(([] {
                 FakeChildren fake;
                 OwnedChildren owner(1, fake);
                 owner.Reserve();
               }()),
               "");
}

TEST(OwnedChildren, ReservedAttachmentOwnsRealChildBeforeFirstObservation) {
  OwnedChildren owner;
  auto id = owner.Reserve();
  pid_t child = fork();
  if (child < 0) {
    owner.AbandonUnspawned(id);
    FAIL() << "fork failed";
  }

  if (!child) {
    for (;;) pause();
  }

  owner.AttachReserved(id, child);
  auto result = owner.StopAndWait(id, 2s);
  ASSERT_EQ(result.state, ChildState::Complete);
  EXPECT_EQ(result.signal, SIGKILL);
  owner.Release(id);
}

TEST(OwnedChildren,
     InitialOwnershipUncertaintyRetainsAttachedSlotWithoutSignaling) {
  ASSERT_EXIT(
      ([] {
        FakeChildren fake;
        OwnedChildren owner(1, fake);
        auto id = owner.Reserve();
        fake.observe_error = ECHILD;
        owner.AttachReserved(id, 123);
        if (fake.observes || owner.Stop(id).state != ChildState::Uncertain ||
            fake.kills || owner.Size() != 1)
          _exit(1);
        try {
          owner.AbandonUnspawned(id);
          _exit(2);
        } catch (const std::logic_error&) {
        }
        _exit(
            0);  // Persistent frontend uncertainty is required if the worker is lost.
      }()),
      ::testing::ExitedWithCode(0), "");
}
