// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "catalog/catalog.hh"
#include <map>
namespace capmgr {
// Reads an existing Action DB snapshot only. Never parses installation metadata.
std::vector<Entry> ReadActionSnapshot(const std::string& source_path);
Json EntityClosure(const Json& action,const std::map<std::string,Json>& entities);
}
namespace capmgr {
// Called on startup or an authenticated source commit notification.
// Replays the committed revision on every reconciliation, including no-op imports.
// Receivers must deduplicate monotonically. The Action-writer feed remains an
// integration prerequisite, not a polling promise.
bool SynchronizeActions(Catalog& catalog,const std::string& source_path,
                        const std::function<void(uint64_t)>& changed);
}
