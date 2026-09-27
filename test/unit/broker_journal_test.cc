// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "launcher/broker_journal.hh"
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <thread>
#include <barrier>
#include <mutex>
#include <set>
#include <cerrno>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace capmgr;
namespace {
class JournalTest : public CatalogTest {
 protected:
  int directory=-1;
  void SetUp() override {
    CatalogTest::SetUp();
    ASSERT_EQ(chmod(root_.c_str(),0700),0);
    directory=open(root_.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  ASSERT_GE(directory,0);
    Seed();
  }
  void TearDown() override {if(directory>=0)close(directory);CatalogTest::TearDown();}
  void Seed(uint64_t next=1) {
    std::ofstream(root_+"/state.json")<<Json{{"version",1},{"generation",0},{"next",next},{"state","clean"},{"jobs",Json::array()}};
    ASSERT_EQ(chmod((root_+"/state.json").c_str(),0600),0);
  }
};
}
TEST_F(JournalTest, DurableNormalLifecyclePreservesMonotonicTokens) {
  {
    BrokerJournal journal(directory,geteuid());
  EXPECT_FALSE(journal.Blocked());
    EXPECT_EQ(journal.BeginGeneration(),1u);auto first=journal.Reserve();
  EXPECT_EQ(first,1u);
    EXPECT_THROW(journal.ConfirmNormalWorkerExit(),Error);
    journal.ConfirmJobGone(first);journal.ConfirmNormalWorkerExit();
  }
  BrokerJournal reopened(directory,geteuid());
  EXPECT_FALSE(reopened.Blocked());
  EXPECT_EQ(reopened.Generation(),1u);
  EXPECT_EQ(reopened.BeginGeneration(),2u);
  EXPECT_EQ(reopened.Reserve(),2u);
}
TEST_F(JournalTest, ReapedCrashedWorkerDoesNotClearDurableReservations) {
  pid_t worker=fork();
  ASSERT_GE(worker,0);
  if(!worker) {
    try {BrokerJournal journal(directory,geteuid());journal.BeginGeneration();journal.Reserve();_exit(17);}
    catch(...){_exit(99);}
  }
  int status;
  ASSERT_EQ(waitpid(worker,&status,0),worker);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status),17);
  BrokerJournal restart(directory,geteuid());
  EXPECT_TRUE(restart.Blocked());
  ASSERT_EQ(restart.Reservations(),std::vector<uint64_t>{1});
  EXPECT_THROW(restart.BeginGeneration(),Error);
  EXPECT_THROW(restart.Reserve(),Error);
  EXPECT_THROW(restart.ConfirmJobGone(1),Error);
  EXPECT_THROW(restart.ConfirmNormalWorkerExit(),Error);
}
TEST_F(JournalTest, StatusLossPersistsUncertaintyEvenWithNoJobs) {
  {BrokerJournal journal(directory,geteuid());journal.BeginGeneration();journal.MarkUncertain();
  EXPECT_TRUE(journal.Blocked());}
  BrokerJournal restart(directory,geteuid());
  EXPECT_TRUE(restart.Blocked());
  EXPECT_TRUE(restart.Reservations().empty());
  EXPECT_THROW(restart.BeginGeneration(),Error);
}
TEST_F(JournalTest, GlobalCapacityIncludesEveryReservedJob) {
  BrokerJournal journal(directory,geteuid());journal.BeginGeneration();
  for(uint64_t id=1;id<=4;++id)EXPECT_EQ(journal.Reserve(),id);
  EXPECT_THROW(journal.Reserve(),Error);
  EXPECT_THROW(journal.ConfirmJobGone(99),Error);
  journal.ConfirmJobGone(2);
  EXPECT_EQ(journal.Reserve(),5u);
}
TEST_F(JournalTest, ExhaustedTokenNeverWrapsOrReuses) {
  Seed(std::numeric_limits<uint64_t>::max());
  BrokerJournal journal(directory,geteuid());journal.BeginGeneration();
  auto token=journal.Reserve();
  EXPECT_EQ(token,std::numeric_limits<uint64_t>::max());
  journal.ConfirmJobGone(token);
  EXPECT_THROW(journal.Reserve(),Error);
  journal.ConfirmNormalWorkerExit();
  EXPECT_EQ(journal.BeginGeneration(),2u);
  EXPECT_THROW(journal.Reserve(),Error);
}
TEST_F(JournalTest, ExclusiveStoreRejectsSecondOwnerAndUnsafeStorage) {
  {BrokerJournal journal(directory,geteuid());
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);}
  ASSERT_EQ(chmod(root_.c_str(),0770),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
  ASSERT_EQ(chmod(root_.c_str(),0700),0);
  ASSERT_EQ(chmod((root_+"/state.json").c_str(),0644),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
}
TEST_F(JournalTest, MissingCorruptDuplicateAndSymlinkStateNeverBootstraps) {
  ASSERT_EQ(unlink((root_+"/state.json").c_str()),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
  Seed();std::ofstream(root_+"/state.json")<<"{\"version\":1,\"generation\":0,\"next\":1,\"state\":\"active\",\"state\":\"clean\",\"jobs\":[]}";
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
  std::ofstream(root_+"/state.json")<<"{bad";
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
  ASSERT_EQ(unlink((root_+"/state.json").c_str()),0);
  ASSERT_EQ(symlink("elsewhere",(root_+"/state.json").c_str()),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
}
TEST_F(JournalTest, PartialWriteMarkerPoisonsAdmissionAndSurvivesRestart) {
  {
    BrokerJournal journal(directory,geteuid());journal.BeginGeneration();
    std::ofstream(root_+"/state.next")<<"partial";
    EXPECT_THROW(journal.Reserve(),Error);
  EXPECT_TRUE(journal.Blocked());
    EXPECT_THROW(journal.ConfirmNormalWorkerExit(),Error);
  }
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
  EXPECT_TRUE(std::filesystem::exists(root_+"/state.next"));
}

TEST_F(JournalTest, ConcurrentCallsSerializeTokensAndReturnIndependentSnapshots) {
  BrokerJournal journal(directory,geteuid());journal.BeginGeneration();
  std::barrier start(16);std::mutex result_mutex;std::set<uint64_t> tokens;
  std::atomic<int> admitted{0},denied{0};std::vector<std::thread> threads;
  for(int i=0;i<16;++i)threads.emplace_back([&] {
    start.arrive_and_wait();
    try {auto token=journal.Reserve();std::lock_guard lock(result_mutex);tokens.insert(token);++admitted;}
    catch(const Error& error){EXPECT_EQ(error.code(),ErrorCode::kBusy);++denied;}
    EXPECT_LE(journal.Reservations().size(),4u);
  EXPECT_EQ(journal.Generation(),1u);
  });
  for(auto& thread:threads)thread.join();
  EXPECT_EQ(admitted,4);
  EXPECT_EQ(denied,12);
  EXPECT_EQ(tokens.size(),4u);
  auto snapshot=journal.Reservations();journal.ConfirmJobGone(snapshot[0]);
  EXPECT_EQ(snapshot.size(),4u);
  EXPECT_EQ(journal.Reservations().size(),3u);
  threads.clear();
  threads.emplace_back([&]{journal.MarkUncertain();});
  threads.emplace_back([&]{try{journal.ConfirmJobGone(snapshot[1]);}catch(const Error& error){EXPECT_EQ(error.code(),ErrorCode::kBusy);}});
  for(auto& thread:threads)thread.join();
  EXPECT_TRUE(journal.Blocked());
  EXPECT_THROW(journal.Reserve(),Error);
}
TEST_F(JournalTest, SpecialModeBitsAreRejectedForDirectoryStateAndLock) {
  for(mode_t bit:{04000,02000,01000}) {
    ASSERT_EQ(chmod(root_.c_str(),0700|bit),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
    ASSERT_EQ(chmod(root_.c_str(),0700),0);
    ASSERT_EQ(chmod((root_+"/state.json").c_str(),0600|bit),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
    ASSERT_EQ(chmod((root_+"/state.json").c_str(),0600),0);
    ASSERT_EQ(chmod((root_+"/lock").c_str(),0600|bit),0);
  EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
    ASSERT_EQ(chmod((root_+"/lock").c_str(),0600),0);
  }
}
namespace {
class FaultOperations : public JournalOperations {
 public:
  enum Point {None,WriteFailure,FileSyncFailure,RenameFailure,DirectorySyncFailure};
  Point failure=None;int sync_count=0;
  ssize_t Write(int fd,const void* bytes,size_t count) noexcept override {
    if(failure==WriteFailure){errno=EIO;return -1;}
    return LinuxJournalOperations().Write(fd,bytes,count);
  }
  int Sync(int fd) noexcept override {
    ++sync_count;
    if((failure==FileSyncFailure && sync_count==1) || (failure==DirectorySyncFailure && sync_count==2)) {errno=EIO;return -1;}
    return LinuxJournalOperations().Sync(fd);
  }
  int Replace(int directory) noexcept override {
    if(failure==RenameFailure){errno=EIO;return -1;}
    return LinuxJournalOperations().Replace(directory);
  }
};
}
TEST_F(JournalTest, WriteFileSyncRenameAndDirectorySyncFailuresPoisonAdmission) {
  for(auto point:{FaultOperations::WriteFailure,FaultOperations::FileSyncFailure,
                  FaultOperations::RenameFailure,FaultOperations::DirectorySyncFailure}) {
    // Each iteration provisions a separate clean fixture, never a runtime reset.
    Seed();std::filesystem::remove(root_+"/state.next");
    {
      FaultOperations operations;BrokerJournal journal(directory,geteuid(),operations);
      journal.BeginGeneration();operations.failure=point;operations.sync_count=0;
      EXPECT_THROW(journal.Reserve(),Error);
  EXPECT_TRUE(journal.Blocked());
      EXPECT_THROW(journal.Reserve(),Error);
  EXPECT_THROW(journal.ConfirmNormalWorkerExit(),Error);
    }
    if(point==FaultOperations::DirectorySyncFailure) {
      BrokerJournal restart(directory,geteuid());
  EXPECT_TRUE(restart.Blocked());
      EXPECT_EQ(restart.Reservations(),std::vector<uint64_t>{1});
    } else {
      EXPECT_TRUE(std::filesystem::exists(root_+"/state.next"));
      EXPECT_THROW(BrokerJournal(directory,geteuid()),Error);
    }
  }
}
