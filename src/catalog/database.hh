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

#ifndef CAPABILITY_MANAGER_CATALOG_DATABASE_HH_
#define CAPABILITY_MANAGER_CATALOG_DATABASE_HH_

#include <sqlite3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "common/error.hh"

namespace capmgr {

class Statement {
 public:
  Statement(sqlite3* db, const char* sql);
  ~Statement();
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  void Bind(int index, std::string_view value);
  void Bind(int index, int64_t value);
  bool Step();

  std::string Text(int column) const;
  int64_t Integer(int column) const;
  int Type(int column) const;

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

class CatalogGenerationLease;

class Database {
 public:
  enum class Access { kReadOnly, kWriter };

  Database(const std::string& path, Access access);
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  sqlite3* handle() const { return db_; }
  void Exec(const char* sql);
  uint64_t Revision();

 private:
  friend class Catalog;
  friend class CoordinatedCatalogWriter;

  explicit Database(std::unique_ptr<CatalogGenerationLease>);
  void Open(const std::string& path, Access access, bool existing = false);
  void CheckGeneration();
  void SealGeneration();
  bool Maintenance() const;
  void Close();
  void CloseOrTerminate() noexcept;

  // Released only after physical SQLite close, including constructor failure.
  std::unique_ptr<CatalogGenerationLease> generation_;
  sqlite3* db_ = nullptr;
};

class Transaction {
 public:
  explicit Transaction(Database& db, bool write = true);
  ~Transaction();
  void Commit();

 private:
  Database& db_;
  bool committed_ = false;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_CATALOG_DATABASE_HH_
