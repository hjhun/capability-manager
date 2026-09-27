// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "launcher/worker_supervisor.hh"
#include <fstream>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
struct Pipe {
  int fds[2]{-1,-1};Pipe(){if(pipe2(fds,O_CLOEXEC|O_NONBLOCK))throw std::runtime_error("pipe");}
  ~Pipe(){for(int fd:fds)if(fd>=0)close(fd);}void Close(int i){if(fds[i]>=0)close(fds[i]);fds[i]=-1;}
  void Send(const uint8_t* data,size_t size){ASSERT_EQ(write(fds[1],data,size),static_cast<ssize_t>(size));}
};
struct Child : ChildOperations {
  bool dead=false;int code=0,signal=0,observe_error=0;
  int Observe(pid_t,ChildExit& exit) noexcept override {exit={dead,code,signal};return observe_error;}
  int Kill(pid_t) noexcept override {dead=true;signal=SIGKILL;return 0;}
  int Reap(pid_t) noexcept override{return 0;}
};
class SupervisorTest : public CatalogTest {
 protected:
  Pipe commands,cancel,replies,bootstrap;Child child;OwnedChildren children{1,child};uint64_t worker=0;
  int directory=-1;std::unique_ptr<BrokerJournal> journal;std::unique_ptr<WorkerSupervisor> supervisor;
  struct sigaction previous{};WorkerSupervisor::Clock::time_point now=WorkerSupervisor::Clock::now();
  std::string request=R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})";
  void SetUp() override {
    CatalogTest::SetUp();chmod(root_.c_str(),0700);directory=open(root_.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    std::ofstream(root_+"/state.json")<<R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})";chmod((root_+"/state.json").c_str(),0600);
    journal=std::make_unique<BrokerJournal>(directory,geteuid());struct sigaction a{};a.sa_handler=SIG_IGN;sigaction(SIGPIPE,&a,&previous);
    auto session=std::make_unique<WorkerSession>(*journal,commands.fds[1],cancel.fds[1],replies.fds[0]);commands.Close(1);cancel.Close(1);replies.Close(0);
    worker=children.Reserve();children.AttachReserved(worker,123); // injected operations, no real PID access
    supervisor=std::make_unique<WorkerSupervisor>(std::move(session),children,worker,bootstrap.fds[0],now);bootstrap.Close(0);
  }
  void TearDown() override {
    supervisor.reset();child.observe_error=0;child.dead=true;
    if(children.Size()){auto status=children.Inspect(worker);
  ASSERT_EQ(status.state,ChildState::Complete);children.Release(worker);}
    journal.reset();close(directory);sigaction(SIGPIPE,&previous,nullptr);CatalogTest::TearDown();
  }
  void Ready(uint64_t revision=12) {
    auto record=EncodeWorkerReady(1,revision);bootstrap.Send(record.data(),record.size());bootstrap.Close(1);
    EXPECT_FALSE(supervisor->PollStartup(now));
  EXPECT_TRUE(supervisor->PollStartup(now));
  }
};
}
TEST_F(SupervisorTest, CannotReserveOrSendBeforeExactReadyEofAndLiveWorker) {
  EXPECT_THROW(supervisor->Start(request),Error);
  EXPECT_TRUE(journal->Reservations().empty());
  auto bytes=EncodeWorkerReady(1,12);bootstrap.Send(bytes.data(),bytes.size());
  EXPECT_FALSE(supervisor->PollStartup(now));
  EXPECT_THROW(supervisor->Start(request),Error);
  EXPECT_TRUE(journal->Reservations().empty());
  bootstrap.Close(1);
  ASSERT_TRUE(supervisor->PollStartup(now));
  EXPECT_EQ(supervisor->CatalogRevision(),12u);
  auto token=supervisor->Start(request);
  EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});
}
TEST_F(SupervisorTest, RetainedWriterAndNoDataBothHaveAbsoluteDeadline) {
  auto bytes=EncodeWorkerReady(1,12);bootstrap.Send(bytes.data(),bytes.size());
  EXPECT_FALSE(supervisor->PollStartup(now));
  EXPECT_THROW(supervisor->PollStartup(now+5s),Error);
  EXPECT_TRUE(supervisor->Failed());
  EXPECT_TRUE(journal->Blocked());
}
TEST_F(SupervisorTest, SilentWorkerTimeoutDoesNotEnableRestart) {
  EXPECT_FALSE(supervisor->PollStartup(now));
  EXPECT_THROW(supervisor->PollStartup(now+5s),Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_THROW(journal->BeginGeneration(),Error);
}
TEST_F(SupervisorTest, LiveCheckRejectsExitAfterReadyBeforeStart) {
  Ready();child.dead=true;child.code=127;
  EXPECT_THROW(supervisor->Start(request),Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_TRUE(journal->Reservations().empty());
}
TEST_F(SupervisorTest, ObservationFailureCannotReuseAnOldRunningState) {
  Ready();child.observe_error=EINTR;
  EXPECT_THROW(supervisor->Start(request),Error);
  EXPECT_TRUE(supervisor->Failed());
  EXPECT_TRUE(journal->Blocked());
}
TEST_F(SupervisorTest, WorkerExitBeforeReadyNeverProvesNoOldJobs) {
  child.dead=true;child.code=127;bootstrap.Close(1);
  EXPECT_THROW(supervisor->PollStartup(now),Error);
  EXPECT_TRUE(journal->Blocked());
}
TEST_F(SupervisorTest, CleanIdleStopRequiresReplyEofAndObservedOwnedExitZero) {
  Ready();supervisor->PrepareStop();replies.Close(1);
  EXPECT_FALSE(supervisor->Step());
  EXPECT_FALSE(supervisor->ConfirmNormalExit());child.dead=true;child.code=0;
  EXPECT_TRUE(supervisor->ConfirmNormalExit());
  EXPECT_EQ(children.Size(),0u);
  EXPECT_FALSE(journal->Blocked());
}
TEST_F(SupervisorTest, AbnormalExitKeepsJournalUncertainEvenWithNoJobs) {
  Ready();supervisor->PrepareStop();replies.Close(1);
  EXPECT_FALSE(supervisor->Step());child.dead=true;child.code=7;
  EXPECT_THROW(supervisor->ConfirmNormalExit(),Error);
  EXPECT_TRUE(journal->Blocked());
}
TEST_F(SupervisorTest, BufferedCompleteDrainsAfterWorkerExitBeforeNormalProof) {
  Ready();auto token=supervisor->Start(request);
  EXPECT_FALSE(supervisor->Step());
  std::array<uint8_t,56> b{};std::copy_n("CWR1",4,b.begin());b[4]=1;b[6]=4;b[8]=1;b[16]=1;b[24]=static_cast<uint8_t>(token);b[36]=1;
  std::fill(b.begin()+40,b.begin()+44,255);replies.Send(b.data(),b.size());replies.Close(1);child.dead=true;
  auto done=supervisor->Step();
  ASSERT_TRUE(done);
  EXPECT_EQ(done->kind,WorkerReplyKind::Complete);
  EXPECT_TRUE(journal->Reservations().empty());
  EXPECT_FALSE(supervisor->Step());
  EXPECT_TRUE(supervisor->ConfirmNormalExit());
  EXPECT_FALSE(journal->Blocked());
}
class BadReadyTest : public SupervisorTest,public ::testing::WithParamInterface<int> {};
TEST_P(BadReadyTest, WrongTruncatedDuplicateOrExtraReadyPoisonsGeneration) {
  auto bytes=EncodeWorkerReady(1,12);std::vector<uint8_t> record(bytes.begin(),bytes.end());
  switch(GetParam()) {
    case 0:record[0]='?';break;case 1:record[4]=2;break;case 2:record[6]=31;break;case 3:record[8]=2;break;
    case 4:record[23]=128;break;case 5:record[24]=1;break;case 6:record.resize(31);break;
    case 7:record.push_back(0);break;case 8:record.insert(record.end(),bytes.begin(),bytes.end());break;
  }
  bootstrap.Send(record.data(),record.size());bootstrap.Close(1);
  EXPECT_THROW({for(int i=0;i<3;++i)supervisor->PollStartup(now);},Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_TRUE(supervisor->Failed());
  EXPECT_THROW(supervisor->Start(request),Error);
}
INSTANTIATE_TEST_SUITE_P(Malformed,BadReadyTest,::testing::Range(0,9));
