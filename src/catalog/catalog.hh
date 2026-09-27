// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "catalog/database.hh"
namespace capmgr {
using Json = nlohmann::json;
enum class Kind : int { kAll = 0, kSkill = 1, kAppSkill = 2, kCli = 3, kAction = 4 };
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
class Catalog {
 public:
  Catalog(const std::string& path, Database::Access access);
  void Stage(const std::string& operation, const std::string& owner,
             const std::vector<Entry>& entries);
  // Atomic trusted Action-source snapshot; rejects every non-Action entry.
  bool PublishActions(const std::vector<Entry>& entries);
  void Finalize(const std::string& operation, bool success);
  void Foreach(Kind filter, const std::function<bool(const Json&)>& callback);
  std::vector<Json> Search(const std::string& query, Kind filter = Kind::kAll);
  Json Get(const std::string& id);
  Entry GetPrivate(const std::string& id);
  uint64_t Revision() { return db_.Revision(); }
  Database& database() { return db_; }
 private:
  void ValidateVersion();
  void Migrate();
  void Replace(const std::string& owner, const std::vector<Entry>& entries);
  void Validate(const std::string& owner, const std::vector<Entry>& entries);
  Database db_;
  bool writer_;
};
}
