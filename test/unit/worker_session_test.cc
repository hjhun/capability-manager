// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "launcher/worker_session.hh"
#include "launcher/worker_result.hh"
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
void Put(std::vector<uint8_t>& b,size_t at,uint64_t v,size_t size){for(size_t i=0;i<size;++i)b[at+i]=static_cast<uint8_t>(v>>(i*8));}
std::vector<uint8_t> Reply(uint64_t token,uint64_t seq,WorkerReplyKind kind=WorkerReplyKind::Complete,
                           WorkerFailure failure=WorkerFailure::Rejected,std::string body={},int code=-1,int signal=0,int error=0) {
  std::vector<uint8_t> b(56+body.size());std::copy_n("CWR1",4,b.begin());Put(b,4,1,2);Put(b,6,static_cast<uint16_t>(kind),2);
  Put(b,8,1,8);Put(b,16,seq,8);Put(b,24,token,8);Put(b,32,body.size(),4);Put(b,36,static_cast<uint32_t>(failure),4);
  Put(b,40,static_cast<uint32_t>(code),4);Put(b,44,signal,4);Put(b,48,error,4);std::copy(body.begin(),body.end(),b.begin()+56);return b;
}
struct Pipe {
  int fds[2]{-1,-1};Pipe(){if(pipe2(fds,O_CLOEXEC|O_NONBLOCK))throw std::runtime_error("pipe");}
  ~Pipe(){Close(0);Close(1);}void Close(int n){if(fds[n]>=0)close(fds[n]);fds[n]=-1;}
  void Send(const std::vector<uint8_t>& bytes){ASSERT_EQ(write(fds[1],bytes.data(),bytes.size()),static_cast<ssize_t>(bytes.size()));}
};
struct Fault : JournalOperations {
  bool fail=false;int mode=0,syncs=0;
  ssize_t Write(int fd,const void* b,size_t n) noexcept override {if(mode==1){errno=EIO;return -1;}return LinuxJournalOperations().Write(fd,b,n);}
  int Sync(int fd) noexcept override {++syncs;if(fail || mode==2 || (mode==4 && syncs==2)){errno=EIO;return -1;}return LinuxJournalOperations().Sync(fd);}
  int Replace(int fd) noexcept override {if(mode==3){errno=EIO;return -1;}return LinuxJournalOperations().Replace(fd);}
};
class SessionTest : public CatalogTest {
 protected:
  Pipe commands,cancel,replies;int directory=-1;struct sigaction saved{};Fault fault;
  std::unique_ptr<BrokerJournal> journal;std::unique_ptr<WorkerSession> session;
  const std::string request=R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})";
  void SetUp() override {
    CatalogTest::SetUp();sigaction(SIGPIPE,nullptr,&saved);struct sigaction a{};a.sa_handler=SIG_IGN;sigaction(SIGPIPE,&a,nullptr);
    chmod(root_.c_str(),0700);directory=open(root_.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    std::ofstream(root_+"/state.json")<<R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})";
    chmod((root_+"/state.json").c_str(),0600);
    journal=std::make_unique<BrokerJournal>(directory,geteuid(),fault);
    session=std::make_unique<WorkerSession>(*journal,commands.fds[1],cancel.fds[1],replies.fds[0]);
    commands.Close(1);cancel.Close(1);replies.Close(0);
  }
  void TearDown() override {session.reset();journal.reset();close(directory);sigaction(SIGPIPE,&saved,nullptr);CatalogTest::TearDown();}
  void SendStart(uint64_t token) {
    WorkerCommandReader reader(commands.fds[0],1);std::optional<WorkerCommand> received;
    for(int i=0;i<30 && !received;++i){session->Step();received=reader.ReadOne();}
    ASSERT_TRUE(received);
  EXPECT_EQ(received->token,token);
  EXPECT_EQ(received->request,request);
  }
  std::optional<WorkerEvent> Receive(const std::vector<uint8_t>& bytes) {
    replies.Send(bytes);for(int i=0;i<5;++i){auto event=session->Step();if(event)return event;}return {};
  }
};
}
TEST_F(SessionTest, ReservePrecedesFirstByteAndOnlyCompleteReleasesCapacity) {
  auto token=session->Start(request);
  EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});
  char byte;
  EXPECT_EQ(read(commands.fds[0],&byte,1),-1);
  EXPECT_EQ(errno,EAGAIN);SendStart(token);
  auto accepted=Receive(Reply(token,1,WorkerReplyKind::Accepted,WorkerFailure::None));
  ASSERT_TRUE(accepted);
  auto data=Receive(Reply(token,2,WorkerReplyKind::Stdout,WorkerFailure::None,"native"));
  ASSERT_TRUE(data);
  EXPECT_EQ(data->Data(),"native");
  EXPECT_EQ(journal->Reservations().size(),1u);
  auto done=Receive(Reply(token,3,WorkerReplyKind::Complete,WorkerFailure::None,{},7));
  ASSERT_TRUE(done);
  EXPECT_EQ(done->code,7);
  EXPECT_TRUE(journal->Reservations().empty());
  EXPECT_THROW(session->Cancel(token),Error);
  session->PrepareStop();replies.Close(1);
  EXPECT_FALSE(session->Step());session->ConfirmNormalExit();
  EXPECT_FALSE(journal->Blocked());
}
TEST_F(SessionTest, BufferedCompleteBeforeEofRequiresIndependentNormalExitProof) {
  auto token=session->Start(request);SendStart(token);replies.Send(Reply(token,1));replies.Close(1);
  auto event=session->Step();
  ASSERT_TRUE(event);
  EXPECT_EQ(event->kind,WorkerReplyKind::Complete);
  EXPECT_TRUE(journal->Reservations().empty());
  EXPECT_FALSE(session->Step());
  EXPECT_FALSE(journal->Blocked());
  // Crash/abnormal exit after a Complete still forbids generation restart.
  session->Abort();
  EXPECT_TRUE(journal->Blocked());
  EXPECT_THROW(session->ConfirmNormalExit(),Error);
}
TEST_F(SessionTest, CleanBufferedCompletionAndExitCanFinishGeneration) {
  auto token=session->Start(request);SendStart(token);replies.Send(Reply(token,1));replies.Close(1);commands.Close(0);cancel.Close(0);
  ASSERT_TRUE(session->Step());
  EXPECT_FALSE(session->Step());session->ConfirmNormalExit();
  EXPECT_FALSE(journal->Blocked());
}
TEST_F(SessionTest, FragmentedHeadersAndBodiesHavePermanentDeadlineFailure) {
  auto token=session->Start(request);SendStart(token);
  auto b=Reply(token,1,WorkerReplyKind::Accepted,WorkerFailure::None);
  for(uint8_t byte:b){replies.Send({byte});auto event=session->Step();(void)event;}
  auto out=Reply(token,2,WorkerReplyKind::Stderr,WorkerFailure::None,"abc");auto now=WorkerSession::Clock::now();
  replies.Send({out.begin(),out.begin()+56});
  EXPECT_FALSE(session->Step(now));
  replies.Send({out[56]});
  EXPECT_FALSE(session->Step(now+1s));
  EXPECT_THROW(session->Step(now+5s),Error);
  EXPECT_TRUE(session->Failed());
  EXPECT_TRUE(journal->Blocked());
  EXPECT_EQ(journal->Reservations().size(),1u);
}
TEST_F(SessionTest, EofWithPartialFrameOrMissingCompleteKeepsReservation) {
  auto token=session->Start(request);SendStart(token);
  ASSERT_TRUE(Receive(Reply(token,1,WorkerReplyKind::Accepted,WorkerFailure::None)));
  ASSERT_TRUE(Receive(Reply(token,2,WorkerReplyKind::Stdout,WorkerFailure::None,"output")));
  replies.Send({'C','W'});replies.Close(1);
  EXPECT_FALSE(session->Step());
  EXPECT_THROW(session->Step(),Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});
}
TEST_F(SessionTest, CompletePersistenceFailureNeverExposesCompletionOrFreesSlot) {
  auto token=session->Start(request);SendStart(token);fault.fail=true;replies.Send(Reply(token,1));
  EXPECT_THROW(session->Step(),Error);
  EXPECT_TRUE(session->Failed());
  EXPECT_TRUE(journal->Blocked());
  EXPECT_EQ(journal->Reservations().size(),1u);
  EXPECT_THROW(session->Start(request),Error);
  EXPECT_TRUE(std::filesystem::exists(root_+"/state.next"));
}
TEST_F(SessionTest, ReservePersistenceFailureWritesNoCommandBytes) {
  fault.fail=true;
  EXPECT_THROW(session->Start(request),Error);
  EXPECT_TRUE(session->Failed());
  EXPECT_TRUE(journal->Blocked());
  char byte;
  EXPECT_EQ(read(commands.fds[0],&byte,1),0);
  EXPECT_TRUE(std::filesystem::exists(root_+"/state.next"));
}
TEST_F(SessionTest, CancelOvertakesLargePartialStartOnIndependentPipe) {
  auto json=Json::parse(request);json["params"]["arguments"]["large"]=std::string(50000,'x');
  auto token=session->Start(json.dump());session->Step();session->Cancel(token);session->Cancel(token);
  WorkerCommandReader reader(cancel.fds[0],1,true);std::optional<WorkerCommand> received;
  for(int i=0;i<4 && !received;++i){session->Step();received=reader.ReadOne();}
  ASSERT_TRUE(received);
  EXPECT_EQ(received->token,token);
  EXPECT_EQ(received->kind,WorkerCommandKind::Cancel);
  EXPECT_EQ(journal->Reservations().size(),1u);
  // Crash without ever decoding START: no guessed local no-clone release.
  replies.Close(1);
  EXPECT_THROW(session->Step(),Error);
  EXPECT_EQ(journal->Reservations().size(),1u);
}
TEST_F(SessionTest, CapacityInvalidInputAndConcurrentStartsAreSerialized) {
  EXPECT_THROW(session->Start("not-json"),Error);
  EXPECT_TRUE(journal->Reservations().empty());
  std::atomic<int> success=0,busy=0;std::vector<std::thread> threads;
  for(int i=0;i<8;++i)threads.emplace_back([&]{try{session->Start(request);++success;}catch(const Error& e){EXPECT_EQ(e.code(),ErrorCode::kBusy);++busy;}});
  for(auto& t:threads)t.join();
  EXPECT_EQ(success,4);
  EXPECT_EQ(busy,4);
  EXPECT_EQ(journal->Reservations().size(),4u);
  EXPECT_FALSE(session->Failed());
}
TEST_F(SessionTest, UnsentTokenCannotBeCompleted) {
  auto json=Json::parse(request);json["params"]["arguments"]["large"]=std::string(50000,'x');auto token=session->Start(json.dump());
  replies.Send(Reply(token,1));
  EXPECT_THROW(session->Step(),Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_EQ(journal->Reservations().size(),1u);
}
TEST_F(SessionTest, DuplicateCompletePoisonsEvenAfterFirstDurableRelease) {
  auto token=session->Start(request);SendStart(token);
  ASSERT_TRUE(Receive(Reply(token,1)));
  EXPECT_THROW(Receive(Reply(token,2)),Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_TRUE(journal->Reservations().empty());
}
TEST_F(SessionTest, SignalExitIsAnObservedTerminalNotAProtocolError) {
  auto token=session->Start(request);SendStart(token);
  ASSERT_TRUE(Receive(Reply(token,1,WorkerReplyKind::Accepted,WorkerFailure::None)));
  auto event=Receive(Reply(token,2,WorkerReplyKind::Complete,WorkerFailure::None,{},-1,9));
  ASSERT_TRUE(event);
  EXPECT_EQ(event->signal,9);
  EXPECT_TRUE(journal->Reservations().empty());
}
TEST_F(SessionTest, MalformedReplyDoesNotClearDurableReservation) {
  auto token=session->Start(request);SendStart(token);auto b=Reply(token,1);b[52]=1;
  EXPECT_THROW(Receive(b),Error);
  EXPECT_TRUE(journal->Blocked());
  EXPECT_EQ(journal->Reservations().size(),1u);
  EXPECT_THROW(session->Step(),Error);
}
TEST_F(SessionTest, ClosingSessionWithOutstandingJobBlocksReopen) {
  session->Start(request);session.reset();journal.reset();
  BrokerJournal restart(directory,geteuid());
  EXPECT_TRUE(restart.Blocked());
  EXPECT_EQ(restart.Reservations().size(),1u);
  EXPECT_THROW(restart.BeginGeneration(),Error);
}
TEST_F(SessionTest, ActualWorkerLoopNoCloneCompletionUsesJournalAndIndependentChannels) {
  struct NoClone : WorkerRuntime {pid_t Spawn(NamespaceInitConfig&,void*) noexcept override {errno=EAGAIN;return -1;}} runtime;
  int parent=open("/proc/self",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  ASSERT_GE(parent,0);
  WorkerLoop worker({session->Generation(),commands.fds[0],cancel.fds[0],replies.fds[1],parent,301,301,"System"},WorkerRegistry({{"cli:fixture","/fixed"}}),runtime);
  auto token=session->Start(request);std::optional<WorkerEvent> terminal;
  for(int i=0;i<30 && !terminal;++i){auto event=session->Step();if(event && event->kind==WorkerReplyKind::Complete)terminal=event;worker.Step();}
  ASSERT_TRUE(terminal);
  EXPECT_EQ(terminal->token,token);
  EXPECT_EQ(terminal->failure,WorkerFailure::Clone);
  EXPECT_TRUE(journal->Reservations().empty());
  worker.Shutdown();worker.Step();
  EXPECT_TRUE(worker.Quiescent());close(parent);
}

TEST_F(SessionTest, SaturatedStartHasDeadlineAndCannotReplayFromBeginning) {
  auto json=Json::parse(request);json["params"]["arguments"]["large"]=std::string(50000,'x');
  auto token=session->Start(json.dump());auto now=WorkerSession::Clock::now();session->Step(now);
  EXPECT_THROW(session->Step(now+6s),Error);
  EXPECT_TRUE(session->Failed());EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});
  EXPECT_THROW(session->Step(),Error);
}
class SessionPersistenceTest : public SessionTest,public ::testing::WithParamInterface<int> {};
TEST_P(SessionPersistenceTest, FailedReservationNeverReachesWorker) {
  fault.mode=GetParam();fault.syncs=0;
  EXPECT_THROW(session->Start(request),Error);
  char byte;EXPECT_EQ(read(commands.fds[0],&byte,1),0);EXPECT_TRUE(journal->Blocked());EXPECT_TRUE(session->Failed());
}
TEST_P(SessionPersistenceTest, FailedCompletionNeverReturnsTerminal) {
  auto token=session->Start(request);SendStart(token);fault.mode=GetParam();fault.syncs=0;
  replies.Send(Reply(token,1));EXPECT_THROW(session->Step(),Error);
  EXPECT_TRUE(journal->Blocked());EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});EXPECT_TRUE(session->Failed());
}
INSTANTIATE_TEST_SUITE_P(Persistence,SessionPersistenceTest,::testing::Values(1,2,3,4));
class SessionMalformedTest : public SessionTest,public ::testing::WithParamInterface<int> {};
TEST_P(SessionMalformedTest, BadFramingOrStatePreservesReservation) {
  auto token=session->Start(request);SendStart(token);auto b=Reply(token,1);
  switch(GetParam()) {
    case 0:b[4]=2;break;case 1:b[6]=6;break;case 2:b[8]=2;break;
    case 3:b[16]=2;break;case 4:b[24]=99;break;case 5:Put(b,32,4097,4);break;
    case 6:b[36]=99;break;case 7:b[6]=static_cast<uint8_t>(WorkerReplyKind::Stdout);break;
    case 8:b[36]=0;break; // successful completion without Accepted
    case 9:Put(b,40,256,4);break;
  }
  EXPECT_THROW(Receive(b),Error);
  EXPECT_TRUE(journal->Blocked());EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});
}
INSTANTIATE_TEST_SUITE_P(Invalid,SessionMalformedTest,::testing::Range(0,10));

TEST_F(SessionTest, StateWithoutActualCancelAfterCompleteIsProtocolFailure) {
  auto token=session->Start(request);SendStart(token);ASSERT_TRUE(Receive(Reply(token,1)));
  EXPECT_THROW(Receive(Reply(token,2,WorkerReplyKind::State,WorkerFailure::Rejected,{},-1,0,ENOENT)),Error);
  EXPECT_TRUE(session->Failed());EXPECT_TRUE(journal->Blocked());
}
TEST_F(SessionTest, SentCancelAllowsExactlyOneLateStateForThatToken) {
  auto token=session->Start(request);SendStart(token);session->Cancel(token);session->Step();
  ASSERT_TRUE(Receive(Reply(token,1)));
  auto late=Receive(Reply(token,2,WorkerReplyKind::State,WorkerFailure::Rejected,{},-1,0,ENOENT));ASSERT_TRUE(late);
  EXPECT_TRUE(journal->Reservations().empty());EXPECT_FALSE(session->Failed());
  EXPECT_THROW(Receive(Reply(token,3,WorkerReplyKind::State,WorkerFailure::Rejected,{},-1,0,ENOENT)),Error);
  EXPECT_TRUE(session->Failed());
}
TEST_F(SessionTest, ConsumedCancellationCannotAuthorizeAnotherLateState) {
  auto token=session->Start(request);SendStart(token);session->Cancel(token);session->Step();
  ASSERT_TRUE(Receive(Reply(token,1,WorkerReplyKind::Complete,WorkerFailure::Cancelled)));
  EXPECT_THROW(Receive(Reply(token,2,WorkerReplyKind::State,WorkerFailure::Rejected,{},-1,0,ENOENT)),Error);
  EXPECT_TRUE(journal->Blocked());
}
TEST_F(SessionTest, AmbiguousCancelConsumptionCannotOverflowBoundedCorrelation) {
  uint64_t sequence=1;
  for(int i=0;i<4;++i) {
    auto token=session->Start(request);session->Step();char bytes[4096];ASSERT_GT(read(commands.fds[0],bytes,sizeof(bytes)),0);
    session->Cancel(token);session->Step();ASSERT_GT(read(cancel.fds[0],bytes,sizeof(bytes)),0);
    ASSERT_TRUE(Receive(Reply(token,sequence++)));
  }
  auto last=session->Start(request);EXPECT_THROW(session->Cancel(last),Error);
  EXPECT_TRUE(session->Failed());EXPECT_TRUE(journal->Blocked());EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{last});
}

TEST_F(SessionTest, ResultCollectorCannotObserveCompleteWhenJournalConfirmationFails) {
  WorkerResult collector(9,request);
  auto token=session->Start(collector.Original());ASSERT_TRUE(collector.Bind(token));SendStart(token);
  auto ack=Receive(Reply(token,1,WorkerReplyKind::Accepted,WorkerFailure::None));ASSERT_TRUE(ack);
  EXPECT_FALSE(collector.Accept(*ack));
  auto data=Receive(Reply(token,2,WorkerReplyKind::Stdout,WorkerFailure::None,R"({"jsonrpc":"2.0","id":1,"result":true})"));ASSERT_TRUE(data);
  EXPECT_FALSE(collector.Accept(*data));fault.fail=true;
  EXPECT_THROW(Receive(Reply(token,3,WorkerReplyKind::Complete,WorkerFailure::None,{},0)),Error);
  collector.LoseSession();EXPECT_TRUE(collector.Uncertain());EXPECT_FALSE(collector.Complete());
  EXPECT_TRUE(journal->Blocked());EXPECT_EQ(journal->Reservations(),std::vector<uint64_t>{token});
}

TEST_F(SessionTest, CoordinatorConsumesValidatedLateCancelStateWithoutReopeningCollector) {
  WorkerResult collector(9,request);
  auto token=session->Start(collector.Original());ASSERT_TRUE(collector.Bind(token));SendStart(token);
  unsigned results=0,retired_cancel_acks=0;
  auto route=[&](const WorkerEvent& event){
    // State was correlated against the session's exact sent-CANCEL entitlement.
    // It is coordinator bookkeeping and must never reach the sealed collector.
    if(event.kind==WorkerReplyKind::State){++retired_cancel_acks;return;}
    if(collector.Accept(event))++results;
  };
  auto ack=Receive(Reply(token,1,WorkerReplyKind::Accepted,WorkerFailure::None));ASSERT_TRUE(ack);route(*ack);
  session->Cancel(token);session->Step();
  auto output=Receive(Reply(token,2,WorkerReplyKind::Stdout,WorkerFailure::None,R"({"jsonrpc":"2.0","id":1,"result":true})"));ASSERT_TRUE(output);route(*output);
  auto done=Receive(Reply(token,3,WorkerReplyKind::Complete,WorkerFailure::None,{},0));ASSERT_TRUE(done);route(*done);
  auto late=Receive(Reply(token,4,WorkerReplyKind::State,WorkerFailure::Rejected,{},-1,0,ENOENT));ASSERT_TRUE(late);route(*late);
  EXPECT_EQ(results,1u);EXPECT_EQ(retired_cancel_acks,1u);EXPECT_TRUE(collector.Complete());EXPECT_FALSE(collector.Uncertain());
  EXPECT_FALSE(session->Failed());EXPECT_TRUE(journal->Reservations().empty());
  session->PrepareStop();replies.Close(1);session->Step();session->ConfirmNormalExit();EXPECT_FALSE(journal->Blocked());
}
