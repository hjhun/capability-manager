// SPDX-License-Identifier: Apache-2.0
#include "platform/peer.hh"
#include "common/error.hh"
#include "platform/cynara_socket.hh"
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef CAPMGR_HAVE_CYNARA
#include <cynara-creds-socket.h>
#endif
namespace capmgr {
namespace {
[[noreturn]] void Denied() {
  throw Error(ErrorCode::kPermission,
              "Unverified local connection credentials");
}
#ifdef CAPMGR_HAVE_CYNARA
template <typename Id>
Id SocketId(int fd, cynara_user_creds method) {
  char* raw = nullptr;
  int status = cynara_creds_socket_get_user(fd, method, &raw);
  std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
  if (status != CYNARA_API_SUCCESS || !raw) Denied();
  std::string_view text(raw, strnlen(raw, 32));
  Id id{};
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), id);
  if (text.empty() || text.size() == 32 || error != std::errc{} ||
      end != text.data() + text.size() || id == static_cast<Id>(-1))
    Denied();
  return id;
}
#endif
}
std::shared_ptr<Peer> Peer::FromSocket(int fd) {
  auto peer = std::shared_ptr<Peer>(new Peer);
  peer->socket_ = fcntl(fd, F_DUPFD_CLOEXEC, 3);
  if (peer->socket_ < 0) Denied();
  int domain = 0;
  socklen_t size = sizeof(domain);
  if (getsockopt(peer->socket_, SOL_SOCKET, SO_DOMAIN, &domain, &size) ||
      size != sizeof(domain) || domain != AF_UNIX)
    Denied();
  struct ucred credentials{};
  size = sizeof(credentials);
  if (getsockopt(peer->socket_, SOL_SOCKET, SO_PEERCRED, &credentials, &size) ||
      size != sizeof(credentials) || credentials.pid <= 0)
    Denied();
  std::array<char, 4096> label{};
  size = label.size();
  if (getsockopt(peer->socket_, SOL_SOCKET, SO_PEERSEC, label.data(), &size) ||
      size > label.size())
    Denied();
  std::string socket_label(label.data(), size);
  if (!socket_label.empty() && socket_label.back() == '\0')
    socket_label.pop_back();
  if (socket_label.empty() || socket_label.find('\0') != std::string::npos)
    Denied();
#ifdef CAPMGR_HAVE_CYNARA
  std::lock_guard lock(internal::CynaraSocketMutex());
  if (cynara_creds_socket_get_pid(peer->socket_, &peer->pid_) !=
      CYNARA_API_SUCCESS)
    Denied();
  peer->uid_ = SocketId<uid_t>(peer->socket_, USER_METHOD_UID);
  peer->gid_ = SocketId<gid_t>(peer->socket_, USER_METHOD_GID);
  char* raw = nullptr;
  int status =
      cynara_creds_socket_get_client(peer->socket_, CLIENT_METHOD_SMACK, &raw);
  std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
  if (status != CYNARA_API_SUCCESS || !raw) Denied();
  const auto label_size = strnlen(raw, 4097);
  if (label_size == 0 || label_size > 4096) Denied();
  peer->label_.assign(raw, label_size);
  // Explicit UID/GID are raw socket credentials, unlike DEFAULT/OWNER_UID.
  // Reject a synthesized no-Smack label or unexpected helper mapping.
  if (peer->pid_ != credentials.pid || peer->uid_ != credentials.uid ||
      peer->gid_ != credentials.gid || peer->label_ != socket_label)
    Denied();
#else
  // Non-Tizen test builds use the kernel socket API. Real Cynara policy stays
  // unavailable; a failed native Cynara call never falls back to this branch.
  peer->pid_ = credentials.pid;
  peer->uid_ = credentials.uid;
  peer->gid_ = credentials.gid;
  peer->label_ = std::move(socket_label);
#endif
  if (!peer->Connected()) Denied();
  return peer;
}
bool Peer::Connected() const {
  pollfd socket{socket_, POLLRDHUP, 0};
  return poll(&socket, 1, 0) >= 0 &&
         !(socket.revents & (POLLHUP | POLLRDHUP | POLLERR | POLLNVAL));
}
bool Peer::SameCredentials(const Peer& other) const {
  return pid_ == other.pid_ && uid_ == other.uid_ && gid_ == other.gid_ &&
         label_ == other.label_ && Connected() && other.Connected();
}
Peer::~Peer() {
  if (socket_ >= 0) close(socket_);
}
}
