// SPDX-License-Identifier: Apache-2.0
#pragma once
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
}
