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

#include "catalog/coordinated_writer.hh"

#include <exception>

#include <unistd.h>

namespace capmgr {

struct CoordinatedCatalogWriter::Impl {
  pid_t creator = getpid();
  Catalog catalog;
  Impl(ReadLeasePolicy policy, CatalogGenerationLease::Mode mode,
       std::chrono::milliseconds budget, GenerationLeaseOperations* operations)
      : catalog(CatalogGenerationLease::Acquire(std::move(policy), mode, budget,
                                                operations)) {}
  void CheckCreator() {
    if (creator != getpid())
      throw Error(ErrorCode::kPermission, "Inherited coordinated writer");
  }

  void Check() {
    CheckCreator();
    catalog.db_.CheckGeneration();
  }
};

CoordinatedCatalogWriter::CoordinatedCatalogWriter(
    ReadLeasePolicy policy, CatalogGenerationLease::Mode mode,
    std::chrono::milliseconds budget, GenerationLeaseOperations* operations)
    : impl_(std::make_unique<Impl>(std::move(policy), mode, budget,
                                   operations)) {}
CoordinatedCatalogWriter::~CoordinatedCatalogWriter() {
  // Do not touch inherited NOMUTEX SQLite or any inherited synchronization state.
  // Child must exec/_exit; a wrong ordinary destruction is an invariant failure.
  if (impl_ && impl_->creator != getpid()) std::terminate();
}

void CoordinatedCatalogWriter::Stage(const std::string& operation,
                                     const std::string& owner,
                                     const std::vector<Entry>& entries) {
  impl_->Check();
  impl_->catalog.Stage(operation, owner, entries);
  impl_->Check();
}

void CoordinatedCatalogWriter::Finalize(const std::string& operation,
                                        bool success) {
  impl_->Check();
  impl_->catalog.Finalize(operation, success);
  impl_->Check();
}

bool CoordinatedCatalogWriter::PublishActions(
    const std::vector<Entry>& entries) {
  impl_->Check();
  const bool changed = impl_->catalog.PublishActions(entries);
  impl_->Check();
  return changed;
}

uint64_t CoordinatedCatalogWriter::Revision() {
  impl_->Check();
  const auto revision = impl_->catalog.Revision();
  impl_->Check();
  return revision;
}

void CoordinatedCatalogWriter::Close() {
  impl_->CheckCreator();
  impl_->catalog.db_.Close();
}
}  // namespace capmgr
