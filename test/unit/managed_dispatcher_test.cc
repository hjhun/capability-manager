// SPDX-License-Identifier: Apache-2.0
#include "api/dispatcher.hh"
#include "launcher/worker_result.hh"
#include "fixture.hh"
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
const std::string native=R"( {"jsonrpc":"2.0","id":1,"result":{"isError":true}} )";
class ScriptOperation:public ManagedOperation {
 public:
  explicit ScriptOperation(std::function<void(ScriptOperation&)> script,ManagedPublicationOperations* operations=nullptr)
    :ManagedOperation(operations),script_(std::move(script)){}
  void Confirm(std::shared_ptr<const std::string> terminal={}){Publish(Cleanup::kConfirmedComplete,std::move(terminal));}
  void Uncertain(){Publish(Cleanup::kUncertain);}
  bool Cancelled()const{return CancellationRequested();}
 private:
  void Coordinate() override {script_(*this);}
  std::function<void(ScriptOperation&)> script_;
};
bool Ready(std::future<void>& f){return f.wait_for(1s)==std::future_status::ready;}
void NoCallback(uint64_t,const char*,bool,void* p){++*static_cast<std::atomic<int>*>(p);}
Dispatcher::CloseResult CloseAfterCallback(Dispatcher& dispatcher) {
  Dispatcher::CloseResult status;
  do{status=dispatcher.Close(10ms);}while(status==Dispatcher::CloseResult::kBusy);
  return status;
}
}
TEST(ManagedDispatcher, ReturnAndThrowWithoutProofNeverSynthesizeTerminalOrRelease) {
  for(bool throws:{false,true}) {
    ASSERT_EXIT(([&]{
      alarm(5);Dispatcher dispatcher;std::atomic<int> calls=0;
      auto owner=std::make_shared<ScriptOperation>([&](auto&){if(throws)throw std::runtime_error("lost session");});
      dispatcher.ExecuteManaged(owner,1,NoCallback,&calls);
      for(int i=0;i<1000 && !owner->PollCleanup().quiescent;++i)std::this_thread::sleep_for(1ms);
      if(!owner->PollCleanup().quiescent)_exit(2);
      auto before=std::chrono::steady_clock::now();
      if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending || calls!=0)_exit(3);
      if(std::chrono::steady_clock::now()-before>200ms)_exit(4);
      if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending || !owner->Cancelled())_exit(5);
      _exit(0); // Deliberately unresolved generation; no destructor may abandon it.
    }()),testing::ExitedWithCode(0),"");
  }
}
TEST(ManagedDispatcher, ConfirmedTerminalStillRetainsCapacityUntilCoordinatorExit) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher;std::promise<void> release,callback;
    auto allowed=release.get_future().share();
    auto owner=std::make_shared<ScriptOperation>([&](auto& operation){
      if(!operation.ClientToken())_exit(2);
      operation.Confirm(std::make_shared<const std::string>(native));allowed.wait();
    });
    struct State{Dispatcher* dispatcher;std::promise<void>* callback;uint64_t token=0;int calls=0;} state{&dispatcher,&callback};
    auto token=dispatcher.ExecuteManaged(owner,1,+[](uint64_t token,const char* text,bool event,void* p){
      auto& state=*static_cast<State*>(p);state.token=token;++state.calls;
      if(event || text!=native)_exit(3);
      try{state.dispatcher->Cancel(token);_exit(4);}catch(const Error& e){if(e.code()!=ErrorCode::kNotFound)_exit(5);}
      state.callback->set_value();
    },&state);
    auto ready=callback.get_future();if(!Ready(ready))_exit(6);
    if(CloseAfterCallback(dispatcher)!=Dispatcher::CloseResult::kIoPending)_exit(7);
    if(owner->PollCleanup().quiescent)_exit(8);
    release.set_value();if(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone)_exit(9);
    if(state.calls!=1 || state.token!=token)_exit(10);
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, CoordinatorTlsCannotSelfPublishQuiescence) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher;std::promise<void> release,entered;std::atomic<int> calls=0;
    auto allowed=release.get_future().share();
    struct Held {std::shared_future<void> release;std::promise<void>* entered;
      ~Held(){entered->set_value();release.wait();}};
    auto owner=std::make_shared<ScriptOperation>([&](auto& operation){
      thread_local std::unique_ptr<Held> held;held.reset(new Held{allowed,&entered});
      operation.Confirm(); // No terminal allocation needed for destroy suppression.
    });
    dispatcher.ExecuteManaged(owner,1,NoCallback,&calls);
    auto ready=entered.get_future();if(!Ready(ready))_exit(2);
    if(owner->PollCleanup().quiescent)_exit(3);
    if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending || calls!=0)_exit(4);
    release.set_value();if(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone || calls!=0)_exit(5);
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, MaterializationRetryPublishesOnceWithoutSecondComplete) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher;std::promise<void> pending,retry,callback;
    auto allowed=retry.get_future().share();
    struct Fault:WorkerResultOperations {bool fail=true;
      void BeforeBuild(WorkerResultBuildStage)override{if(fail)throw std::bad_alloc();}};
    auto owner=std::make_shared<ScriptOperation>([&](auto& operation){
      Fault fault;
      WorkerResult collector(operation.ClientToken(),R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:test","arguments":{}}})",&fault);
      collector.Bind(42);WorkerEvent event{WorkerReplyKind::Stdout,42,WorkerFailure::None,-1,0,0,{},native.size()};
      std::copy(native.begin(),native.end(),event.bytes.begin());collector.Accept(event);
      event={WorkerReplyKind::Complete,42,WorkerFailure::None,0,0,0,{},0};
      try{collector.Accept(event);_exit(2);}catch(const std::bad_alloc&){}
      if(!collector.TerminalPending())_exit(3);
      operation.Confirm();pending.set_value();allowed.wait();fault.fail=false;
      auto result=collector.RetryTerminal();operation.Confirm(std::make_shared<const std::string>(std::move(result.response)));
    });
    std::atomic<int> calls=0;
    struct State {std::atomic<int>* calls;std::promise<void>* callback;} state{&calls,&callback};
    dispatcher.ExecuteManaged(owner,1,+[](uint64_t,const char* text,bool,void* p){
      auto& state=*static_cast<State*>(p);if(text!=native || ++*state.calls!=1)_exit(4);state.callback->set_value();
    },&state);
    auto ready=pending.get_future();if(!Ready(ready) || calls!=0)_exit(5);
    retry.set_value();ready=callback.get_future();if(!Ready(ready))_exit(6);
    while(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone){}
    if(calls!=1)_exit(7);_exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, CallbackCancelRecordsRequestOutsideSessionIo) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher;std::promise<void> started;std::atomic<int> calls=0;
    auto owner=std::make_shared<ScriptOperation>([&](auto& operation){
      started.set_value();while(!operation.Cancelled())std::this_thread::sleep_for(1ms);
      operation.Confirm(std::make_shared<const std::string>(native));
    });
    auto token=dispatcher.ExecuteManaged(owner,1,NoCallback,&calls);
    auto ready=started.get_future();if(!Ready(ready))_exit(2);
    struct State {Dispatcher* dispatcher;uint64_t token;std::promise<void> done;} state{&dispatcher,token,{}};
    dispatcher.SetChanged(+[](uint64_t,void* p){auto& state=*static_cast<State*>(p);
      if(state.dispatcher->Close()!=Dispatcher::CloseResult::kBusy)_exit(3);
      state.dispatcher->Cancel(state.token);state.done.set_value();},&state);
    dispatcher.Changed(1);ready=state.done.get_future();if(!Ready(ready))_exit(4);
    while(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone){}
    if(!owner->Cancelled())_exit(5);_exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, ReuseAndInvalidTerminalFailClosed) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher,other;std::atomic<int> calls=0;
    std::this_thread::sleep_for(10ms); // Dispatcher begins in the empty-job wait.
    auto owner=std::make_shared<ScriptOperation>([](auto& operation){operation.Confirm(std::make_shared<const std::string>("not json"));});
    dispatcher.ExecuteManaged(owner,1,NoCallback,&calls);
    try{other.ExecuteManaged(owner,2,NoCallback,&calls);_exit(2);}catch(const Error& e){if(e.code()!=ErrorCode::kConflict)_exit(3);}
    for(int i=0;i<1000;++i){try{dispatcher.CheckAdmission();}catch(const Error&){break;}std::this_thread::sleep_for(1ms);}
    try{dispatcher.CheckAdmission();_exit(4);}catch(const Error&){}
    if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending || calls!=0)_exit(5);
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, TerminalPublicationAllocationFailureKeepsCoherentProofForRetry) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher;std::promise<void> pending,retry,callback;
    auto allowed=retry.get_future().share();
    struct Fault:ManagedPublicationOperations {bool fail=true;void BeforeTerminalPublication()override{if(fail)throw std::bad_alloc();}} fault;
    auto owner=std::make_shared<ScriptOperation>([&](auto& operation){
      auto terminal=std::make_shared<const std::string>(native);
      try{operation.Confirm(terminal);_exit(2);}catch(const std::bad_alloc&){}
      pending.set_value();allowed.wait();fault.fail=false;operation.Confirm(terminal);
    },&fault);
    dispatcher.ExecuteManaged(owner,1,+[](uint64_t,const char*,bool,void* p){static_cast<std::promise<void>*>(p)->set_value();},&callback);
    auto ready=pending.get_future();if(!Ready(ready))_exit(3);
    auto state=owner->PollCleanup();
    if(state.publication->cleanup!=ManagedOperation::Cleanup::kConfirmedComplete || state.publication->terminal || state.quiescent)_exit(4);
    retry.set_value();ready=callback.get_future();if(!Ready(ready))_exit(5);
    while(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone){}
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
namespace {
struct SyncGate:JournalOperations {
  std::atomic<bool> block=false;bool fail=false;
  std::promise<void> entered,release;std::shared_future<void> allowed=release.get_future().share();
  ssize_t Write(int fd,const void* b,size_t n)noexcept override{return LinuxJournalOperations().Write(fd,b,n);}
  int Replace(int fd)noexcept override{return LinuxJournalOperations().Replace(fd);}
  int Sync(int fd)noexcept override{
    if(block.exchange(false)){entered.set_value();allowed.wait();if(fail){errno=EIO;return -1;}}
    return LinuxJournalOperations().Sync(fd);
  }
};
std::vector<uint8_t> RejectedComplete(uint64_t token) {
  std::vector<uint8_t> b(56);std::copy_n("CWR1",4,b.begin());
  auto put=[&](size_t at,uint64_t v,size_t n){for(size_t i=0;i<n;++i)b[at+i]=static_cast<uint8_t>(v>>(8*i));};
  put(4,1,2);put(6,static_cast<uint16_t>(WorkerReplyKind::Complete),2);put(8,1,8);put(16,1,8);put(24,token,8);
  put(36,static_cast<uint32_t>(WorkerFailure::Rejected),4);put(40,UINT32_MAX,4);return b;
}
}
TEST_F(CatalogTest, ManagedDestroyDoesNotWaitOnConfirmJobGoneFsyncOrItsMutex) {
  for(bool fail:{false,true}) {
    ASSERT_EXIT(([&]{
      alarm(8);signal(SIGPIPE,SIG_IGN);chmod(root_.c_str(),0700);
      std::ofstream(root_+"/state.json")<<R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})";
      chmod((root_+"/state.json").c_str(),0600);
      int dir=open(root_.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);int commands[2],cancel[2],reply[2];
      if(dir<0 || pipe2(commands,O_NONBLOCK|O_CLOEXEC) || pipe2(cancel,O_NONBLOCK|O_CLOEXEC) || pipe2(reply,O_NONBLOCK|O_CLOEXEC))_exit(2);
      SyncGate operations;operations.fail=fail;Dispatcher dispatcher;std::atomic<int> callbacks=0;
      auto owner=std::make_shared<ScriptOperation>([&](auto& operation){
        // All journal/session destruction stays on this coordinator, including
        // MarkUncertain/fsync from the Session destructor after a thrown Step.
        BrokerJournal journal(dir,geteuid(),operations);
        WorkerSession session(journal,commands[1],cancel[1],reply[0]);
        WorkerResult result(operation.ClientToken(),R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})");
        auto token=session.Start(result.Original());result.Bind(token);session.Step();
        char buffer[4096];if(read(commands[0],buffer,sizeof(buffer))<=0)_exit(3);
        auto bytes=RejectedComplete(token);if(write(reply[1],bytes.data(),bytes.size())!=static_cast<ssize_t>(bytes.size()))_exit(4);
        operations.block=true;
        try{
          auto event=session.Step();if(!event)_exit(5);
          auto terminal=result.Accept(*event);if(!terminal)_exit(6);
          operation.Confirm(std::make_shared<const std::string>(std::move(terminal->response)));
        }catch(const Error&){result.LoseSession();operation.Uncertain();}
      });
      dispatcher.ExecuteManaged(owner,1,NoCallback,&callbacks);
      auto ready=operations.entered.get_future();if(!Ready(ready))_exit(7);
      auto before=std::chrono::steady_clock::now();
      if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending || callbacks!=0)_exit(8);
      if(std::chrono::steady_clock::now()-before>200ms || !owner->Cancelled())_exit(9);
      if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending)_exit(10);
      operations.release.set_value();
      for(int i=0;i<2000 && !owner->PollCleanup().quiescent;++i)std::this_thread::sleep_for(1ms);
      if(!owner->PollCleanup().quiescent)_exit(11);
      if(fail){
        // Failed fsync leaves state.next: startup rejects, never repairs it here.
        if(dispatcher.Close(10ms)!=Dispatcher::CloseResult::kIoPending)_exit(12);
        try{BrokerJournal reopened(dir,geteuid());_exit(13);}catch(const Error&){}
        std::ifstream input(root_+"/state.json");Json state;input>>state;
        if(state["jobs"].size()!=1 || !std::filesystem::exists(root_+"/state.next"))_exit(14);
      }else {
        BrokerJournal reopened(dir,geteuid()); // destroyed on coordinator
        if(!reopened.Blocked() || !reopened.Reservations().empty() ||
           dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone)_exit(16);
      }
      if(callbacks!=0)_exit(15);
      _exit(0);
    }()),testing::ExitedWithCode(0),"");
    // Each subprocess uses this fixture's owned root; the failed generation is
    // intentionally retained there until ordinary parent fixture cleanup.
    std::filesystem::remove(root_+"/state.next");std::filesystem::remove(root_+"/state.json");
    std::filesystem::remove(root_+"/lock");
  }
}
TEST(ManagedDispatcher, RetainedOwnerDestructionRunsOutsideDispatcherMutex) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher dispatcher;std::atomic<bool> destroyed=false;std::atomic<int> calls=0;
    class Owner final:public ManagedOperation {
     public:Owner(Dispatcher& dispatcher,std::atomic<bool>& destroyed):dispatcher_(dispatcher),destroyed_(destroyed){}
      ~Owner(){try{dispatcher_.CheckAdmission();}catch(const Error&){}destroyed_=true;}
     private:void Coordinate()override{Publish(Cleanup::kConfirmedComplete);}
      Dispatcher& dispatcher_;std::atomic<bool>& destroyed_;
    };
    dispatcher.ExecuteManaged(std::make_shared<Owner>(dispatcher,destroyed),1,NoCallback,&calls);
    if(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone || !destroyed || calls!=0)_exit(2);
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, ConcurrentPublicationAndCloseNeverDowngradeObservedProof) {
  ASSERT_EXIT(([]{
    alarm(8);
    for(int iteration=0;iteration<32;++iteration) {
      Dispatcher dispatcher;std::atomic<int> calls=0;
      auto owner=std::make_shared<ScriptOperation>([](auto& operation){
        operation.Confirm();std::this_thread::yield();
        operation.Confirm(std::make_shared<const std::string>(native));
      });
      dispatcher.ExecuteManaged(owner,1,NoCallback,&calls);
      while(dispatcher.Close(1s)!=Dispatcher::CloseResult::kDone){}
      if(!owner->PollCleanup().quiescent)_exit(2);
    }
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, UnconfirmedQuiescentOwnersKeepGlobalCapacityAndCannotInferProof) {
  ASSERT_EXIT(([]{
    alarm(5);Dispatcher first,second,third;std::atomic<int> calls=0;
    auto make=[] {return std::make_shared<ScriptOperation>([](auto& operation){
      operation.Uncertain();try{operation.Confirm();_exit(2);}catch(const Error&){}
    });};
    first.ExecuteManaged(make(),1,NoCallback,&calls);first.ExecuteManaged(make(),2,NoCallback,&calls);
    second.ExecuteManaged(make(),3,NoCallback,&calls);second.ExecuteManaged(make(),4,NoCallback,&calls);
    if(first.Close(10ms)!=Dispatcher::CloseResult::kIoPending)_exit(3);
    try{third.ExecuteManaged(make(),5,NoCallback,&calls);_exit(4);}catch(const Error& e){if(e.code()!=ErrorCode::kLimit)_exit(5);}
    if(calls!=0 || first.Close(10ms)!=Dispatcher::CloseResult::kIoPending)_exit(6);
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
TEST(ManagedDispatcher, InsertionAllocationFailureReleasesOwnerOutsideLockAndRollsBackCapacity) {
  ASSERT_EXIT(([]{
    alarm(5);
    struct Fault:DispatcherOperations {bool fail=true;void BeforeInsert()override{if(fail){fail=false;throw std::bad_alloc();}}} fault;
    Dispatcher first(1,&fault),second,third;std::atomic<bool> destroyed=false;std::atomic<int> calls=0;
    class Owner final:public ManagedOperation {
     public:Owner(Dispatcher& dispatcher,std::atomic<bool>& destroyed):dispatcher_(dispatcher),destroyed_(destroyed){}
      ~Owner(){dispatcher_.CheckAdmission();if(ClientToken())_exit(2);destroyed_=true;}
     private:void Coordinate()override{_exit(3);}
      Dispatcher& dispatcher_;std::atomic<bool>& destroyed_;
    };
    uint64_t token=0;
    try{token=first.ExecuteManaged(std::make_shared<Owner>(first,destroyed),1,NoCallback,&calls);_exit(4);}
    catch(const std::bad_alloc&){}
    if(token || !destroyed)_exit(5);
    auto work=[](const auto& cancelled,const auto&){while(!cancelled)std::this_thread::sleep_for(1ms);return std::string();};
    if(first.Execute(work,1,NoCallback,&calls)!=1)_exit(6);
    first.Execute(work,2,NoCallback,&calls);second.Execute(work,3,NoCallback,&calls);second.Execute(work,4,NoCallback,&calls);
    try{third.Execute(work,5,NoCallback,&calls);_exit(7);}catch(const Error& e){if(e.code()!=ErrorCode::kLimit)_exit(8);}
    if(first.Close(1s)!=Dispatcher::CloseResult::kDone || second.Close(1s)!=Dispatcher::CloseResult::kDone)_exit(9);
    _exit(0);
  }()),testing::ExitedWithCode(0),"");
}
