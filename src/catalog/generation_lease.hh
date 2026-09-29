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
// SPDX-License-Identifier: Apache-2.0

#ifndef CAPABILITY_MANAGER_CATALOG_GENERATION_LEASE_HH_
#define CAPABILITY_MANAGER_CATALOG_GENERATION_LEASE_HH_

#include "catalog/read_lease.hh"

#include <chrono>
#include <memory>

namespace capmgr {

// Trusted internal configuration only. No application-selected maintenance path.
// Ancestor/mount provenance and external writer cooperation remain image gates.
struct GenerationLeaseOperations : ReadLeaseOperations {
  virtual int Lock(int fd, short type);
};

class CatalogGenerationLease {
 public:
  enum class Mode { kExisting, kMaintenance };
  static std::unique_ptr<CatalogGenerationLease> Acquire(
      ReadLeasePolicy policy, Mode mode,
      std::chrono::milliseconds budget = std::chrono::milliseconds(100),
      GenerationLeaseOperations* operations = nullptr);
  ~CatalogGenerationLease();
  CatalogGenerationLease(const CatalogGenerationLease&) = delete;
  CatalogGenerationLease& operator=(const CatalogGenerationLease&) = delete;
  const std::string& Path() const noexcept;
  bool Maintenance() const noexcept;
  bool InCreator() const noexcept;
  void Check();

 private:
  friend class Database;
  friend class Catalog;
  struct Impl;
  explicit CatalogGenerationLease(std::unique_ptr<Impl>);
  // These are reachable only inside the coordinated SQLite owner.
  void Prepare();
  void Opened(Database&);
  void Seal();
  std::unique_ptr<Impl> impl_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_CATALOG_GENERATION_LEASE_HH_
