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

#include "platform/authorization.hh"
#include "common/error.hh"
#include "platform/cynara_socket.hh"

#include <cstdlib>
#include <memory>

#ifdef CAPMGR_HAVE_CYNARA
#include <cynara-client.h>
#include <cynara-creds-socket.h>

#endif

namespace capmgr {

namespace {

class CynaraPolicy final : public ConnectionPolicy {
 public:
  PolicyDecision CheckSocket(int socket) override {
#ifdef CAPMGR_HAVE_CYNARA
    std::lock_guard lock(internal::CynaraSocketMutex());
    char* user = nullptr;
    int status =
        cynara_creds_socket_get_user(socket, USER_METHOD_DEFAULT, &user);
    std::unique_ptr<char, decltype(&std::free)> user_guard(user, &std::free);
    if (status != CYNARA_API_SUCCESS || !user || !*user)
      throw Error(ErrorCode::kPermission, "Cannot identify Cynara socket user");
    char* client = nullptr;
    status =
        cynara_creds_socket_get_client(socket, CLIENT_METHOD_DEFAULT, &client);
    std::unique_ptr<char, decltype(&std::free)> client_guard(client,
                                                             &std::free);
    if (status != CYNARA_API_SUCCESS || !client || !*client)
      throw Error(ErrorCode::kPermission,
                  "Cannot identify Cynara socket client");
    cynara* raw = nullptr;
    if (cynara_initialize(&raw, nullptr) != CYNARA_API_SUCCESS)
      throw Error(ErrorCode::kPermission, "Cynara service unavailable");
    std::unique_ptr<cynara, decltype(&cynara_finish)> handle(raw,
                                                             &cynara_finish);
    // DEFAULT follows target user/client mapping, derived from this verified socket.
    // simple_check cannot prompt: unresolved policy is denied along with failures.
    status = cynara_simple_check(
        handle.get(), client, "", user,
        "http://tizen.org/privilege/internal/default/platform");
    if (status == CYNARA_API_ACCESS_ALLOWED) return PolicyDecision::kAllowed;
    if (status == CYNARA_API_ACCESS_DENIED) return PolicyDecision::kDenied;
    if (status == CYNARA_API_ACCESS_NOT_RESOLVED)
      return PolicyDecision::kUnresolved;
    return PolicyDecision::kUnavailable;
#else
    (void)socket;
    return PolicyDecision::kUnavailable;
#endif
  }
};

}  // namespace

void RequirePlatformPrivilege(const Peer& peer, ConnectionPolicy& policy) {
  if (!peer.Connected())
    throw Error(ErrorCode::kPermission, "Connection is no longer available");
  if (policy.CheckSocket(peer.socket_fd()) != PolicyDecision::kAllowed ||
      !peer.Connected())
    throw Error(ErrorCode::kPermission,
                "Platform privilege denied or unresolved");
}

void RequirePlatformPrivilege(const Peer& peer) {
  CynaraPolicy policy;
  RequirePlatformPrivilege(peer, policy);
}
}  // namespace capmgr
