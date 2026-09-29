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

#ifndef CAPABILITY_MANAGER_AMD_MODULE_ACTION_IMPORT_HH_
#define CAPABILITY_MANAGER_AMD_MODULE_ACTION_IMPORT_HH_

#include "catalog/catalog.hh"

#include <map>

namespace capmgr {

// Reads an existing Action DB snapshot only. Never parses installation metadata.
std::vector<Entry> ReadActionSnapshot(const std::string& source_path);
Json EntityClosure(const Json& action,
                   const std::map<std::string, Json>& entities);
}  // namespace capmgr

namespace capmgr {

// Called on startup or an authenticated source commit notification.
// Replays the committed revision on every reconciliation, including no-op imports.
// Receivers must deduplicate monotonically. The Action-writer feed remains an
// integration prerequisite, not a polling promise.
bool SynchronizeActions(CatalogWriter& catalog, const std::string& source_path,
                        const std::function<void(uint64_t)>& changed);
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_AMD_MODULE_ACTION_IMPORT_HH_
