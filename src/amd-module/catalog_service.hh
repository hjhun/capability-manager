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

#ifndef CAPABILITY_MANAGER_AMD_MODULE_CATALOG_SERVICE_HH_
#define CAPABILITY_MANAGER_AMD_MODULE_CATALOG_SERVICE_HH_

#include "catalog/coordinated_writer.hh"

#include <thread>

namespace capmgr {

// All methods and destruction run on ONE owner thread. No public admission or
// installer finalization is selected by this maintenance/import service.
class AmdCatalogService {
 public:
  AmdCatalogService(ReadLeasePolicy policy, std::string action_path,
                    std::function<void(uint64_t)> changed,
                    GenerationLeaseOperations* operations = nullptr);
  ~AmdCatalogService();
  void Start();
  bool Reconcile();
  void Stop();

 private:
  void Owner() const;
  const std::thread::id owner_ = std::this_thread::get_id();
  ReadLeasePolicy policy_;
  std::string action_path_;
  std::function<void(uint64_t)> changed_;
  GenerationLeaseOperations* operations_;
  std::unique_ptr<CoordinatedCatalogWriter> writer_;
  bool started_ = false;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_AMD_MODULE_CATALOG_SERVICE_HH_
