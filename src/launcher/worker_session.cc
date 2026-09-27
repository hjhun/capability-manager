// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_session.hh"
#include "common/error.hh"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
namespace capmgr {
namespace {
[[noreturn]] void Bad(const char* reason){throw Error(ErrorCode::kIo,reason);}
void Require(bool okay,const char* reason){if(!okay)Bad(reason);}
struct Fd {int value=-1;~Fd(){Close();}void Close() noexcept {if(value>=0)close(value);value=-1;}};
void Pipe(Fd& fd,int source,int direction) {
  fd.value=fcntl(source,F_DUPFD_CLOEXEC,3);struct stat st{};
  int flags=fd.value<0?-1:fcntl(fd.value,F_GETFL);
  Require(fd.value>=0 && !fstat(fd.value,&st) && S_ISFIFO(st.st_mode) && flags>=0 &&
          (flags&O_ACCMODE)==direction && !fcntl(fd.value,F_SETFL,flags|O_NONBLOCK),"Invalid frontend pipe");
}
uint64_t Get(const uint8_t* b,size_t size){uint64_t v=0;for(size_t i=0;i<size;++i)v|=uint64_t{b[i]}<<(i*8);return v;}
void Put(uint8_t* b,uint64_t v,size_t size){for(size_t i=0;i<size;++i)b[i]=static_cast<uint8_t>(v>>(i*8));}
uint64_t Next(uint64_t v){return v==UINT64_MAX?0:v+1;}
int Signed(uint64_t v){return v<=INT32_MAX?static_cast<int>(v):-1-static_cast<int>(UINT32_MAX-v);}
}
struct WorkerSession::Impl {
  struct Job {uint64_t token=0;std::vector<uint8_t> command;size_t sent=0,output=0;bool accepted=false,cancelled=false;Clock::time_point queued{};};
  struct Cancellation {std::array<uint8_t,40> bytes{};size_t offset=0;Clock::time_point queued{};};
  struct CancelReply {uint64_t token=0;bool sent=false;};
  std::array<CancelReply,4> cancel_replies{};
  BrokerJournal& journal;mutable std::mutex mutex;
  Fd commands,cancels,replies;
  uint64_t generation=0,command_sequence=1,cancel_sequence=1,reply_sequence=1;
  bool failed=false,stopping=false,finished=false,eof=false,closing=false;
  std::optional<Clock::time_point> closing_since;
  std::array<Job,4> jobs;
  std::array<Cancellation,4> priority{};size_t head=0,count=0;
  std::array<uint8_t,56+4096> reply{};size_t used=0,wanted=56;
  std::optional<Clock::time_point> partial;
  Impl(BrokerJournal& j,int command,int cancel,int response):journal(j) {
    struct sigaction action{};
    Require(!sigaction(SIGPIPE,nullptr,&action) && action.sa_handler==SIG_IGN,"Frontend requires ignored SIGPIPE");
    Pipe(commands,command,O_WRONLY);Pipe(cancels,cancel,O_WRONLY);Pipe(replies,response,O_RDONLY);
    Require(!journal.Blocked() && journal.Reservations().empty(),"Frontend journal not clean");
    generation=journal.BeginGeneration();
  }
  void Abort() noexcept {
    if(finished)return;
    failed=true;commands.Close();cancels.Close();replies.Close();
    try {journal.MarkUncertain();}catch(...) {} // active/partial disk state also blocks restart
  }
  void Active(){if(failed || stopping || closing || finished || journal.Blocked())throw Error(ErrorCode::kBusy,"Frontend admission closed");}
  Job* Find(uint64_t token){for(auto& j:jobs)if(j.token==token)return &j;return nullptr;}
  CancelReply* CancellationReply(uint64_t token){for(auto& c:cancel_replies)if(c.token==token)return &c;return nullptr;}
  void Write(int fd,const uint8_t* data,size_t size,size_t& offset) {
    ssize_t n=write(fd,data+offset,std::min<size_t>(size-offset,8192));
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
    if(n<=0)Bad("Frontend command write failed");
    offset+=static_cast<size_t>(n);
  }
  std::optional<WorkerEvent> Read(Clock::time_point now) {
    if(partial && now-*partial>=std::chrono::seconds(5))Bad("Frontend partial reply deadline");
    ssize_t n=read(replies.value,reply.data()+used,wanted-used);
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return {};
    if(n<0)Bad("Frontend reply read failure");
    if(!n) {
      Require(!used && journal.Reservations().empty(),"Frontend EOF without complete cleanup");
      eof=true;stopping=true;count=0;commands.Close();cancels.Close();return {};
    }
    if(!partial)partial=now;
    used+=static_cast<size_t>(n);if(used<wanted)return {};
    const auto* b=reply.data();
    if(wanted==56) {
      Require(!std::memcmp(b,"CWR1",4) && Get(b+4,2)==1 && Get(b+8,8)==generation &&
        reply_sequence && Get(b+16,8)==reply_sequence && Get(b+24,8) && !Get(b+52,4),"Invalid frontend reply header");
      auto size=Get(b+32,4);Require(size<=4096,"Frontend reply too large");
      wanted+=static_cast<size_t>(size);if(used<wanted)return {};
    }
    auto kind=static_cast<WorkerReplyKind>(Get(b+6,2));auto failure=static_cast<WorkerFailure>(Get(b+36,4));
    uint64_t token=Get(b+24,8);size_t size=wanted-56;
    int code=Signed(Get(b+40,4)),signal=Signed(Get(b+44,4)),error=Signed(Get(b+48,4));
    bool output=kind==WorkerReplyKind::Stdout || kind==WorkerReplyKind::Stderr;
    Require(kind>=WorkerReplyKind::Accepted && kind<=WorkerReplyKind::State && failure<=WorkerFailure::Channel &&
      (output?size>0:size==0) && code>=-1 && code<=255 && signal>=0 && signal<=64 && error>=0,
      "Invalid frontend reply fields");
    Job* job=Find(token);
    // A queued CANCEL can reach a worker after that job's Complete. The worker
    // replies State/ENOENT; this acknowledges no cleanup and releases nothing.
    auto* cancel_reply=CancellationReply(token);
    bool retired=kind==WorkerReplyKind::State && !job && cancel_reply && cancel_reply->sent &&
      failure==WorkerFailure::Rejected && error==ENOENT;
    Require(retired || (job && job->sent==job->command.size()),"Unknown/unsent frontend reply token");
    if(kind==WorkerReplyKind::State)Require(retired,"Unsolicited frontend State reply");
    Require(!(code>=0 && signal>0),"Conflicting frontend exit status");
    if(kind!=WorkerReplyKind::Complete)Require(code==-1 && signal==0,"Unexpected frontend reply exit status");
    if(kind==WorkerReplyKind::Accepted || output)Require(failure==WorkerFailure::None && error==0,"Invalid frontend data status");
    if(kind==WorkerReplyKind::Accepted){Require(!job->accepted,"Duplicate frontend acceptance");job->accepted=true;}
    if(output) {
      Require(job->accepted && size<=1024*1024-job->output,"Unaccepted/excess frontend output");
      job->output+=size;
    }
    if(kind==WorkerReplyKind::Complete) {
      Require(failure!=WorkerFailure::None || (job->accepted && ((code>=0 && !signal) || (code==-1 && signal>0)) && error==0),"Invalid frontend successful completion");
      // No output or ACK alone can clear a reservation. Disk failure poisons the
      // session and never exposes this Complete as a successful cleanup result.
      if(failure==WorkerFailure::Cancelled)Require(cancel_reply && cancel_reply->sent,"Unsolicited frontend cancellation completion");
      journal.ConfirmJobGone(token);*job=Job{};
      // Cancelled proves that this exact cancellation was consumed while live.
      // Other outcomes cannot prove that: retain the bounded late-reply ticket.
      if(failure==WorkerFailure::Cancelled)*cancel_reply=CancelReply{};
    }
    if(retired)*cancel_reply=CancelReply{};
    WorkerEvent event{kind,token,failure,code,signal,error,{},size};
    if(size)std::memcpy(event.bytes.data(),b+56,size);
    used=0;wanted=56;partial.reset();reply_sequence=Next(reply_sequence);
    return event;
  }
  std::optional<WorkerEvent> Step(Clock::time_point now) {
    if(failed || finished || journal.Blocked())throw Error(ErrorCode::kBusy,"Frontend session closed");
    if(eof)return {};
    try {
      pollfd fds[]={{commands.value,POLLOUT,0},{cancels.value,POLLOUT,0},{replies.value,POLLIN,0}};
      int n=poll(fds,3,0);if(n<0 && errno!=EINTR)Bad("Frontend poll failed");
      if(fds[2].revents&(POLLERR|POLLNVAL))Bad("Frontend reply channel lost");
      if((fds[0].revents|fds[1].revents)&(POLLHUP|POLLERR|POLLNVAL) || (fds[2].revents&POLLHUP)) {
        closing=true;if(!closing_since)closing_since=now;
      }
      if(closing_since && now-*closing_since>=std::chrono::seconds(5))Bad("Frontend worker-close drain deadline");
      // Reply HUP may accompany buffered Complete. Stop sending but drain valid
      // replies before EOF; only external normal-exit proof can clean generation.
      // Priority always progresses independently of a partially written START.
      if(!closing && !stopping && count){auto& c=priority[head];
        Require(now-c.queued<std::chrono::seconds(5),"Frontend CANCEL write deadline");Write(cancels.value,c.bytes.data(),40,c.offset);
        if(c.offset==40){
          auto* entitlement=CancellationReply(Get(c.bytes.data()+24,8));
          Require(entitlement,"Missing frontend CANCEL bookkeeping");entitlement->sent=true;
          c.offset=0;head=(head+1)%priority.size();--count;
        }}
      Job* pending=nullptr;
      for(auto& j:jobs)if(j.token && j.sent<j.command.size() && (!pending || j.token<pending->token))pending=&j;
      if(!closing && !stopping && pending) {
        Require(now-pending->queued<std::chrono::seconds(5),"Frontend START write deadline");
        Write(commands.value,pending->command.data(),pending->command.size(),pending->sent);
      }
      return Read(now);
    } catch(...) {Abort();throw;}
  }
};
WorkerSession::WorkerSession(BrokerJournal& j,int c,int p,int r):impl_(std::make_unique<Impl>(j,c,p,r)){}
WorkerSession::~WorkerSession(){impl_->Abort();}
uint64_t WorkerSession::Generation() const {std::lock_guard lock(impl_->mutex);return impl_->generation;}
bool WorkerSession::Failed() const {std::lock_guard lock(impl_->mutex);return impl_->failed;}
uint64_t WorkerSession::Start(const std::string& request) {
  auto& s=*impl_;std::lock_guard lock(s.mutex);s.Active();
  Impl::Job* slot=nullptr;for(auto& j:s.jobs)if(!j.token){slot=&j;break;}
  if(!slot || !s.command_sequence)throw Error(ErrorCode::kBusy,"Frontend capacity/sequence exhausted");
  // Validation/allocation precede durable admission. After Reserve only no-throw
  // token patch and vector move occur; first command byte is written by Step.
  auto command=EncodeWorkerCommand({WorkerCommandKind::Start,s.generation,s.command_sequence,1,request});
  uint64_t token;
  try {token=s.journal.Reserve();}catch(...) {s.Abort();throw;}
  Put(command.data()+24,token,8);slot->token=token;slot->command=std::move(command);slot->queued=Clock::now();
  s.command_sequence=Next(s.command_sequence);return token;
}
void WorkerSession::Cancel(uint64_t token) {
  auto& s=*impl_;std::lock_guard lock(s.mutex);s.Active();auto* job=s.Find(token);
  if(!job)throw Error(ErrorCode::kNotFound,"Frontend job not found");
  if(job->cancelled)return;
  if(s.count==s.priority.size() || !s.cancel_sequence)throw Error(ErrorCode::kBusy,"Frontend cancellation queue full");
  Impl::CancelReply* entitlement=nullptr;for(auto& c:s.cancel_replies)if(!c.token){entitlement=&c;break;}
  if(!entitlement){s.Abort();throw Error(ErrorCode::kBusy,"Frontend CANCEL correlation exhausted");}
  auto bytes=EncodeWorkerCommand({WorkerCommandKind::Cancel,s.generation,s.cancel_sequence,token,{}});
  auto& record=s.priority[(s.head+s.count)%s.priority.size()];std::copy(bytes.begin(),bytes.end(),record.bytes.begin());record.offset=0;record.queued=Clock::now();
  entitlement->token=token;entitlement->sent=false;
  ++s.count;s.cancel_sequence=Next(s.cancel_sequence);job->cancelled=true;
}
std::optional<WorkerEvent> WorkerSession::Step(Clock::time_point now){std::lock_guard lock(impl_->mutex);return impl_->Step(now);}
void WorkerSession::Abort() noexcept {std::lock_guard lock(impl_->mutex);impl_->Abort();}
void WorkerSession::PrepareStop() {
  auto& s=*impl_;std::lock_guard lock(s.mutex);s.Active();
  if(s.count || s.used || !s.journal.Reservations().empty())throw Error(ErrorCode::kBusy,"Frontend still has pending work");
  s.stopping=true;s.closing=true;s.closing_since=Clock::now();s.commands.Close();s.cancels.Close();
}
void WorkerSession::ConfirmNormalExit() {
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  try {
    Require(s.stopping && s.eof && !s.failed && !s.finished && !s.used,"Frontend normal exit not prepared");
    pollfd fd{s.replies.value,POLLIN,0};Require(poll(&fd,1,0)>=0 && !(fd.revents&(POLLIN|POLLERR|POLLNVAL)),"Frontend unread/lost status on normal exit");
    s.journal.ConfirmNormalWorkerExit();s.finished=true;s.replies.Close();
  }catch(...){s.Abort();throw;}
}
}
