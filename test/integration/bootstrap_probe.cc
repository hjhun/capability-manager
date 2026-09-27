// SPDX-License-Identifier: Apache-2.0
// Explicit root development fixture; no unit, global policy or operational DB.
#include "catalog/catalog.hh"
#include "trusted_fixture.hh"
#include "launcher/worker_spawn.hh"
#include "launcher/worker_supervisor.hh"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Pipe {
  int fd[2]{-1,-1};Pipe(){Check(!pipe2(fd,O_CLOEXEC),"pipe");}
  ~Pipe(){for(int value:fd)if(value>=0)close(value);}
  void Close(int side){if(fd[side]>=0)close(fd[side]);fd[side]=-1;}
};
struct Scope {
  char path[64]="/opt/usr/capmgr-bootstrap-fixture-XXXXXX";bool uncertain=false,done=false;int anchor=-1;struct stat identity{};
  Scope(){fixture::TrustedPath("/opt/usr");Check(mkdtemp(path),"scope");std::cout<<"OWNED_SCOPE="<<path<<std::endl;
    anchor=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(anchor<0 || fstat(anchor,&identity)){if(anchor>=0)close(anchor);std::cerr<<"RETAINED_SCOPE="<<path<<std::endl;throw std::runtime_error("scope anchor");}}

  ~Scope(){if(!done){if(uncertain)std::cerr<<"RETAINED_SCOPE="<<path<<" namespace cleanup unconfirmed\n";else {try{Cleanup();}catch(...){}}}if(anchor>=0)close(anchor);}
  void Cleanup(){if(done)return;
    try{fixture::TrustedPath("/opt/usr");fixture::TrustedPath(path);struct stat current{},held{};
      Check(!lstat(path,&current) && !fstat(anchor,&held) && current.st_dev==identity.st_dev && current.st_ino==identity.st_ino &&
        held.st_dev==identity.st_dev && held.st_ino==identity.st_ino && (current.st_mode&07777)==0700,"scope identity/mode changed");}
    catch(...){done=true;std::cerr<<"RETAINED_SCOPE="<<path<<" unsafe cleanup path"<<std::endl;throw;}
    std::error_code error;std::filesystem::remove_all(path,error);done=true;
    if(error){std::cerr<<"RETAINED_SCOPE="<<path<<" cleanup_error="<<error.value()<<std::endl;throw std::runtime_error("scope cleanup");}
    std::cout<<"REMOVED_SCOPE="<<path<<std::endl;}
};
struct Fd {int value=-1;~Fd(){if(value>=0)close(value);}};
struct Child {
  OwnedChildren children{1};uint64_t token=0;
  ~Child(){if(token && children.Size()){auto status=children.StopAndWait(token,5s);
    if(status.state!=ChildState::Complete){std::cerr<<"RETAINED_SCOPE child not reaped\n"<<std::flush;std::terminate();}
    children.Release(token);}}
};
void Run(const std::string& mode) {
  Scope scope;std::string catalog=std::string(scope.path)+"/catalog",journal_path=std::string(scope.path)+"/journal";
  Check(!mkdir(catalog.c_str(),0700) && !mkdir(journal_path.c_str(),0700),"private directories");
  {
    Catalog writer(catalog+"/catalog.db",Database::Access::kWriter);
    Entry entry;entry.kind=Kind::kCli;entry.owner="fixture";entry.key="fixture";entry.name="Fixture";entry.id="cli:fixture";entry.executable="/usr/bin/true";entry.detail=Json::object();
    writer.Stage("fixture","fixture",{entry});writer.Finalize("fixture",true);
    for(auto suffix:{"","-wal","-shm"})Check(!chmod((catalog+"/catalog.db"+suffix).c_str(),0600),"catalog mode");
    if(mode=="unsafe-catalog")Check(!chmod((catalog+"/catalog.db").c_str(),0604),"unsafe fixture mode");
    std::ofstream(journal_path+"/state.json")<<R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})";
    Check(!chmod((journal_path+"/state.json").c_str(),0600),"journal mode");
    Fd journal_fd{open(journal_path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC)},catalog_fd{open(catalog.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC)};
    const char* parent_path=mode=="wrong-parent"?"/proc/1":mode=="wrong-directory"?"/tmp":"/proc/self";
    Fd parent{open(parent_path,O_RDONLY|O_DIRECTORY|O_CLOEXEC)};
    Check(parent.value>=0 && journal_fd.value>=0 && catalog_fd.value>=0,"fixture directories");
    BrokerJournal journal(journal_fd.value,0);Pipe command,cancel,reply,ready;Child child;
    auto session=std::make_unique<WorkerSession>(journal,command.fd[1],cancel.fd[1],reply.fd[0]);
    command.Close(1);cancel.Close(1);reply.Close(0);
    child.token=SpawnFixedWorker(child.children,journal.Generation(),{command.fd[0],cancel.fd[0],reply.fd[1],parent.value,catalog_fd.value,ready.fd[1]});
    command.Close(0);cancel.Close(0);reply.Close(1);if(mode!="retained-ready")ready.Close(1);
    WorkerSupervisor supervisor(std::move(session),child.children,child.token,ready.fd[0]);ready.Close(0);
    bool rejected=mode=="wrong-parent" || mode=="wrong-directory" || mode=="unsafe-catalog" || mode=="retained-ready";
    auto deadline=WorkerSupervisor::Clock::now()+7s;bool started=false,failed=false;
    try{while(!started && WorkerSupervisor::Clock::now()<deadline){started=supervisor.PollStartup();if(!started)usleep(1000);}}
    catch(const std::exception& e){failed=true;std::cout<<"STARTUP_REJECTED mode="<<mode<<" reason="<<e.what()<<std::endl;}
    if(rejected){Check(failed && !started && journal.Blocked() && journal.Reservations().empty(),"expected startup rejection");}
    else {
      Check(started && supervisor.CatalogRevision()==1,"actual READY/revision");
      if(mode=="run") {
        scope.uncertain=true;auto token=supervisor.Start(R"({"jsonrpc":"2.0","id":"bootstrap","method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})");
        bool completed=false;deadline=WorkerSupervisor::Clock::now()+10s;
        while(!completed && WorkerSupervisor::Clock::now()<deadline){auto event=supervisor.Step();
          if(event && event->kind==WorkerReplyKind::Complete && event->token==token){
            // Complete already persisted child absence; result outcome is separate.
            scope.uncertain=false;completed=true;Check(event->failure==WorkerFailure::None && event->code==0 && event->signal==0,"namespace workload result");}
          usleep(1000);}
        Check(completed && journal.Reservations().empty(),"durable namespace completion");
      }
      supervisor.PrepareStop();bool clean=false;deadline=WorkerSupervisor::Clock::now()+5s;
      while(!clean && WorkerSupervisor::Clock::now()<deadline){supervisor.Step();clean=supervisor.ConfirmNormalExit();if(!clean)usleep(1000);}
      Check(clean && !journal.Blocked() && child.children.Size()==0,"clean owned worker exit");
    }
  }
  scope.Cleanup();std::cout<<"BOOTSTRAP_CASE_PASS mode="<<mode<<std::endl;
}
}
int main(int argc,char** argv) {
  if(argc!=2 || std::string(argv[1])!="--run-root-fixture")return 2;
  try {Check(getuid()==0 && geteuid()==0,"root fixture only");umask(0077);
    fixture::TrustedPath(std::filesystem::read_symlink("/proc/self/exe").string(),true);
    fixture::TrustedPath(CAPMGR_WORKER_IMAGE,true);fixture::TrustedPath("/opt/usr");
    struct sigaction action{};action.sa_handler=SIG_IGN;Check(!sigaction(SIGPIPE,&action,nullptr),"SIGPIPE");action.sa_handler=SIG_DFL;Check(!sigaction(SIGCHLD,&action,nullptr),"SIGCHLD");
    for(const char* mode:{"wrong-parent","wrong-directory","unsafe-catalog","retained-ready","idle","run"})Run(mode);
    std::cout<<"BOOTSTRAP_FIXTURE_PASS\n";return 0;
  }catch(const std::exception& e){std::cerr<<"BOOTSTRAP_FIXTURE_FAIL "<<e.what()<<std::endl;return 1;}
}
