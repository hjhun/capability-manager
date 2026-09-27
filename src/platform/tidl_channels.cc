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
    if (!main->SameConnector(*callback)) return false;
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
        !main_->SameConnector(*callback_))
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
