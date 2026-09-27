// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <memory>
#include <string>
#include <sys/types.h>
namespace capmgr {
// Pins the CONNECTING process and its socket credentials. An inherited/passed
// socket can be used by another sender: this is NOT per-message identity and
// must not authorize a remount into the current sender's namespace.
// Proc/starttime guards do not prove absence of an initial PID-reuse race.
class Peer {
 public:
  static std::shared_ptr<Peer> FromSocket(int accepted_fd);
  ~Peer();
  Peer(const Peer&)=delete;
  Peer& operator=(const Peer&)=delete;
  pid_t pid() const{return pid_;}
  uid_t uid() const{return uid_;}
  gid_t gid() const{return gid_;}
  int socket_fd() const{return socket_;}
  // Object pin only; not a verified remount target.
  int namespace_fd() const{return namespace_;}
  const std::string& security_label() const{return label_;}
  bool Alive() const;
  // Compare two pinned connections to one live process and namespace object.
  bool SameConnector(const Peer& other) const;
 private:
  Peer()=default;
  int socket_=-1,proc_=-1,namespace_=-1;
  pid_t pid_=-1;
  uid_t uid_=0;
  gid_t gid_=0;
  std::string label_,start_time_;
};
}
