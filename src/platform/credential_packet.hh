// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "platform/peer.hh"
#include <string>
namespace capmgr {
// Listener must enable SO_PASSCRED and SO_PASSSEC before accepting connections.
// Receive one bounded packet without waiting. Reject delegation: per-packet
// PID/UID/GID and packet-carried SOCKET label must match the connection.
// SCM_SECURITY is not the sender's current task label after exec/relabel; a
// current-label validation/race strategy remains a production authorization gate.
// This is a transport check, not ticket, privilege or mount authorization.
std::string ReceiveCredentialPacket(const Peer& peer);
}
