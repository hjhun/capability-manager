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

#ifndef CAPABILITY_MANAGER_PKGMGR_PLUGIN_PARSER_HH_
#define CAPABILITY_MANAGER_PKGMGR_PLUGIN_PARSER_HH_

#include "catalog/catalog.hh"

namespace capmgr {

struct Metadata {
  Kind kind;
  std::string value;
  std::string app_id;
};

std::vector<Entry> ParsePackage(const std::string& package_root,
                                const std::string& owner,
                                const std::vector<Metadata>& metadata);
enum class FinalizationAuthority { kUnavailable, kOfflineHarness };
// Production integration has no authoritative finalizer yet and must pass unavailable.
// All metadata callbacks must be collected before calling this package boundary.
void StagePackage(
    CatalogWriter& catalog, const std::string& operation,
    const std::string& root, const std::string& owner,
    const std::vector<Metadata>& metadata,
    FinalizationAuthority authority = FinalizationAuthority::kUnavailable);
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PKGMGR_PLUGIN_PARSER_HH_
