// SPDX-License-Identifier: Apache-2.0
#pragma once
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
}
