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

#ifndef CAPABILITY_MANAGER_CATALOG_READ_ACCESS_HH_
#define CAPABILITY_MANAGER_CATALOG_READ_ACCESS_HH_

#include "catalog/database.hh"
namespace capmgr {
// Private admission lifetime. Must outlive SQLite, including failed destroy
// retries. Destruction only closes owned descriptors; no IPC/join/fsync.
class ReadAccess {
 public:
  virtual ~ReadAccess() = default;
  virtual const std::string& Path() const noexcept = 0;
  virtual void Check() = 0;
  virtual void Opened(Database&) = 0;
};
}

#endif  // CAPABILITY_MANAGER_CATALOG_READ_ACCESS_HH_
