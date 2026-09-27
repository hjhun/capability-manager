// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_loop.hh"
#include <gtest/gtest.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <vector>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
struct Pipe {
  int fd[2]{-1,-1};
  Pipe(){if(pipe2(fd,O_CLOEXEC|O_NONBLOCK))throw std::runtime_error("pipe");}
  ~Pipe(){for(int value:fd)if(value>=0)close(value);}
  void Send(const std::vector<uint8_t>& bytes){ASSERT_EQ(write(fd[1],bytes.data(),bytes.size()),static_cast<ssize_t>(bytes.size()));}
};
struct Signals {
  struct sigaction pipe{},child{};
  Signals(){sigaction(SIGPIPE,nullptr,&pipe);sigaction(SIGCHLD,nullptr,&child);struct sigaction a{};a.sa_handler=SIG_IGN;sigaction(SIGPIPE,&a,nullptr);a.sa_handler=SIG_DFL;sigaction(SIGCHLD,&a,nullptr);}
  ~Signals(){sigaction(SIGPIPE,&pipe,nullptr);sigaction(SIGCHLD,&child,nullptr);}
};
void Write(int fd,const void* bytes,size_t size) {
  auto* data=static_cast<const char*>(bytes);
  while(size){ssize_t n=write(fd,data,size);if(n<0 && errno==EINTR)continue;if(n<=0)_exit(88);data+=n;size-=static_cast<size_t>(n);}
}
struct Runtime : WorkerRuntime {
  enum Mode { Normal,Linger,NoReady,MissingExit,Flood,CloneError } mode=Normal;
  int spawns=0;pid_t last=-1;
  pid_t Spawn(NamespaceInitConfig& c,void*) noexcept override {
    ++spawns;if(mode==CloneError){errno=EAGAIN;return -1;}
    pid_t pid=fork();if(pid){last=pid;return pid;}
    // This test-only child is a direct-child protocol fixture, not namespace or
    // privilege isolation. Target integration uses real NamespaceInit separately.
    for(int fd=3;fd<1024;++fd)if(fd!=c.control_read && fd!=c.status_write && fd!=c.stdout_write && fd!=c.stderr_write)close(fd);
    if(mode==NoReady)for(;;)pause();
    InitMessage message{InitMessageKind::Ready,InitStage::Go,0,0,0};Write(c.status_write,&message,sizeof(message));
    pollfd control{c.control_read,POLLIN,0};
    if(poll(&control,1,1000)!=1 || (control.revents&POLLHUP))_exit(89);
    char go=0;if(read(c.control_read,&go,1)!=1 || go!='G')_exit(89);
    message={InitMessageKind::Started,InitStage::Exec,0,0,0};Write(c.status_write,&message,sizeof(message));
    if(mode==Linger)for(;;)pause();
    if(mode==Flood){std::array<char,4096> bytes{};for(int i=0;i<200;++i)Write(c.stdout_write,bytes.data(),bytes.size());}
    else {Write(c.stdout_write,"stdout",6);Write(c.stderr_write,"stderr",6);}
    if(mode!=MissingExit){message={InitMessageKind::Exited,InitStage::Wait,0,7,0};Write(c.status_write,&message,sizeof(message));}
    _exit(0);
  }
};
uint64_t Get(const uint8_t* b,size_t size){uint64_t n=0;for(size_t i=0;i<size;++i)n|=static_cast<uint64_t>(b[i])<<(i*8);return n;}
struct Reply {WorkerReplyKind kind;uint64_t token;WorkerFailure failure;int code;std::string data;};
struct Fixture {
  Signals signals;Pipe commands,cancel,replies;Runtime runtime;int parent=open("/proc/self",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  std::unique_ptr<WorkerLoop> loop;std::vector<uint8_t> pending;std::vector<Reply> received;
  uint64_t sequence=1,cancel_sequence=1;
  Fixture(WorkerLimits limits={},int anchor=-1,bool registered=true){loop=std::make_unique<WorkerLoop>(WorkerContext{7,commands.fd[0],cancel.fd[0],replies.fd[1],anchor<0?parent:anchor,301,301,"System"},WorkerRegistry(registered?std::vector<RegisteredCli>{{"cli:fixture","/fixture"}}:std::vector<RegisteredCli>{}),runtime,limits);}
  ~Fixture(){loop->Shutdown();for(int i=0;i<2000 && !loop->Quiescent();++i){loop->Step();usleep(1000);}loop.reset();close(parent);}
  std::vector<uint8_t> StartBytes(uint64_t token){return EncodeWorkerCommand({WorkerCommandKind::Start,7,sequence++,token,R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})"});}
  void Start(uint64_t token){commands.Send(StartBytes(token));}
  void Cancel(uint64_t token){cancel.Send(EncodeWorkerCommand({WorkerCommandKind::Cancel,7,cancel_sequence++,token,{}}));}
  void Drain(){
    uint8_t bytes[8192];ssize_t n;
    while((n=read(replies.fd[0],bytes,sizeof(bytes)))>0)pending.insert(pending.end(),bytes,bytes+n);
    while(pending.size()>=56) {
      EXPECT_EQ(std::string(reinterpret_cast<char*>(pending.data()),4),"CWR1");
      size_t size=Get(pending.data()+32,4);EXPECT_LE(size,4096u);if(pending.size()<56+size)break;
      received.push_back({static_cast<WorkerReplyKind>(Get(pending.data()+6,2)),Get(pending.data()+24,8),static_cast<WorkerFailure>(Get(pending.data()+36,4)),static_cast<int32_t>(Get(pending.data()+40,4)),std::string(reinterpret_cast<char*>(pending.data()+56),size)});
      pending.erase(pending.begin(),pending.begin()+56+size);
    }
  }
  void Tick(bool drain=true){loop->Step();if(drain)Drain();usleep(1000);}
  bool Complete(uint64_t token){for(auto& r:received)if(r.token==token && r.kind==WorkerReplyKind::Complete)return true;return false;}
  void Until(uint64_t token){for(int i=0;i<2000 && !Complete(token);++i)Tick();ASSERT_TRUE(Complete(token));}
  Reply Done(uint64_t token){for(auto& r:received)if(r.token==token && r.kind==WorkerReplyKind::Complete)return r;throw std::runtime_error("no completion");}
};
}
TEST(WorkerLoop, SeparateStreamsCompleteOnlyAfterExclusiveReap) {
  Fixture f;f.Start(1);f.Until(1);
  EXPECT_EQ(f.Done(1).failure,WorkerFailure::None);EXPECT_EQ(f.Done(1).code,7);
  std::string out,err;for(auto& r:f.received){if(r.kind==WorkerReplyKind::Stdout)out+=r.data;if(r.kind==WorkerReplyKind::Stderr)err+=r.data;}
  EXPECT_EQ(out,"stdout");EXPECT_EQ(err,"stderr");
  int status;EXPECT_EQ(waitpid(f.runtime.last,&status,WNOHANG),-1);EXPECT_EQ(errno,ECHILD);EXPECT_EQ(f.loop->Jobs(),0u);
}
TEST(WorkerLoop, CancelOvertakesPartialStartAndPreventsClone) {
  Fixture f;auto bytes=f.StartBytes(1);std::vector<uint8_t> first(bytes.begin(),bytes.begin()+45);f.commands.Send(first);f.Tick();f.Tick();
  f.Cancel(1);f.Tick();f.commands.Send(std::vector<uint8_t>(bytes.begin()+45,bytes.end()));f.Until(1);
  EXPECT_EQ(f.runtime.spawns,0);EXPECT_EQ(f.Done(1).failure,WorkerFailure::Cancelled);
}
TEST(WorkerLoop, PartialCancelBlocksGoAndTimesOutWithoutBlockingCleanup) {
  Fixture f;f.runtime.mode=Runtime::Linger;f.Start(1);for(int i=0;i<10;++i)f.Tick();
  auto bytes=EncodeWorkerCommand({WorkerCommandKind::Cancel,7,1,1,{}});bytes.resize(10);f.cancel.Send(bytes);f.Tick();
  f.loop->Step(WorkerLoop::Clock::now()+6s);f.Until(1);
  EXPECT_FALSE(f.loop->AdmissionOpen());EXPECT_EQ(f.Done(1).failure,WorkerFailure::Protocol);
}
TEST(WorkerLoop, ReadyDeadlineAndMissingFinalStatusAreFailures) {
  for(auto mode:{Runtime::NoReady,Runtime::MissingExit}) {
    Fixture f({50ms,20ms,1024*1024});f.runtime.mode=mode;f.Start(1);f.Until(1);
    EXPECT_EQ(f.Done(1).failure,mode==Runtime::NoReady?WorkerFailure::Timeout:WorkerFailure::Protocol);
  }
}
TEST(WorkerLoop, CatalogFailureAndCloneFailureHaveNoChildProof) {
  for(bool reject:{false,true}) {
    Fixture f({},-1,!reject);f.runtime.mode=Runtime::CloneError;f.Start(1);f.Until(1);
    EXPECT_EQ(f.Done(1).failure,reject?WorkerFailure::Rejected:WorkerFailure::Clone);
    EXPECT_EQ(f.runtime.spawns,reject?0:1);EXPECT_EQ(f.loop->Jobs(),0u);
  }
}
TEST(WorkerLoop, OutputOverflowIsNotNativeSuccess) {
  Fixture f({30s,5s,4});f.Start(1);f.Until(1);EXPECT_EQ(f.Done(1).failure,WorkerFailure::OutputLimit);
}
TEST(WorkerLoop, SaturatedFrontendCannotBlockCancellationAndReaping) {
  Fixture f;f.runtime.mode=Runtime::Flood;f.Start(1);
  for(int i=0;i<300 && f.loop->AdmissionOpen();++i)f.Tick(false);
  EXPECT_FALSE(f.loop->AdmissionOpen());
  // Queue pressure closes admission; cleanup must proceed even before the frontend
  // starts consuming buffered output. Completion remains explicitly unsuccessful.
  for(int i=0;i<2000 && !f.loop->Quiescent();++i)f.Tick(false);
  EXPECT_TRUE(f.loop->Quiescent());
  EXPECT_FALSE(f.loop->CanExitCleanly()); // Complete/output still queued behind full pipe
  f.Until(1);
  EXPECT_TRUE(f.loop->CanExitCleanly());
  EXPECT_EQ(f.Done(1).failure,WorkerFailure::Backpressure);
}
TEST(WorkerLoop, ActiveCancelProgressesWhileAnotherStartIsPartial) {
  Fixture f;f.runtime.mode=Runtime::Linger;f.Start(1);for(int i=0;i<10;++i)f.Tick();
  auto bytes=f.StartBytes(2);bytes.resize(41);f.commands.Send(bytes);f.Tick();f.Cancel(1);f.Until(1);
  EXPECT_EQ(f.Done(1).failure,WorkerFailure::Cancelled);EXPECT_EQ(f.runtime.spawns,1);
}
TEST(WorkerLoop, DeadAnchoredParentWinsEvenWhenControlWritersRemain) {
  pid_t owner=fork();ASSERT_GE(owner,0);if(!owner)for(;;)pause();
  std::string path="/proc/"+std::to_string(owner);int anchor=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);ASSERT_GE(anchor,0);
  {
    Fixture f({},anchor);f.runtime.mode=Runtime::Linger;f.Start(1);for(int i=0;i<10;++i)f.Tick();
    ASSERT_EQ(kill(owner,SIGKILL),0);siginfo_t observed{};
    ASSERT_EQ(waitid(P_PID,owner,&observed,WEXITED|WNOWAIT),0);
    f.Until(1);EXPECT_FALSE(f.loop->AdmissionOpen());EXPECT_EQ(f.Done(1).failure,WorkerFailure::ParentLost);
  }
  close(anchor);int status;ASSERT_EQ(waitpid(owner,&status,0),owner);
}
TEST(WorkerLoop, BufferedStartWithClosedChannelNeverSpawns) {
  Fixture f;f.Start(1);close(f.commands.fd[1]);f.commands.fd[1]=-1;f.Tick();
  EXPECT_FALSE(f.loop->AdmissionOpen());EXPECT_EQ(f.runtime.spawns,0);EXPECT_TRUE(f.loop->Quiescent());
}
TEST(WorkerLoop, LostReplyConsumerClosesAdmissionAndRetainsUncertainty) {
  Fixture f;f.runtime.mode=Runtime::Linger;f.Start(1);for(int i=0;i<10;++i)f.Tick();
  close(f.replies.fd[0]);f.replies.fd[0]=-1;for(int i=0;i<2000 && !f.loop->Quiescent();++i)f.Tick(false);
  EXPECT_FALSE(f.loop->AdmissionOpen());EXPECT_TRUE(f.loop->DeliveryLost());EXPECT_TRUE(f.loop->Quiescent());
  EXPECT_FALSE(f.loop->CanExitCleanly());
}

TEST(WorkerLoop, CapacityRejectedStartsConsumePrecancelWithoutHurtingLiveJobs) {
  Fixture f;f.runtime.mode=Runtime::Linger;
  for(uint64_t token=1;token<=4;++token){f.Start(token);for(int i=0;i<5;++i)f.Tick();}
  ASSERT_EQ(f.loop->Jobs(),4u);
  for(uint64_t token=5;token<=9;++token){f.Cancel(token);f.Start(token);f.Until(token);EXPECT_EQ(f.Done(token).failure,WorkerFailure::Cancelled);}
  EXPECT_TRUE(f.loop->AdmissionOpen());EXPECT_EQ(f.loop->Jobs(),4u);EXPECT_EQ(f.runtime.spawns,4);
  f.Cancel(1);f.Until(1);EXPECT_EQ(f.Done(1).failure,WorkerFailure::Cancelled);EXPECT_TRUE(f.loop->AdmissionOpen());
}
TEST(WorkerRegistry, FixedBoundImmutableLookupRejectsAmbiguousEntries) {
  std::vector<RegisteredCli> entries(257,{"cli:x","/fixture"});
  EXPECT_THROW((WorkerRegistry(entries)),std::runtime_error);
  EXPECT_THROW((WorkerRegistry({{"cli:x","/one"},{"cli:x","/two"}})),std::runtime_error);
  EXPECT_THROW((WorkerRegistry({{"action:x","/one"}})),std::runtime_error);
  WorkerRegistry registry({{"cli:x","/one"}});EXPECT_EQ(registry.Resolve("cli:x"),"/one");EXPECT_TRUE(registry.Resolve("cli:unknown").empty());
  static_assert(noexcept(registry.Resolve("cli:x")));
}

TEST(WorkerLoop, NormalExitRequiresClosedAdmissionEvenWithoutJobs) {
  Fixture f;
  EXPECT_FALSE(f.loop->CanExitCleanly());
  f.loop->Shutdown();
  EXPECT_TRUE(f.loop->CanExitCleanly());
}
