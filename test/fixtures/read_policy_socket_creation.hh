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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SOCKET_CREATION_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SOCKET_CREATION_HH_

#include <sys/stat.h>

#include <stdexcept>

namespace capmgr::fixture::realpolicy {

// Caller has already validated the fixed root server identity/topology/reference
// and exact one-task table. This process-wide mask persists through constructors
// and kernel exit; never query/toggle it after platform threads may exist.
inline void EstablishReferenceServerCreationMask() {
  const mode_t inherited = umask(0000);
  if (inherited != 0077)
    throw std::runtime_error("unexpected inherited reference creation mask");
  // Unexpected startup terminates; no restore, retry, or module-load fallback.
}

}  // namespace capmgr::fixture::realpolicy

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_SOCKET_CREATION_HH_
