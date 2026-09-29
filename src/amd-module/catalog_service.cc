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

#include "amd-module/catalog_service.hh"

#include <sys/stat.h>

#include <cerrno>
#include <system_error>

#include "amd-module/action_import.hh"

namespace capmgr {

AmdCatalogService::AmdCatalogService(ReadLeasePolicy policy,
                                     std::string action_path,
                                     std::function<void(uint64_t)> changed,
                                     GenerationLeaseOperations* operations)
    : policy_(std::move(policy)),
      action_path_(std::move(action_path)),
      changed_(std::move(changed)),
      operations_(operations) {}

AmdCatalogService::~AmdCatalogService() {
  if (std::this_thread::get_id() != owner_) std::terminate();
}

void AmdCatalogService::Owner() const {
  if (std::this_thread::get_id() != owner_)
    throw Error(ErrorCode::kPermission,
                "AMD catalog service wrong owner thread");
}

void AmdCatalogService::Start() {
  Owner();
  if (started_)
    throw Error(ErrorCode::kBusy, "AMD catalog service already started");
  started_ = true;
  // Existing readers retain SH. Ordinary restart/import must not require EX.
  // Only observed missing main DB admits initialization; an existing unsupported
  // schema or a permission/metadata failure never triggers automatic repair.
  struct stat info{};
  if (lstat((policy_.directory + "/catalog.db").c_str(), &info) != 0) {
    if (errno != ENOENT)
      throw std::system_error(errno, std::generic_category(),
                              "inspect catalog");
    CoordinatedCatalogWriter maintenance(
        policy_, CatalogGenerationLease::Mode::kMaintenance,
        std::chrono::milliseconds(100), operations_);
    maintenance.CheckIntegrity();
    maintenance.Close();
  }
  auto writer = std::make_unique<CoordinatedCatalogWriter>(
      policy_, CatalogGenerationLease::Mode::kExisting,
      std::chrono::milliseconds(100), operations_);
  writer->CheckIntegrity();
  SynchronizeActions(*writer, action_path_, changed_);
  writer_ = std::move(writer);
}

bool AmdCatalogService::Reconcile() {
  Owner();
  if (!writer_)
    throw Error(ErrorCode::kInvalid, "AMD catalog service is not running");
  return SynchronizeActions(*writer_, action_path_, changed_);
}

void AmdCatalogService::Stop() {
  Owner();
  if (writer_) {
    writer_
        ->Close();  // Physical SQLite close before generation lease retirement.
    writer_.reset();
  }
}

}  // namespace capmgr
