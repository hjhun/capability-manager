// SPDX-License-Identifier: Apache-2.0
#pragma once
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
}
