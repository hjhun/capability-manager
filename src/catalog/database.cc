// SPDX-License-Identifier: Apache-2.0
#include "catalog/database.hh"
#include <limits>
namespace capmgr {
namespace {
[[noreturn]] void Fail(sqlite3* db, int code) {
  const int primary = code & 0xff;
  throw Error(primary == SQLITE_BUSY || primary == SQLITE_LOCKED
                  ? ErrorCode::kBusy : ErrorCode::kDatabase,
              db ? sqlite3_errmsg(db) : sqlite3_errstr(code));
}
}
Statement::Statement(sqlite3* db, const char* sql) {
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
  if (rc != SQLITE_OK) Fail(db, rc);
}
Statement::~Statement() { sqlite3_finalize(stmt_); }
void Statement::Bind(int index, std::string_view value) {
  if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    throw Error(ErrorCode::kLimit, "SQLite string too large");
  int rc = sqlite3_bind_text(stmt_, index, value.data(),
                            static_cast<int>(value.size()), SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) Fail(sqlite3_db_handle(stmt_), rc);
}
void Statement::Bind(int index, int64_t value) {
  int rc = sqlite3_bind_int64(stmt_, index, value);
  if (rc != SQLITE_OK) Fail(sqlite3_db_handle(stmt_), rc);
}
bool Statement::Step() {
  int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  Fail(sqlite3_db_handle(stmt_), rc);
}
std::string Statement::Text(int column) const {
  const auto* p = sqlite3_column_text(stmt_, column);
  return p ? std::string(reinterpret_cast<const char*>(p),
                         sqlite3_column_bytes(stmt_, column)) : std::string{};
}
int Statement::Type(int column) const { return sqlite3_column_type(stmt_,column); }
int64_t Statement::Integer(int column) const {
  return sqlite3_column_int64(stmt_, column);
}
Database::Database(const std::string& path, Access access) {
  int flags = access == Access::kWriter ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                       : SQLITE_OPEN_READONLY;
  int rc = sqlite3_open_v2(path.c_str(), &db_, flags | SQLITE_OPEN_NOMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    std::string message = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
    sqlite3_close(db_); db_ = nullptr;
    throw Error(ErrorCode::kDatabase, message);
  }
  try {
    sqlite3_extended_result_codes(db_, 1);
    sqlite3_busy_timeout(db_, 1000);
    Exec("PRAGMA cache_size=-1024; PRAGMA foreign_keys=ON;");
    if (access == Access::kWriter) {
      Statement mode(db_, "PRAGMA journal_mode=WAL");
      if (!mode.Step() || mode.Text(0) != "wal")
        throw Error(ErrorCode::kDatabase, "WAL mode required");
      Exec("PRAGMA synchronous=FULL;");
    } else {
      Exec("PRAGMA query_only=ON;");
    }
  } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Database::~Database() { sqlite3_close(db_); }
void Database::Exec(const char* sql) {
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK) Fail(db_, rc);
}
uint64_t Database::Revision() {
  Statement q(db_, "SELECT revision FROM catalog_state WHERE singleton=1 AND typeof(revision)='integer'");
  if (!q.Step() || q.Integer(0) < 0)
    throw Error(ErrorCode::kDatabase, "Invalid catalog revision");
  return static_cast<uint64_t>(q.Integer(0));
}
Transaction::Transaction(Database& db, bool write) : db_(db) {
  db_.Exec(write ? "BEGIN IMMEDIATE" : "BEGIN");
}
Transaction::~Transaction() {
  if (!committed_) sqlite3_exec(db_.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
}
void Transaction::Commit() { db_.Exec("COMMIT"); committed_ = true; }
}
