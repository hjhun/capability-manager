// SPDX-License-Identifier: Apache-2.0
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

#ifndef CAPABILITY_MANAGER_PLATFORM_PEER_HH_
#define CAPABILITY_MANAGER_PLATFORM_PEER_HH_

#include <memory>
#include <string>

#include <sys/types.h>

namespace capmgr {

// Owns connection-time socket credentials, obtained through platform APIs.
// An inherited/passed endpoint can outlive its connector. This is not a task
// liveness, PID-reuse, per-message identity or namespace-authority proof.
class Peer {
 public:
  static std::shared_ptr<Peer> FromSocket(int accepted_fd);
  ~Peer();
  Peer(const Peer&) = delete;
  Peer& operator=(const Peer&) = delete;
  pid_t pid() const { return pid_; }
  uid_t uid() const { return uid_; }
  gid_t gid() const { return gid_; }
  int socket_fd() const { return socket_; }
  const std::string& security_label() const { return label_; }
  bool Connected() const;
  bool SameCredentials(const Peer& other) const;
  // No reviewed platform API currently proves the original live process object.
  // Task-dependent experiments must deny instead of substituting socket liveness.
  bool HasVerifiedLiveTask() const noexcept { return false; }

 private:
  Peer() = default;
  int socket_ = -1;
  pid_t pid_ = -1;
  uid_t uid_ = 0;
  gid_t gid_ = 0;
  std::string label_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_PEER_HH_
