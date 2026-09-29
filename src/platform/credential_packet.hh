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

#ifndef CAPABILITY_MANAGER_PLATFORM_CREDENTIAL_PACKET_HH_
#define CAPABILITY_MANAGER_PLATFORM_CREDENTIAL_PACKET_HH_

#include "platform/peer.hh"

#include <string>

namespace capmgr {

// Disabled with NOT_SUPPORTED while Peer has no verified live-task API.
// The retained decoder is not reachable through connection credentials alone.
// Listener must enable SO_PASSCRED and SO_PASSSEC before accepting connections.
// Receive one bounded packet without waiting. Reject delegation: per-packet
// PID/UID/GID and packet-carried SOCKET label must match the connection.
// SCM_SECURITY is not the sender's current task label after exec/relabel; a
// current-label validation/race strategy remains a production authorization gate.
// This is a transport check, not ticket, privilege or mount authorization.
std::string ReceiveCredentialPacket(const Peer& peer);
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_CREDENTIAL_PACKET_HH_
