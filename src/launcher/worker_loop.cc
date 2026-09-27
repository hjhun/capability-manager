// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_loop.hh"
#include "launcher/runner.hh"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
namespace capmgr {
namespace {
struct Fd {
  int value=-1;
  Fd()=default;
  Fd(const Fd&)=delete;
  Fd& operator=(const Fd&)=delete;
  ~Fd(){Reset();}
  void Reset(int next=-1) noexcept {if(value>=0)close(value);value=next;}
};
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Nonblocking(int fd){int flags=fcntl(fd,F_GETFL);Check(flags>=0 && fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0,"worker nonblocking FD");}
void Pipe(Fd& read,Fd& write){int fds[2];Check(pipe2(fds,O_CLOEXEC)==0,"worker pipe");read.Reset(fds[0]);write.Reset(fds[1]);}
int Duplicate(int fd){int value=fcntl(fd,F_DUPFD_CLOEXEC,3);Check(value>=0,"worker duplicate FD");return value;}
bool Alive(int anchor) noexcept {
  int fd=openat(anchor,"stat",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return false;
  char data[4096];ssize_t size=read(fd,data,sizeof(data));close(fd);
  if(size<=0 || size>=static_cast<ssize_t>(sizeof(data)))return false;
  data[size]=0;char* end=strrchr(data,')');
  return end && end[1]==' ' && end[2] && end[2]!='Z' && end[2]!='X' && end[2]!='x';
}
void Put(uint8_t* out,uint64_t value,size_t size){for(size_t i=0;i<size;++i)out[i]=static_cast<uint8_t>(value>>(8*i));}
constexpr size_t kStack=1024*1024,kHeader=56,kChunk=4096,kQueue=128;
}
WorkerRegistry::WorkerRegistry(std::vector<RegisteredCli> entries):entries_(std::move(entries)) {
  Check(entries_.size()<=256,"worker registry limit");
  for(size_t i=0;i<entries_.size();++i) {
    const auto& entry=entries_[i];
    Check(entry.id.starts_with("cli:") && entry.id.size()>4 && entry.id.size()<=1024 &&
          entry.id.find('\0')==std::string::npos && !entry.executable.empty() && entry.executable[0]=='/' &&
          entry.executable.size()<4096 && entry.executable.find('\0')==std::string::npos,"worker registry entry");
    for(size_t k=0;k<i;++k)Check(entries_[k].id!=entry.id,"duplicate worker registry ID");
  }
}
std::string_view WorkerRegistry::Resolve(std::string_view id) const noexcept {
  for(const auto& entry:entries_)if(entry.id==id)return entry.executable;
  return {};
}
pid_t WorkerRuntime::Spawn(NamespaceInitConfig& config,void* stack_top) noexcept {
  return clone(NamespaceInit,stack_top,CLONE_NEWPID|CLONE_NEWNS|SIGCHLD,&config);
}
struct WorkerLoop::Impl {
  struct Record {std::array<uint8_t,kHeader+kChunk> bytes{};size_t size=0;};
  struct Job {
    uint64_t token=0,child=0;
    std::string executable,request;
    Fd go,status,out,err;
    std::array<uint8_t,sizeof(InitMessage)> partial{};
    size_t used=0,output=0;
    bool ready=false,go_sent=false,started=false,exited=false,status_eof=false,
         out_eof=false,err_eof=false,gone=false;
    WorkerFailure failure=WorkerFailure::None;
    int error=0,code=-1,signal=0;
    Clock::time_point admitted;
  };
  const WorkerRegistry registry;
  WorkerRuntime& runtime;
  WorkerContext context;
  WorkerLimits limits;
  WorkerCommandReader commands,cancels;
  Fd reply,parent,self,mount;
  OwnedChildren children;
  std::array<Job,4> jobs{};
  std::array<uint64_t,4> precancel{};
  std::array<Record,kQueue> queue{};
  size_t head=0,count=0,offset=0;
  uint64_t sequence=1,last_token=0;
  bool open=true,lost=false;
  Impl(const WorkerContext& c,const WorkerRegistry& catalog,WorkerRuntime& r,WorkerLimits l)
      :registry(catalog),runtime(r),context(c),limits(l),commands(c.command_read,c.generation),cancels(c.cancel_read,c.generation,true) {
    Check(c.generation && c.uid && c.gid && !c.smack_label.empty() && c.smack_label.size()<256 &&
          c.smack_label.find('\0')==std::string::npos,"worker fixed context");
    Check(l.runtime.count()>0 && l.runtime<=std::chrono::seconds(30) && l.setup.count()>0 &&
          l.setup<=std::chrono::seconds(5) && l.output_bytes && l.output_bytes<=1024*1024,"worker limits");
    struct sigaction action{};
    Check(sigaction(SIGPIPE,nullptr,&action)==0 && action.sa_handler==SIG_IGN,"worker must ignore SIGPIPE");
    Check(sigaction(SIGCHLD,nullptr,&action)==0 && action.sa_handler==SIG_DFL && !(action.sa_flags&SA_NOCLDWAIT),"worker exclusive child ownership");
    reply.Reset(Duplicate(c.reply_write));parent.Reset(Duplicate(c.parent_process));
    struct stat info{};int flags=fcntl(reply.value,F_GETFL);
    Check(fstat(reply.value,&info)==0 && S_ISFIFO(info.st_mode) && flags>=0 && (flags&O_ACCMODE)==O_WRONLY,"worker reply pipe");
    Nonblocking(reply.value);Check(Alive(parent.value),"worker parent anchor");
    self.Reset(::open("/proc/self",O_RDONLY|O_DIRECTORY|O_CLOEXEC));
    mount.Reset(::open("/proc/self/ns/mnt",O_RDONLY|O_CLOEXEC));
    Check(self.value>=3 && mount.value>=3,"worker creator anchors");
  }
  void Fail(Job& j,WorkerFailure cause,int error=0) noexcept {
    if(j.failure==WorkerFailure::None){j.failure=cause;j.error=error;}
    j.go.Reset();
  }
  void Close(WorkerFailure cause) noexcept {
    open=false;for(auto& j:jobs)if(j.token)Fail(j,cause);
  }
  bool Send(WorkerReplyKind kind,uint64_t token,WorkerFailure failure=WorkerFailure::None,
            int code=-1,int signal=0,int error=0,const void* data=nullptr,size_t size=0) noexcept {
    if(lost)return false;
    if(count==kQueue || !sequence || size>kChunk){Close(WorkerFailure::Backpressure);return false;}
    auto& record=queue[(head+count)%kQueue];record.bytes.fill(0);auto* b=record.bytes.data();
    std::memcpy(b,"CWR1",4);Put(b+4,1,2);Put(b+6,static_cast<uint16_t>(kind),2);
    Put(b+8,context.generation,8);Put(b+16,sequence,8);Put(b+24,token,8);Put(b+32,size,4);
    Put(b+36,static_cast<uint32_t>(failure),4);Put(b+40,static_cast<uint32_t>(code),4);
    Put(b+44,static_cast<uint32_t>(signal),4);Put(b+48,static_cast<uint32_t>(error),4);
    if(size)std::memcpy(b+kHeader,data,size);
    record.size=kHeader+size;++count;
    sequence=sequence==UINT64_MAX?0:sequence+1;return true;
  }
  void Flush() noexcept {
    if(lost || !count)return;
    auto& front=queue[head];ssize_t n=write(reply.value,front.bytes.data()+offset,front.size-offset);
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
    if(n<=0){lost=true;count=offset=0;Close(WorkerFailure::Channel);return;}
    offset+=static_cast<size_t>(n);
    if(offset==front.size){head=(head+1)%kQueue;--count;offset=0;}
  }
  Job* Find(uint64_t token) noexcept {for(auto& j:jobs)if(j.token==token)return &j;return nullptr;}
  void Cancel(uint64_t token) noexcept {
    if(auto* job=Find(token)){Fail(*job,WorkerFailure::Cancelled);return;}
    // A priority cancel may overtake a fragmented START on the other pipe.
    if(token>last_token) {
      for(auto v:precancel)if(v==token)return;
      for(auto& v:precancel)if(!v){v=token;return;}
      Close(WorkerFailure::Protocol);return;
    }
    Send(WorkerReplyKind::State,token,WorkerFailure::Rejected,-1,0,ENOENT);
  }
  void Start(WorkerCommand& command,Clock::time_point now) {
    if(command.token<=last_token)throw std::runtime_error("worker duplicate/reordered token");
    last_token=command.token;
    bool cancelled=false;
    // Monotonic START makes every older future-cancel tombstone obsolete too.
    // Consume before capacity rejection; rejected no-child tokens cannot leak it.
    for(auto& v:precancel)if(v && v<=command.token){cancelled|=v==command.token;v=0;}
    Job* slot=nullptr;for(auto& j:jobs)if(!j.token){slot=&j;break;}
    // Capacity rejection is proof of no clone for this token, not all-job failure.
    if(!slot){Send(WorkerReplyKind::Complete,command.token,cancelled?WorkerFailure::Cancelled:WorkerFailure::Rejected,-1,0,EBUSY);return;}
    auto& j=*slot;j.token=command.token;j.admitted=now;
    if(cancelled){j.gone=true;Fail(j,WorkerFailure::Cancelled);return;}
    try {
      auto request=ParseRequest(command.request);j.executable=registry.Resolve(request.capability_id);
      Check(!j.executable.empty() && j.executable.front()=='/' && j.executable.size()<4096 && j.executable.find('\0')==std::string::npos,"worker catalog executable");
      j.request=std::move(command.request);
      Fd control_read,status_write,out_write,err_write;
      Pipe(control_read,j.go);Pipe(j.status,status_write);Pipe(j.out,out_write);Pipe(j.err,err_write);
      Nonblocking(j.go.value);Nonblocking(j.status.value);Nonblocking(j.out.value);Nonblocking(j.err.value);
      void* stack=mmap(nullptr,kStack,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK,-1,0);
      Check(stack!=MAP_FAILED,"worker clone stack");
      uint64_t id;
      try {id=children.Reserve();}catch(...){munmap(stack,kStack);throw;}
      if(!Alive(parent.value)) {
        children.AbandonUnspawned(id);munmap(stack,kStack);j.gone=true;Close(WorkerFailure::ParentLost);return;
      }
      NamespaceInitConfig config{context.uid,context.gid,context.smack_label.c_str(),j.executable.c_str(),j.request.c_str(),
        control_read.value,status_write.value,out_write.value,err_write.value,self.value,mount.value};
      pid_t pid=runtime.Spawn(config,static_cast<char*>(stack)+kStack);
      if(pid>0)children.AttachReserved(id,pid);
      else children.AbandonUnspawned(id);
      int error=errno;munmap(stack,kStack);
      if(pid<=0){j.gone=true;Fail(j,WorkerFailure::Clone,error);return;}
      j.child=id;Send(WorkerReplyKind::Accepted,j.token);
    } catch(...) {
      // All throwing setup precedes clone/attach. Never erase an owned child.
      if(!j.child)j.gone=true;
      Fail(j,WorkerFailure::Rejected,EINVAL);
    }
  }
  void Status(Job& j) noexcept {
    if(j.status_eof || j.status.value<0)return;
    ssize_t n=read(j.status.value,j.partial.data()+j.used,j.partial.size()-j.used);
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
    if(n<=0){j.status_eof=true;if(n<0 || j.used || (!j.exited && j.failure==WorkerFailure::None))Fail(j,WorkerFailure::Protocol,EIO);return;}
    j.used+=static_cast<size_t>(n);if(j.used<j.partial.size())return;
    InitMessage message;std::memcpy(&message,j.partial.data(),sizeof(message));j.used=0;
    if(message.kind==InitMessageKind::Ready && !j.ready && !j.started && !j.exited && message.stage==InitStage::Go && !message.error)j.ready=true;
    else if(message.kind==InitMessageKind::Started && j.go_sent && !j.started && !j.exited && message.stage==InitStage::Exec && !message.error)j.started=true;
    else if(message.kind==InitMessageKind::Exited && j.started && !j.exited && message.stage==InitStage::Wait && !message.error) {
      j.exited=true;j.code=message.code;j.signal=message.signal;
    } else if(message.kind==InitMessageKind::Failed && !j.exited && message.error>0)Fail(j,WorkerFailure::Setup,message.error);
    else Fail(j,WorkerFailure::Protocol,EPROTO);
  }
  void Output(Job& j,Fd& fd,bool& eof,WorkerReplyKind kind) noexcept {
    if(eof || fd.value<0)return;
    std::array<uint8_t,kChunk> bytes;ssize_t n=read(fd.value,bytes.data(),bytes.size());
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
    if(n<=0){eof=true;if(n<0)Fail(j,WorkerFailure::Channel,errno);return;}
    size_t size=static_cast<size_t>(n);
    if(size>limits.output_bytes-j.output){Fail(j,WorkerFailure::OutputLimit,EOVERFLOW);return;}
    j.output+=size;
    if(j.failure==WorkerFailure::None && !Send(kind,j.token,WorkerFailure::None,-1,0,0,bytes.data(),size))Fail(j,WorkerFailure::Backpressure);
  }
  void Jobs(Clock::time_point now,bool priority_clear) noexcept {
    for(auto& j:jobs)if(j.token) {
      if(j.child && !j.gone) {
        Status(j);Output(j,j.out,j.out_eof,WorkerReplyKind::Stdout);Output(j,j.err,j.err_eof,WorkerReplyKind::Stderr);
        if(j.failure==WorkerFailure::None && (now-j.admitted>=limits.runtime || (!j.started && now-j.admitted>=limits.setup)))Fail(j,WorkerFailure::Timeout,ETIMEDOUT);
        if(j.ready && !j.go_sent && j.failure==WorkerFailure::None && priority_clear) {
          if(!Alive(parent.value))Close(WorkerFailure::ParentLost);
          else {char go='G';ssize_t n=write(j.go.value,&go,1);
            if(n==1){j.go_sent=true;} // Keep the sole GO writer until cleanup; HUP means creator loss.
            else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)Fail(j,WorkerFailure::Channel,errno);}
        }
        auto status=j.failure==WorkerFailure::None?children.Inspect(j.child):children.Stop(j.child);
        if(status.state==ChildState::Uncertain)Close(WorkerFailure::Channel);
        if(status.state==ChildState::Complete){children.Release(j.child);j.child=0;j.gone=true;}
      }
      // Drain finite pipe data after confirmed namespace death; never wait for a
      // leaked output writer forever. Timeout converts it into explicit failure.
      if(j.gone && j.status.value>=0) {
        Status(j);Output(j,j.out,j.out_eof,WorkerReplyKind::Stdout);Output(j,j.err,j.err_eof,WorkerReplyKind::Stderr);
        if(now-j.admitted>=limits.runtime)Fail(j,WorkerFailure::Timeout,ETIMEDOUT);
      }
      bool streams=j.status.value<0 || (j.status_eof && j.out_eof && j.err_eof);
      if(j.gone && (streams || j.failure!=WorkerFailure::None || lost)) {
        if(j.failure==WorkerFailure::None && !j.exited)Fail(j,WorkerFailure::Protocol,EPROTO);
        if(lost || Send(WorkerReplyKind::Complete,j.token,j.failure,j.code,j.signal,j.error)) {
          j.go.Reset();j.status.Reset();j.out.Reset();j.err.Reset();j.token=0;j.child=0;
          // Fd is deliberately nonmovable; reconstruct only this now-unowned slot.
          j.~Job();new (&j) Job;
        }
      }
    }
  }
  void Step(Clock::time_point now) noexcept {
    bool priority_clear=false;
    try {
      if(!Alive(parent.value))Close(WorkerFailure::ParentLost);
      pollfd endpoints[]={{commands.fd(),POLLIN,0},{cancels.fd(),POLLIN,0},{reply.value,POLLOUT,0}};
      int polled=poll(endpoints,3,0);
      if(polled<0 && errno!=EINTR)Close(WorkerFailure::Channel);
      for(auto& endpoint:endpoints)if(endpoint.revents&(POLLHUP|POLLERR|POLLNVAL))Close(WorkerFailure::Channel);
      if(endpoints[2].revents&(POLLHUP|POLLERR|POLLNVAL)){lost=true;count=offset=0;}
      if(open) {
        for(int i=0;i<4;++i){auto command=cancels.ReadOne(now);if(command)Cancel(command->token);else break;}
        pollfd priority{cancels.fd(),POLLIN,0};int n=poll(&priority,1,0);
        priority_clear=n==0 && !cancels.Incomplete();
        if(n<0 && errno!=EINTR)Close(WorkerFailure::Channel);
        if(open && priority_clear) {
          auto command=commands.ReadOne(now);
          if(command) {
            if(command->kind==WorkerCommandKind::Start)Start(*command,now);
            else {auto* job=Find(command->token);Send(WorkerReplyKind::State,command->token,job?job->failure:WorkerFailure::Rejected,-1,0,job?0:ENOENT);}
          }
        }
      }
    } catch(...) {Close(WorkerFailure::Protocol);}
    Jobs(now,open && priority_clear);Flush();
  }
};
WorkerLoop::WorkerLoop(const WorkerContext& c,const WorkerRegistry& registry,WorkerRuntime& r,WorkerLimits l):impl_(std::make_unique<Impl>(c,registry,r,l)){}
WorkerLoop::~WorkerLoop()=default;
void WorkerLoop::Step(Clock::time_point now) noexcept {impl_->Step(now);}
void WorkerLoop::Shutdown() noexcept {impl_->Close(WorkerFailure::ParentLost);}
bool WorkerLoop::AdmissionOpen() const noexcept {return impl_->open;}
bool WorkerLoop::Quiescent() const noexcept {return !impl_->open && impl_->children.Size()==0;}
bool WorkerLoop::CanExitCleanly() const noexcept {return Quiescent() && Jobs()==0 && !impl_->lost && impl_->count==0 && impl_->offset==0;}
std::array<int,6> WorkerLoop::StartupDescriptors() const {
  Check(impl_->open && !impl_->last_token && !Jobs(),"worker descriptor witness after admission");
  return {impl_->commands.fd(),impl_->cancels.fd(),impl_->reply.value,impl_->parent.value,impl_->self.value,impl_->mount.value};
}
bool WorkerLoop::DeliveryLost() const noexcept {return impl_->lost;}
size_t WorkerLoop::Jobs() const noexcept {size_t n=0;for(auto& j:impl_->jobs)if(j.token)++n;return n;}
}
