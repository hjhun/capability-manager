// SPDX-License-Identifier: Apache-2.0
#include "platform/peer.hh"
#include "common/error.hh"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
namespace capmgr {
namespace {
[[noreturn]] void Denied(){throw Error(ErrorCode::kPermission,"Unverified local peer identity");}
std::string StartTime(int proc) {
  int file=openat(proc,"stat",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(file<0)Denied();
  std::array<char,8192> buffer{};ssize_t count;
  do {count=read(file,buffer.data(),buffer.size());}while(count<0 && errno==EINTR);
  close(file);if(count<=0 || static_cast<size_t>(count)==buffer.size())Denied();
  std::string text(buffer.data(),static_cast<size_t>(count));auto end=text.rfind(')');
  if(end==std::string::npos)Denied();
  std::istringstream fields(text.substr(end+1));std::string field;
  // Reject exited connectors even when a recipient retains their endpoint.
  // The first token after comm is state (field 3); starttime is field 22.
  if(!(fields>>field) || field.size()!=1 || field=="Z" || field=="X" || field=="x")Denied();
  for(int i=4;i<=22;++i)if(!(fields>>field))Denied();
  if(field.empty() || field.find_first_not_of("0123456789")!=std::string::npos)Denied();
  return field;
}
}
std::shared_ptr<Peer> Peer::FromSocket(int fd) {
  auto peer=std::shared_ptr<Peer>(new Peer);
  peer->socket_=fcntl(fd,F_DUPFD_CLOEXEC,3);if(peer->socket_<0)Denied();
  int domain=0;socklen_t size=sizeof(domain);
  if(getsockopt(peer->socket_,SOL_SOCKET,SO_DOMAIN,&domain,&size) || domain!=AF_UNIX)Denied();
  struct ucred credentials{};size=sizeof(credentials);
  if(getsockopt(peer->socket_,SOL_SOCKET,SO_PEERCRED,&credentials,&size) ||
     size!=sizeof(credentials) || credentials.pid<=0)Denied();
  peer->pid_=credentials.pid;peer->uid_=credentials.uid;peer->gid_=credentials.gid;
  std::array<char,4096> label{};size=label.size();
  if(getsockopt(peer->socket_,SOL_SOCKET,SO_PEERSEC,label.data(),&size) || size>label.size())Denied();
  peer->label_.assign(label.data(),size);
  if(!peer->label_.empty() && peer->label_.back()=='\0')peer->label_.pop_back();
  if(peer->label_.empty() || peer->label_.find('\0')!=std::string::npos)Denied();
  auto path="/proc/"+std::to_string(peer->pid_);
  peer->proc_=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(peer->proc_<0)Denied();
  peer->start_time_=StartTime(peer->proc_);
  peer->namespace_=openat(peer->proc_,"ns/mnt",O_RDONLY|O_CLOEXEC);
  if(peer->namespace_<0 || !peer->Alive())Denied();
  return peer;
}
bool Peer::Alive() const {
  try {
    pollfd socket{socket_,POLLRDHUP,0};
    if(poll(&socket,1,0)<0 || (socket.revents&(POLLHUP|POLLRDHUP|POLLERR|POLLNVAL)))return false;
    return StartTime(proc_)==start_time_;
  } catch(...) {return false;}
}
bool Peer::SameConnector(const Peer& other) const {
  struct stat left{},right{};
  return pid_==other.pid_ && uid_==other.uid_ && gid_==other.gid_ &&
      label_==other.label_ && start_time_==other.start_time_ &&
      fstat(namespace_,&left)==0 && fstat(other.namespace_,&right)==0 &&
      left.st_dev==right.st_dev && left.st_ino==right.st_ino && Alive() && other.Alive();
}
Peer::~Peer() {
  if(namespace_>=0)close(namespace_);
  if(proc_>=0)close(proc_);
  if(socket_>=0)close(socket_);
}
}
