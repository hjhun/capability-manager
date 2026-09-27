// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "platform/authorization.hh"
#include <mutex>
namespace capmgr {
// Inherited by the locally generated ServiceBase. Bind BOTH sockets before
// OnCreate; validate the actual MAIN fd before any parcel is decoded. Callback
// extension getters are never an authority. This authorizes a connection, not
// the writer of each stream fragment; remount needs the credential sidechannel.
class TidlChannels {
 public:
  explicit TidlChannels(std::shared_ptr<ConnectionPolicy> policy = {});
  bool BindChannels(int main_fd, int callback_fd) noexcept;
  bool ValidateChannels(int main_fd, int callback_fd) noexcept;
  std::shared_ptr<Peer> MainPrincipal() const;

 private:
  void Authorize(const Peer& peer);
  std::shared_ptr<ConnectionPolicy> policy_;
  mutable std::mutex mutex_;
  std::shared_ptr<Peer> main_, callback_;
};
}
