// SPDX-License-Identifier: Apache-2.0
#include "platform/tidl_channels.hh"
#include <sys/stat.h>
namespace capmgr {
namespace {
bool SameSocket(int left, int right) {
  struct stat a{}, b{};
  return fstat(left, &a) == 0 && fstat(right, &b) == 0 && S_ISSOCK(a.st_mode) &&
         S_ISSOCK(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
}
TidlChannels::TidlChannels(std::shared_ptr<ConnectionPolicy> policy)
    : policy_(std::move(policy)) {}
bool TidlChannels::CheckCallbackExtension(int fd, pid_t pid, uid_t owner_uid,
                                          int cached_fd, pid_t cached_pid,
                                          uid_t cached_owner_uid) noexcept {
  return fd >= 0 && pid > 0 && cached_fd == fd && cached_pid == pid &&
         cached_owner_uid == owner_uid;
}
void TidlChannels::Authorize(const Peer& peer) {
  if (policy_)
    RequirePlatformPrivilege(peer, *policy_);
  else
    RequirePlatformPrivilege(peer);
}
bool TidlChannels::BindChannels(int main_fd, int callback_fd) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (main_ || callback_ || SameSocket(main_fd, callback_fd)) return false;
    auto main = Peer::FromSocket(main_fd),
         callback = Peer::FromSocket(callback_fd);
    if (!main->SameCredentials(*callback)) return false;
    Authorize(*main);
    main_ = std::move(main);
    callback_ = std::move(callback);
    return true;
  } catch (...) {
    return false;
  }
}
bool TidlChannels::ValidateChannels(int main_fd, int callback_fd) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (!main_ || !callback_ || !SameSocket(main_fd, main_->socket_fd()) ||
        !SameSocket(callback_fd, callback_->socket_fd()) ||
        !main_->SameCredentials(*callback_))
      return false;
    auto fresh_main = Peer::FromSocket(main_fd);
    auto fresh_callback = Peer::FromSocket(callback_fd);
    if (!main_->SameCredentials(*fresh_main) ||
        !callback_->SameCredentials(*fresh_callback))
      return false;
    Authorize(*main_);
    return true;
  } catch (...) {
    return false;
  }
}
std::shared_ptr<Peer> TidlChannels::MainPrincipal() const {
  std::lock_guard lock(mutex_);
  return main_;
}
}
