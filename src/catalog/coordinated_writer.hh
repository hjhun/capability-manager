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

#ifndef CAPABILITY_MANAGER_CATALOG_COORDINATED_WRITER_HH_
#define CAPABILITY_MANAGER_CATALOG_COORDINATED_WRITER_HH_

#include "catalog/catalog.hh"
#include "catalog/generation_lease.hh"

namespace capmgr {

// Private single-owner facade. No SQLite/Catalog/statement/blob/backup resource
// escapes; all results are owning values. Callers quiesce before destruction.
// Legacy path-only Catalog remains an isolated harness, not this production API.
class CoordinatedCatalogWriter final : public CatalogWriter {
 public:
  CoordinatedCatalogWriter(
      ReadLeasePolicy policy, CatalogGenerationLease::Mode mode,
      std::chrono::milliseconds budget = std::chrono::milliseconds(100),
      GenerationLeaseOperations* operations = nullptr);
  ~CoordinatedCatalogWriter() override;
  CoordinatedCatalogWriter(const CoordinatedCatalogWriter&) = delete;
  CoordinatedCatalogWriter& operator=(const CoordinatedCatalogWriter&) = delete;
  void Stage(const std::string&, const std::string&,
             const std::vector<Entry>&) override;
  void Finalize(const std::string&, bool) override;
  bool PublishActions(const std::vector<Entry>&) override;
  uint64_t Revision() override;
  // Integrity check under the same generation lease, without exposing SQLite.
  void CheckIntegrity();
  // An internal SQLite close BUSY retains the DB AND lease for retry. Destruction
  // fail-stops on that invariant violation; public client destroy is unchanged.
  void Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_CATALOG_COORDINATED_WRITER_HH_
