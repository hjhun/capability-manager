// SPDX-License-Identifier: Apache-2.0
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
  int observe_error=0,reap_error=0,kill_error=0;
  int kills=0,reaps=0,observes=0;
  bool released=false;
  int Observe(pid_t,ChildExit& result) noexcept override {
    ++observes;result=exit;return observe_error;
  }
  int Kill(pid_t) noexcept override { ++kills;return released?ESRCH:kill_error; }
  int Reap(pid_t) noexcept override { ++reaps;if(!reap_error)released=true;return reap_error; }
};
}
TEST(OwnedChildren, PendingCleanupRetainsCapacityAndCanBeRetried) {
  FakeChildren fake;OwnedChildren owner(1,fake);
  auto id=owner.Adopt(123);
  auto before=std::chrono::steady_clock::now();
  EXPECT_EQ(owner.StopAndWait(id,5ms).state,ChildState::CleanupPending);
  EXPECT_LT(std::chrono::steady_clock::now()-before,1s);
  EXPECT_GE(fake.kills,1);EXPECT_EQ(fake.reaps,0);
  const int sent=fake.kills;
  EXPECT_THROW(owner.Adopt(124),std::runtime_error);
  EXPECT_THROW(owner.Release(id),std::logic_error);
  EXPECT_EQ(owner.Size(),1u);
  fake.exit={true,-1,SIGKILL};
  auto done=owner.Inspect(id);
  EXPECT_EQ(done.state,ChildState::Complete);EXPECT_EQ(done.signal,SIGKILL);
  EXPECT_EQ(owner.Stop(id).state,ChildState::Complete);EXPECT_EQ(fake.kills,sent);
  owner.Release(id);EXPECT_EQ(owner.Size(),0u);
  EXPECT_THROW(owner.Stop(id),std::out_of_range);
}
TEST(OwnedChildren, ExitBeforeCancelNeverSignalsAndReapRetryStaysTerminal) {
  FakeChildren fake;OwnedChildren owner(1,fake);auto id=owner.Adopt(111);
  fake.exit={true,7,0};fake.reap_error=EINTR;
  EXPECT_EQ(owner.Stop(id).state,ChildState::ReapPending);EXPECT_EQ(fake.kills,0);
  EXPECT_EQ(owner.Stop(id).state,ChildState::ReapPending);EXPECT_EQ(fake.kills,0);
  fake.reap_error=0;
  auto status=owner.Stop(id);EXPECT_EQ(status.state,ChildState::Complete);EXPECT_EQ(status.exit_code,7);
  EXPECT_EQ(owner.Stop(id).state,ChildState::Complete);EXPECT_EQ(fake.kills,0);
  EXPECT_EQ(fake.reaps,3);
}
TEST(OwnedChildren, FailedObserveDoesNotSignalUnverifiedPid) {
  FakeChildren fake;OwnedChildren owner(1,fake);auto id=owner.Adopt(111);
  fake.observe_error=EINTR;
  EXPECT_EQ(owner.Stop(id).system_error,EINTR);EXPECT_EQ(fake.kills,0);
  fake.observe_error=0;fake.kill_error=EPERM;
  EXPECT_EQ(owner.Stop(id).system_error,EPERM);EXPECT_EQ(fake.kills,1);
  fake.exit={true,0,0};EXPECT_EQ(owner.Inspect(id).state,ChildState::Complete);
}
TEST(OwnedChildren, IdExhaustionAndValidationDoNotTransferOwnership) {
  FakeChildren fake;OwnedChildren owner(2,fake,UINT64_MAX);
  EXPECT_THROW(owner.Adopt(-1),std::invalid_argument);
  fake.observe_error=ECHILD;EXPECT_THROW(owner.Adopt(9),std::system_error);
  fake.observe_error=0;auto id=owner.Adopt(9);EXPECT_EQ(id,UINT64_MAX);
  EXPECT_THROW(owner.Adopt(9),std::invalid_argument);
  EXPECT_THROW(owner.Adopt(10),std::runtime_error);
  EXPECT_THROW(owner.StopAndWait(id,-1ms),std::invalid_argument);
  fake.exit={true,0,0};owner.Inspect(id);owner.Release(id);
  EXPECT_THROW(owner.Adopt(10),std::runtime_error); // Never wraps/reuses ID.
}
TEST(OwnedChildren, ExternalReapingDisablesSignalsAndCannotBeForgotten) {
  ASSERT_EXIT(([] {
    FakeChildren fake;OwnedChildren owner(1,fake);auto id=owner.Adopt(123);
    fake.observe_error=ECHILD;
    if(owner.Stop(id).state!=ChildState::Uncertain || fake.kills)_exit(1);
    try { owner.Release(id);_exit(2); }catch(const std::logic_error&){}
    if(owner.Stop(id).state!=ChildState::Uncertain || fake.kills)_exit(3);
    _exit(0); // Real broker must fail stop/restart, not destroy an active table.
  }()),::testing::ExitedWithCode(0),"");
}
TEST(OwnedChildren, CannotDestroyTableWithUnconfirmedCleanup) {
  ASSERT_DEATH(([] {FakeChildren fake;OwnedChildren owner(1,fake);owner.Adopt(123);}()),"");
}
TEST(OwnedChildren, ConcurrentCancelCannotSignalAcrossReap) {
  struct BlockingReap final:ChildOperations {
    std::promise<void> reaping,release;
    std::shared_future<void> allowed=release.get_future().share();
    bool exited=false;std::atomic<int> kills{0};
    int Observe(pid_t,ChildExit& result) noexcept override {result={exited,0,0};return 0;}
    int Kill(pid_t) noexcept override {++kills;return 0;}
    int Reap(pid_t) noexcept override {reaping.set_value();allowed.wait();return 0;}
  } fake;
  OwnedChildren owner(1,fake);auto id=owner.Adopt(123);fake.exited=true;
  auto inspect=std::async(std::launch::async,[&]{return owner.Inspect(id);});
  ASSERT_EQ(fake.reaping.get_future().wait_for(1s),std::future_status::ready);
  auto cancel=std::async(std::launch::async,[&]{return owner.Stop(id);});
  fake.release.set_value();
  EXPECT_EQ(inspect.get().state,ChildState::Complete);
  EXPECT_EQ(cancel.get().state,ChildState::Complete);EXPECT_EQ(fake.kills,0);
}
TEST(OwnedChildren, RealDirectChildKillWaitAndReap) {
  pid_t child=fork();ASSERT_GE(child,0);
  if(child==0) {for(;;)pause();}
  OwnedChildren owner;auto id=owner.Adopt(child);
  auto done=owner.StopAndWait(id,2s);
  ASSERT_EQ(done.state,ChildState::Complete);EXPECT_EQ(done.signal,SIGKILL);
  int status=0;EXPECT_EQ(waitpid(child,&status,WNOHANG),-1);EXPECT_EQ(errno,ECHILD);
  EXPECT_EQ(owner.Stop(id).state,ChildState::Complete);owner.Release(id);
}
TEST(OwnedChildren, RealNormalExitIsObservedWithoutLosingStatus) {
  pid_t child=fork();ASSERT_GE(child,0);if(child==0)_exit(17);
  OwnedChildren owner;auto id=owner.Adopt(child);ChildStatus status;
  for(int i=0;i<1000;++i) {
    status=owner.Inspect(id);if(status.state==ChildState::Complete)break;
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(status.state,ChildState::Complete);EXPECT_EQ(status.exit_code,17);
  EXPECT_EQ(status.signal,0);owner.Release(id);
}

TEST(OwnedChildren, BoundedStopRetriesObservationAndSignalFailures) {
  struct Transient final:ChildOperations {
    int observes=0,kills=0;
    int Observe(pid_t,ChildExit& exit) noexcept override {
      ++observes;
      if(observes==2)return EINTR; // First Stop, after successful Adopt.
      exit={kills>=2,0,0};return 0;
    }
    int Kill(pid_t) noexcept override {return ++kills==1?EINTR:0;}
    int Reap(pid_t) noexcept override {return 0;}
  } fake;
  OwnedChildren owner(1,fake);auto id=owner.Adopt(123);
  auto done=owner.StopAndWait(id,100ms);
  EXPECT_EQ(done.state,ChildState::Complete);EXPECT_EQ(done.system_error,0);
  EXPECT_EQ(fake.kills,2);
}
TEST(OwnedChildren, PersistentSignalErrorSurvivesInspectionAndKeepsSlot) {
  FakeChildren fake;OwnedChildren owner(1,fake);auto id=owner.Adopt(123);
  fake.kill_error=EPERM;
  auto pending=owner.StopAndWait(id,5ms);
  EXPECT_EQ(pending.state,ChildState::CleanupPending);EXPECT_EQ(pending.system_error,EPERM);
  EXPECT_GT(fake.kills,1);EXPECT_EQ(owner.Inspect(id).system_error,EPERM);
  EXPECT_THROW(owner.Adopt(124),std::runtime_error);
  fake.kill_error=0;
  EXPECT_EQ(owner.Stop(id).system_error,0); // Successful signal clears its failure.
  fake.exit={true,0,0};EXPECT_EQ(owner.Inspect(id).state,ChildState::Complete);
}
