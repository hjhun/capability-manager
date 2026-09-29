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
 * SPDX-License-Identifier: Apache-2.0
 */

#include "platform/credential_packet.hh"
#include "common/error.hh"

#include <array>
#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <unistd.h>

#ifndef SCM_SECURITY
#define SCM_SECURITY 0x03
#endif

namespace capmgr {

namespace {

[[noreturn]] void Reject() {
  throw Error(ErrorCode::kPermission, "Unverified packet sender");
}

void RequireOption(int fd, int name, int expected) {
  int value = 0;
  socklen_t size = sizeof(value);
  if (getsockopt(fd, SOL_SOCKET, name, &value, &size) ||
      size != sizeof(value) || value != expected)
    Reject();
}
}  // namespace

std::string ReceiveCredentialPacket(const Peer& peer) {
  if (!peer.HasVerifiedLiveTask())
    throw Error(ErrorCode::kUnsupported, "Live-task proof API unavailable");
  int fd = peer.socket_fd();
  RequireOption(fd, SO_DOMAIN, AF_UNIX);
  RequireOption(fd, SO_TYPE, SOCK_SEQPACKET);
  RequireOption(fd, SO_PASSCRED, 1);
  RequireOption(fd, SO_PASSSEC, 1);
  std::array<char, 1024> payload{};
  alignas(cmsghdr) std::array<char, 8192> ancillary{};
  iovec io{payload.data(), payload.size()};
  msghdr message{};
  message.msg_iov = &io;
  message.msg_iovlen = 1;
  message.msg_control = ancillary.data();
  message.msg_controllen = ancillary.size();
  ssize_t received;
  do {
    received = recvmsg(fd, &message, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);
  } while (received < 0 && errno == EINTR);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    throw Error(ErrorCode::kBusy, "No credential packet available");
  if (received < 0)
    throw Error(ErrorCode::kIo, "Credential packet receive failed");
  struct ucred credentials{};
  int credential_count = 0, label_count = 0;
  std::string label;
  bool unexpected = false;
  for (auto* header = CMSG_FIRSTHDR(&message); header;
       header = CMSG_NXTHDR(&message, header)) {
    // The kernel constructs received control headers. Still validate lengths.
    auto offset = reinterpret_cast<char*>(header) - ancillary.data();
    if (header->cmsg_len < CMSG_LEN(0) ||
        header->cmsg_len >
            message.msg_controllen - static_cast<size_t>(offset)) {
      unexpected = true;
      break;
    }
    auto bytes = header->cmsg_len - CMSG_LEN(0);
    if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
      // Close even on truncated/rejected packets: receiving rights installs FDs.
      for (size_t i = 0; i + sizeof(int) <= bytes; i += sizeof(int)) {
        int passed;
        std::memcpy(&passed, CMSG_DATA(header) + i, sizeof(passed));
        close(passed);
      }
      unexpected = true;
    } else if (header->cmsg_level == SOL_SOCKET &&
               header->cmsg_type == SCM_CREDENTIALS) {
      ++credential_count;
      if (bytes != sizeof(credentials))
        unexpected = true;
      else
        std::memcpy(&credentials, CMSG_DATA(header), sizeof(credentials));
    } else if (header->cmsg_level == SOL_SOCKET &&
               header->cmsg_type == SCM_SECURITY) {
      ++label_count;
      label.assign(reinterpret_cast<char*>(CMSG_DATA(header)), bytes);
      if (!label.empty() && label.back() == '\0') label.pop_back();
    } else
      unexpected = true;
  }

  if (received <= 0 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
      unexpected || credential_count != 1 || label_count != 1 ||
      credentials.pid != peer.pid() || credentials.uid != peer.uid() ||
      credentials.gid != peer.gid() || label.empty() ||
      label.find('\0') != std::string::npos || label != peer.security_label() ||
      !peer.HasVerifiedLiveTask())
    Reject();
  return std::string(payload.data(), static_cast<size_t>(received));
}
}  // namespace capmgr
