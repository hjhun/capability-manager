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

#ifndef CAPABILITY_MANAGER_CATALOG_CATALOG_HH_
#define CAPABILITY_MANAGER_CATALOG_CATALOG_HH_

#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "catalog/database.hh"
namespace capmgr {
using Json = nlohmann::json;
enum class Kind : int {
  kAll = 0,
  kSkill = 1,
  kAppSkill = 2,
  kCli = 3,
  kAction = 4
};
struct Entry {
  std::string id;
  std::string name;
  std::string desc;
  std::string keywords;
  Kind kind = Kind::kSkill;
  std::string owner;
  std::string app_id;
  Json detail;
  std::string resource;
  std::string executable;
  std::string key;
};
std::string CanonicalId(Kind kind, const std::string& name,
                        const std::string& app_id = {});
const char* KindName(Kind kind);
// Owning-value writer subset shared with isolated harnesses. No SQLite escape.
class CatalogWriter {
 public:
  virtual ~CatalogWriter() = default;
  virtual void Stage(const std::string& operation, const std::string& owner,
                     const std::vector<Entry>& entries) = 0;
  virtual bool PublishActions(const std::vector<Entry>& entries) = 0;
  virtual void Finalize(const std::string& operation, bool success) = 0;
  virtual uint64_t Revision() = 0;
};
class Catalog : public CatalogWriter {
 public:
  Catalog(const std::string& path, Database::Access access);
  ~Catalog() override = default;
  void Stage(const std::string& operation, const std::string& owner,
             const std::vector<Entry>& entries) override;
  // Atomic trusted Action-source snapshot; rejects every non-Action entry.
  bool PublishActions(const std::vector<Entry>& entries) override;
  void Finalize(const std::string& operation, bool success) override;
  void Foreach(Kind filter, const std::function<bool(const Json&)>& callback);
  std::vector<Json> Search(const std::string& query, Kind filter = Kind::kAll);
  Json Get(const std::string& id);
  Entry GetPrivate(const std::string& id);
  uint64_t Revision() override { return db_.Revision(); }
  Database& database() { return db_; }

 private:
  friend class CoordinatedCatalogWriter;
  explicit Catalog(std::unique_ptr<CatalogGenerationLease>);
  void ValidateVersion();
  void Migrate();
  void Replace(const std::string& owner, const std::vector<Entry>& entries);
  void Validate(const std::string& owner, const std::vector<Entry>& entries);
  Database db_;
  bool writer_;
};
}

#endif  // CAPABILITY_MANAGER_CATALOG_CATALOG_HH_
