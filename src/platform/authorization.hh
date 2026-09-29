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

#ifndef CAPABILITY_MANAGER_PLATFORM_AUTHORIZATION_HH_
#define CAPABILITY_MANAGER_PLATFORM_AUTHORIZATION_HH_

#include "platform/peer.hh"

namespace capmgr {

enum class PolicyDecision { kAllowed, kDenied, kUnresolved, kUnavailable };
// Private test/integration boundary, never installed or exported.
class ConnectionPolicy {
 public:
  virtual ~ConnectionPolicy() = default;
  virtual PolicyDecision CheckSocket(int socket) = 0;
};

// Explicit CONNECTION-principal check, including system UIDs that TIDL bypasses.
// No policy grant, caller-provided identity or interactive privilege prompt.
void RequirePlatformPrivilege(const Peer& peer);
void RequirePlatformPrivilege(const Peer& peer, ConnectionPolicy& policy);
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_AUTHORIZATION_HH_
