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

#ifndef CAPABILITY_MANAGER_AMD_MODULE_MODULE_CONFIG_HH_
#define CAPABILITY_MANAGER_AMD_MODULE_MODULE_CONFIG_HH_

#include "catalog/catalog.hh"

namespace capmgr {

struct AmdModuleConfig {
  bool enabled = false;
  std::string directory_label, file_label, lock_label;
};

// Administrative labels only. Paths, commands, IDs and mode bits are fixed by
// the module, not selected by this root-protected configuration.
AmdModuleConfig ParseAmdModuleConfig(const Json& value);

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_AMD_MODULE_MODULE_CONFIG_HH_
