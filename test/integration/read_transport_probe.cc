// SPDX-License-Identifier: Apache-2.0
// Explicit root development-image fixture. No global policy or production DB.
#include "trusted_fixture.hh"
#include "api/client.hh"
#include "platform/tidl_read_channel.hh"
#include "platform/tidl_read_service.hh"
#include "catalog/catalog.hh"
#include "launcher/owned_children.hh"
#include "capability_manager_proxy.h"
#include <rpc-port-internal.h>
#include <fcntl.h>
#include <spawn.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <set>
#include <thread>
extern char** environ;
using namespace capmgr;
using namespace std::chrono_literals;
using Stub=rpc_port::capability_manager_stub::stub::CapabilityManager;
using Proxy=rpc_port::capability_manager_proxy::proxy::CapabilityManager;
namespace {
const char* stage="startup";
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string Self(){return std::filesystem::read_symlink("/proc/self/exe").string();}
struct Scope {
  char path[64]="/opt/usr/capmgr-read-fixture-XXXXXX";
  int anchor=-1;struct stat identity{};bool attempted=false;
  Scope(){fixture::TrustedPath("/opt/usr");Check(mkdtemp(path),"scope");std::cout<<"OWNED_SCOPE="<<path<<std::endl;
    anchor=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(anchor<0 || fstat(anchor,&identity)){if(anchor>=0)close(anchor);
      std::cerr<<"RETAINED_SCOPE="<<path<<std::endl;throw std::runtime_error("scope anchor");}}
  ~Scope(){if(!attempted){try{Cleanup();}catch(...){}}if(anchor>=0)close(anchor);}
  void Cleanup(){if(attempted)return;attempted=true;
    try{fixture::TrustedPath(path);struct stat current{},held{};
      Check(!lstat(path,&current) && !fstat(anchor,&held) && current.st_dev==identity.st_dev && current.st_ino==identity.st_ino &&
        held.st_dev==identity.st_dev && held.st_ino==identity.st_ino && (current.st_mode&07777)==0700,"scope changed");
      std::error_code error;std::filesystem::remove_all(path,error);Check(!error,"scope deletion failed");
      std::cout<<"REMOVED_SCOPE="<<path<<std::endl;
    }catch(...){std::cerr<<"RETAINED_SCOPE="<<path<<std::endl;throw;}}
};
std::string Label(const std::string& path) {
  int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);Check(fd>=0,"label open");
  try{ReadLeaseOperations op;auto label=op.Label(fd);close(fd);return label;}catch(...){close(fd);throw;}
}
ReadLeasePolicy Policy(const std::string& root) {
  fixture::TrustedPath(root);auto dir=root+"/catalog",lock=root+"/lease";
  return {dir,lock,0,0,0,0,0700,0600,0600,Label(dir),Label(dir+"/catalog.db"),Label(lock)};
}
void Initialize(const std::string& root) {
  Check(!mkdir((root+"/catalog").c_str(),0700),"catalog directory");
  int fd=open((root+"/lease").c_str(),O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);Check(fd>=0,"lease creation");close(fd);
  Catalog writer(root+"/catalog/catalog.db",Database::Access::kWriter);
  Entry entry;entry.id="cli:fixture";entry.key=entry.name="fixture";entry.owner="pkg";entry.kind=Kind::kCli;
  entry.desc="Read admission fixture";entry.executable="/usr/bin/true";entry.detail=Json::object();
  writer.Stage("fixture","pkg",{entry});writer.Finalize("fixture",true);
  int persistent=1;Check(sqlite3_file_control(writer.database().handle(),"main",SQLITE_FCNTL_PERSIST_WAL,&persistent)==SQLITE_OK,"persistent WAL");
  for(const char* suffix:{"","-wal","-shm"})Check(!chmod((root+"/catalog/catalog.db"+suffix).c_str(),0600),"catalog mode");
}
struct Context {
  GMainContext* value=g_main_context_new();
  Context(){Check(value,"private context");g_main_context_push_thread_default(value);}
  ~Context(){g_main_context_pop_thread_default(value);g_main_context_unref(value);}
};
struct Listener:Proxy::IEventListener {
  bool connected=false;int calls=0;
  void OnConnected()override{connected=true;++calls;}
  void OnDisconnected()override{connected=false;++calls;}
  void OnRejected()override{connected=false;++calls;}
};
size_t Fds(){return static_cast<size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{}));}
void DropReplyTo(pid_t pid) {
  // Fixture-only: shut down connected socket objects in THIS server that belong
  // to this verified fixture client, including the split reply half. No foreign
  // process is signalled, no descriptor is closed/reused, and normal Stub teardown
  // still unlinks its own endpoint and deregisters the process.
  for(const auto& item:std::filesystem::directory_iterator("/proc/self/fd")) {
    int fd=std::stoi(item.path().filename().string());struct ucred peer{};socklen_t size=sizeof(peer);
    if(!getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size) && size==sizeof(peer) && peer.pid==pid)
      shutdown(fd,SHUT_RDWR);
  }
}
struct Registration {
  bool active=false;
  explicit Registration(const std::string& endpoint) {
    Check(!rpc_port_register_proc_info(endpoint.c_str(),nullptr),"process registration");active=true;
  }
  ~Registration(){if(active && rpc_port_deregister_proc_info())std::cerr<<"PROC_DEREGISTER_FAILED\n";}
  void Close(){Check(!rpc_port_deregister_proc_info(),"process deregistration");active=false;}
};
class Service:public TidlReadService {
 public:
  Service(std::string sender,std::string instance,ReadLeasePolicy policy,
          std::shared_ptr<CatalogGrantBudget> budget,std::string mode)
      :TidlReadService(std::move(sender),std::move(instance),std::move(policy),
                      {mode=="deny"?1u:0u,0,"User::Shell"},budget),budget_(std::move(budget)),mode_(std::move(mode)){}
  std::string AuthorizeCatalog()override {
    auto result=TidlReadService::AuthorizeCatalog();
    if(mode_=="malformed")return "CMG1:bad";
    if(mode_=="oversize")return std::string(4096,'x');
    if(mode_=="stalled"){
      std::this_thread::sleep_for(5100ms);
      if(budget_->Outstanding()!=1)_exit(77);
      std::cout<<"STALLED_CONTEXT_RETAINS_SLOT\n"<<std::flush;
    }
    return result;
  }
  int ConfirmCatalog(std::string nonce)override {
    int result=TidlReadService::ConfirmCatalog(std::move(nonce));
    if(mode_=="lost-reply" && result==0){
      std::cout<<"CONFIRM_CONSUMED_REPLY_LOST\n"<<std::flush;DropReplyTo(MainPrincipal()->pid());
    }
    return result;
  }
 private:std::shared_ptr<CatalogGrantBudget> budget_;std::string mode_;
};
class Factory:public Stub::ServiceBase::Factory {
 public:
  Factory(ReadLeasePolicy policy,std::string mode):policy_(std::move(policy)),mode_(std::move(mode)){}
  std::unique_ptr<Stub::ServiceBase> CreateService(std::string sender,std::string instance)override {
    return std::make_unique<Service>(std::move(sender),std::move(instance),policy_,budget,mode_);
  }
  std::shared_ptr<CatalogGrantBudget> budget=std::make_shared<CatalogGrantBudget>();
 private:ReadLeasePolicy policy_;std::string mode_;
};
int Server(const std::string& root,const std::string& endpoint,const std::string& mode) {
  stage="server register";Context context;Registration registration(endpoint);
  stage="server policy";
  auto factory=std::make_shared<Factory>(Policy(root),mode);
  GMainLoop* loop=g_main_loop_new(context.value,false);Check(loop,"server loop");
  {
    stage="server listen";Stub stub;stub.Listen(factory);
    struct Tick{std::string stop;GMainLoop* loop;Stub* stub;std::shared_ptr<Factory> factory;};
    Tick tick{root+"/stop",loop,&stub,factory};GSource* timer=g_timeout_source_new(100);
    g_source_set_callback(timer,[](gpointer data)->gboolean{
      auto& t=*static_cast<Tick*>(data);
      if(std::filesystem::exists(t.stop) && t.stub->GetServices().empty() && t.factory->budget->Outstanding()==0)
        g_main_loop_quit(t.loop);
      return G_SOURCE_CONTINUE;
    },&tick,nullptr);g_source_attach(timer,context.value);
    std::ofstream(root+"/ready")<<"ready\n";stage="server loop";g_main_loop_run(loop);
    g_source_destroy(timer);g_source_unref(timer);
    Check(stub.GetServices().empty() && factory->budget->Outstanding()==0,"service/timer/grant cleanup");
    std::cout<<"SERVER_GRANTS_SERVICES_DRAINED\n"<<std::flush;
  }
  g_main_loop_unref(loop);registration.Close();return 0;
}
void PublicClient(const std::string& root,const std::string& endpoint,const std::string& mode) {
  auto* previous=g_main_context_get_thread_default();const int loops=mode=="normal"?24:4;
  size_t baseline=0;
  for(int i=0;i<loops;++i) {
    {
      stage="channel construct/connect";TidlReadChannel channel(endpoint);
      stage="local handoff/create";LeasedCatalogGate gate(Policy(root),channel);
      capmgr_client_h client=nullptr;int result=CreateClient(gate,&client);
      if(result)std::cout<<"CREATE_RESULT="<<result<<" mode="<<mode<<std::endl;
      if(mode=="normal") {
        Check(result==0 && client,"public create");Check(g_main_context_get_thread_default()==previous,"context restored before publication");
        char* detail=nullptr;Check(capmgr_client_get_capability(client,"cli:fixture",&detail)==0 && detail,"local query");std::free(detail);
        Check(capmgr_client_destroy(client)==0,"public destroy");
      }else Check(result!=0 && !client,"negative create must remain NULL");
    }
    Check(g_main_context_get_thread_default()==previous,"context restored on all exits");
    if(mode!="normal")std::cout<<"NEGATIVE_ITERATION mode="<<mode<<" iteration="<<(i+1)<<" fds="<<Fds()<<std::endl;
    if(i==0)baseline=Fds();else Check(Fds()<=baseline,"client descriptor leak");
  }
  std::cout<<"PUBLIC_CLIENT_"<<mode<<"_PASS fd_count="<<Fds()<<std::endl;
}
void RawClient(const std::string& root,const std::string& endpoint) {
  Context context;Listener a,b;Proxy first(&a,endpoint),second(&b,endpoint);first.Connect(true);second.Connect(true);
  auto receipt=ParseCatalogGrant(first.AuthorizeCatalog());auto other=ParseCatalogGrant(second.AuthorizeCatalog());
  Check(receipt.descriptor==other.descriptor && receipt.nonce!=other.nonce,"independent service grants");
  auto local=std::make_unique<CatalogReadLease>(Policy(root));local->MatchDescriptor(receipt.descriptor);
  Check(second.ConfirmCatalog(receipt.nonce)!=0,"wrong-instance nonce");
  Check(first.ConfirmCatalog(receipt.nonce)==0,"same-instance nonce");Check(first.ConfirmCatalog(receipt.nonce)!=0,"replayed nonce");
  first.Disconnect();second.Disconnect();
  Listener idle_listener;Proxy idle(&idle_listener,endpoint);idle.Connect(true);
  auto expiring=ParseCatalogGrant(idle.AuthorizeCatalog());std::this_thread::sleep_for(5200ms);
  Check(idle.ConfirmCatalog(expiring.nonce)!=0,"expired nonce");idle.Disconnect();
  std::cout<<"RAW_INSTANCE_REPLAY_PASS\n"<<std::flush;
}
std::set<int> Sockets() {
  std::set<int> result;
  for(const auto& item:std::filesystem::directory_iterator("/proc/self/fd")) {
    int fd=std::stoi(item.path().filename().string());struct stat info{};
    if(!fstat(fd,&info) && S_ISSOCK(info.st_mode))result.insert(fd);
  }
  return result;
}
struct RawProxy {
  rpc_port_proxy_h proxy=nullptr;rpc_port_h main=nullptr;
  RawProxy(){Check(!rpc_port_proxy_create(&proxy),"raw proxy");}
  ~RawProxy(){if(proxy)rpc_port_proxy_destroy(proxy);}
};
struct Parcel {
  rpc_port_parcel_h value=nullptr;
  ~Parcel(){if(value)rpc_port_parcel_destroy(value);}
};
std::string RawAuthorize(rpc_port_h port) {
  Parcel request,response;Check(!rpc_port_parcel_create(&request.value),"raw parcel");
  Check(!rpc_port_parcel_write_int32(request.value,2) && !rpc_port_parcel_send(request.value,port),"raw authorize send");
  Check(!rpc_port_parcel_create_from_port(&response.value,port),"raw authorize receive");
  int method=-1;char* text=nullptr;
  Check(!rpc_port_parcel_read_int32(response.value,&method) && method==0 &&
        !rpc_port_parcel_read_string(response.value,&text) && text,"raw authorize result");
  std::unique_ptr<char,decltype(&std::free)> owned(text,&std::free);return text;
}
bool RawConfirmFails(rpc_port_h port,const std::string& nonce) {
  Parcel request,response;Check(!rpc_port_parcel_create(&request.value),"confirm parcel");
  Check(!rpc_port_parcel_write_int32(request.value,10) && !rpc_port_parcel_write_string(request.value,nonce.c_str()),"confirm encoding");
  if(rpc_port_parcel_send(request.value,port))return true;
  if(rpc_port_parcel_create_from_port(&response.value,port))return true;
  int method=-1,result=-1;
  return rpc_port_parcel_read_int32(response.value,&method) || method!=0 ||
         rpc_port_parcel_read_int32(response.value,&result) || result!=0;
}
void SplitSockets(const std::string& root,const std::string& endpoint) {
  for(size_t selected=0;selected<2;++selected) {
    Context context;RawProxy proxy;
    Check(!rpc_port_proxy_add_connected_event_cb(proxy.proxy,[](const char*,const char*,rpc_port_h port,void* data){
      static_cast<RawProxy*>(data)->main=port;
    },&proxy),"raw listener");
    auto before=Sockets();Check(!rpc_port_proxy_connect_sync(proxy.proxy,endpoint.c_str(),"CapabilityManager") && proxy.main,"raw connect");
    auto after=Sockets();std::vector<int> sockets;
    for(int fd:after)if(!before.contains(fd))sockets.push_back(fd);
    Check(sockets.size()==4,"target must exhibit four split sockets");
    rpc_port_h callback=nullptr;int main_read=-1,callback_read=-1;
    Check(!rpc_port_proxy_get_port(proxy.proxy,RPC_PORT_PORT_CALLBACK,&callback) && callback &&
          !rpc_port_get_read_fd(proxy.main,&main_read) && !rpc_port_get_read_fd(callback,&callback_read),"raw read FDs");
    Check(main_read!=callback_read && after.contains(main_read) && after.contains(callback_read),"distinct reads");
    std::vector<int> writes;for(int fd:sockets)if(fd!=main_read && fd!=callback_read)writes.push_back(fd);
    Check(writes.size()==2,"two separate write halves");
    auto receipt=ParseCatalogGrant(RawAuthorize(proxy.main));auto local=std::make_unique<CatalogReadLease>(Policy(root));
    local->MatchDescriptor(receipt.descriptor);Check(!shutdown(writes[selected],SHUT_RDWR),"write-half shutdown");
    pollfd read_fds[2]{{main_read,POLLRDHUP,0},{callback_read,POLLRDHUP,0}};
    Check(poll(read_fds,2,0)>=0,"read-half observation");
    std::cout<<"SPLIT_FDS sockets=4 reads=2 writes=2 removed_write="<<selected
             <<" read_revents="<<read_fds[0].revents<<","<<read_fds[1].revents<<std::endl;
    // Give server a bounded scheduling opportunity to remove the instance after
    // either write-half disconnect. Read-half polling is deliberately NOT proof.
    std::this_thread::sleep_for(100ms);
    Check(RawConfirmFails(proxy.main,receipt.nonce),"disconnected split write must not confirm");
  }
}
struct Child {
  OwnedChildren owned{1};uint64_t token=0;
  ~Child(){if(token){auto status=owned.StopAndWait(token,2s);
    if(status.state!=ChildState::Complete){std::cerr<<"RETAINED_SCOPE child cleanup unconfirmed\n"<<std::flush;std::terminate();}
    owned.Release(token);}}
  void Start(std::vector<std::string> args) {
    std::vector<char*> pointers;for(auto& arg:args)pointers.push_back(arg.data());pointers.push_back(nullptr);
    posix_spawn_file_actions_t actions;Check(!posix_spawn_file_actions_init(&actions),"spawn init");
    if(posix_spawn_file_actions_addclosefrom_np(&actions,3)){posix_spawn_file_actions_destroy(&actions);throw std::runtime_error("closefrom");}
    auto id=owned.Reserve();pid_t child=-1;int result=posix_spawn(&child,args[0].c_str(),&actions,nullptr,pointers.data(),environ);
    if(result==0)owned.AttachReserved(id,child);else owned.AbandonUnspawned(id);
    posix_spawn_file_actions_destroy(&actions);Check(result==0,"spawn");token=id;
  }
  void Wait(std::chrono::seconds budget=20s) {
    auto end=std::chrono::steady_clock::now()+budget;
    while(std::chrono::steady_clock::now()<end) {
      auto status=owned.Inspect(token);
      if(status.state==ChildState::Complete){owned.Release(token);token=0;Check(status.exit_code==0 && status.signal==0,"child failed");return;}
      std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("child deadline");
  }
};
void Run() {
  fixture::TrustedPath(Self(),true);Scope scope;Initialize(scope.path);
  int index=0;
  for(const std::string mode:{"normal","raw","split","deny","malformed","oversize","stalled","lost-reply"}) {
    auto endpoint="d::org.capmgr.readprobe."+std::to_string(getpid())+"."+std::to_string(++index);
    std::filesystem::remove(std::string(scope.path)+"/ready");std::filesystem::remove(std::string(scope.path)+"/stop");
    Child server;server.Start({Self(),"server",scope.path,endpoint,mode});
    auto end=std::chrono::steady_clock::now()+5s;
    while(!std::filesystem::exists(std::string(scope.path)+"/ready") && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(10ms);
    Check(std::filesystem::exists(std::string(scope.path)+"/ready"),"server ready deadline");
    const auto endpoint_path="/run/aul/rpcport/."+endpoint+"::CapabilityManager";
    try {
      stage="client start/wait";Child client;client.Start({Self(),"client",scope.path,endpoint,mode});client.Wait(45s);
    }catch(...) {
      // Let the server destroy Stub/unlink its endpoint before a forced stop.
      // Preserve the original failure; never count killed server as endpoint cleanup.
      auto failure=std::current_exception();std::ofstream(std::string(scope.path)+"/stop")<<"stop\n";
      try{server.Wait(5s);}catch(...){std::cerr<<"SERVER_DRAIN_FAILED endpoint="<<endpoint_path<<std::endl;}
      if(std::filesystem::exists(endpoint_path))std::cerr<<"RETAINED_ENDPOINT="<<endpoint_path<<std::endl;
      std::rethrow_exception(failure);
    }
    std::ofstream(std::string(scope.path)+"/stop")<<"stop\n";stage="server drain";server.Wait();
    Check(!std::filesystem::exists(endpoint_path),"server endpoint retained");
    std::cout<<"READ_TRANSPORT_CASE_PASS "<<mode<<std::endl;
  }
  scope.Cleanup();std::cout<<"READ_TRANSPORT_FIXTURE_PASS\n";
}
}
int main(int argc,char** argv) {
  try{
    Check(geteuid()==0,"root fixture only");fixture::TrustedPath(Self(),true);
    if(argc==1){Run();return 0;}
    Check(argc==5,"fixture arguments");std::string role=argv[1],root=argv[2],endpoint=argv[3],mode=argv[4];
    fixture::TrustedPath(root);
    if(role=="server")return Server(root,endpoint,mode);
    Check(role=="client","role");stage="client register";Registration registration(endpoint+".client");
    if(mode=="raw")RawClient(root,endpoint);
    else if(mode=="split")SplitSockets(root,endpoint);
    else PublicClient(root,endpoint,mode);
    registration.Close();return 0;
  }catch(const std::exception& error){std::cerr<<"READ_TRANSPORT_FAIL stage="<<stage<<" "<<error.what()<<std::endl;return 1;}
  catch(const rpc_port::capability_manager_proxy::proxy::PermissionDeniedException&){std::cerr<<"READ_TRANSPORT_FAIL stage="<<stage<<" native PermissionDenied\n";return 1;}
  catch(const rpc_port::capability_manager_proxy::proxy::InvalidIOException&){std::cerr<<"READ_TRANSPORT_FAIL stage="<<stage<<" native InvalidIO\n";return 1;}
  catch(const rpc_port::capability_manager_proxy::proxy::InvalidProtocolException&){std::cerr<<"READ_TRANSPORT_FAIL stage="<<stage<<" native InvalidProtocol\n";return 1;}
  catch(...){std::cerr<<"READ_TRANSPORT_FAIL stage="<<stage<<" native exception\n";return 1;}
}
