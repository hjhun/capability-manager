/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// SPDX-License-Identifier: Apache-2.0

#ifndef CAPABILITY_MANAGER_PLATFORM_TIDL_CHANNELS_HH_
#define CAPABILITY_MANAGER_PLATFORM_TIDL_CHANNELS_HH_

#include "platform/authorization.hh"

#include <mutex>

namespace capmgr {

// Inherited by the locally generated ServiceBase. Bind BOTH sockets before
// OnCreate; validate the actual MAIN fd before any parcel is decoded. Callback
// extension getters are never an authority. This authorizes a connection, not
// the writer of each stream fragment. Remount/task authorization stays disabled.
class TidlChannels {
 public:
  explicit TidlChannels(std::shared_ptr<ConnectionPolicy> policy = {});
  // Compare extension cache only with independently successful callback API data.
  static bool CheckCallbackExtension(int actual_fd, pid_t actual_pid,
                                     uid_t actual_owner_uid, int cached_fd,
                                     pid_t cached_pid,
                                     uid_t cached_owner_uid) noexcept;
  bool BindChannels(int main_fd, int callback_fd) noexcept;
  bool ValidateChannels(int main_fd, int callback_fd) noexcept;
  std::shared_ptr<Peer> MainPrincipal() const;

 private:
  void Authorize(const Peer& peer);
  std::shared_ptr<ConnectionPolicy> policy_;
  mutable std::mutex mutex_;
  std::shared_ptr<Peer> main_, callback_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_TIDL_CHANNELS_HH_
