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

#include "catalog/database.hh"

#include <exception>
#include <limits>

#include "catalog/generation_lease.hh"

namespace capmgr {

namespace {

[[noreturn]] void Fail(sqlite3* db, int code) {
  const int primary = code & 0xff;
  throw Error(primary == SQLITE_BUSY || primary == SQLITE_LOCKED
                  ? ErrorCode::kBusy
                  : ErrorCode::kDatabase,
              db ? sqlite3_errmsg(db) : sqlite3_errstr(code));
}
}  // namespace

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
                         sqlite3_column_bytes(stmt_, column))
           : std::string{};
}

int Statement::Type(int column) const {
  return sqlite3_column_type(stmt_, column);
}

int64_t Statement::Integer(int column) const {
  return sqlite3_column_int64(stmt_, column);
}

Database::Database(const std::string& path, Access access) {
  Open(path, access);
}

Database::Database(std::unique_ptr<CatalogGenerationLease> generation)
    : generation_(std::move(generation)) {
  try {
    generation_->Prepare();
    Open(generation_->Path(), Access::kWriter, !generation_->Maintenance());
    generation_->Opened(*this);
  } catch (...) {
    CloseOrTerminate();
    throw;
  }
}

void Database::Open(const std::string& path, Access access, bool existing) {
  int flags = access == Access::kWriter
                  ? SQLITE_OPEN_READWRITE | (existing ? 0 : SQLITE_OPEN_CREATE)
                  : SQLITE_OPEN_READONLY;
  int rc =
      sqlite3_open_v2(path.c_str(), &db_, flags | SQLITE_OPEN_NOMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    std::string message = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
    if (generation_)
      CloseOrTerminate();
    else {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    throw Error(ErrorCode::kDatabase, message);
  }
  try {
    sqlite3_extended_result_codes(db_, 1);
    sqlite3_busy_timeout(db_, 1000);
    Exec("PRAGMA cache_size=-1024; PRAGMA foreign_keys=ON;");
    if (access == Access::kWriter) {
      {
        Statement mode(
            db_, existing ? "PRAGMA journal_mode" : "PRAGMA journal_mode=WAL");
        if (!mode.Step() || mode.Text(0) != "wal")
          throw Error(ErrorCode::kDatabase, "WAL mode required");
      }
      int persistent = 1;
      rc = sqlite3_file_control(db_, "main", SQLITE_FCNTL_PERSIST_WAL,
                                &persistent);
      if (rc != SQLITE_OK)
        throw Error(ErrorCode::kDatabase, "SQLITE_FCNTL_PERSIST_WAL failed (" +
                                              std::to_string(rc) +
                                              "): " + sqlite3_errstr(rc));
      Exec("PRAGMA synchronous=FULL;");
    } else {
      Exec("PRAGMA query_only=ON;");
    }
  } catch (...) {
    if (generation_)
      CloseOrTerminate();
    else {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    throw;
  }
}

Database::~Database() {
  if (generation_)
    CloseOrTerminate();
  else
    sqlite3_close(db_);  // Unchanged isolated legacy lifetime.
}

void Database::CloseOrTerminate() noexcept {
  if (generation_ && !generation_->InCreator()) std::terminate();
  if (db_ && sqlite3_close(db_) != SQLITE_OK) std::terminate();
  db_ = nullptr;
  generation_.reset();
}

void Database::Close() {
  if (generation_ && !generation_->InCreator())
    throw Error(ErrorCode::kPermission, "Inherited catalog writer");
  if (db_) {
    const int rc = sqlite3_close(db_);
    if (rc != SQLITE_OK) Fail(db_, rc);  // Both resources remain owned.
    db_ = nullptr;
  }

  generation_.reset();
}

void Database::CheckGeneration() {
  if (!db_) throw Error(ErrorCode::kInvalid, "Closed catalog writer");
  if (generation_) generation_->Check();
}

void Database::SealGeneration() {
  CheckGeneration();
  generation_->Seal();
}

bool Database::Maintenance() const { return generation_->Maintenance(); }
void Database::Exec(const char* sql) {
  if (generation_) CheckGeneration();
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK) Fail(db_, rc);
}

uint64_t Database::Revision() {
  if (generation_) CheckGeneration();
  Statement q(
      db_,
      "SELECT revision FROM catalog_state WHERE singleton=1 AND typeof(revision)='integer'");
  if (!q.Step() || q.Integer(0) < 0)
    throw Error(ErrorCode::kDatabase, "Invalid catalog revision");
  return static_cast<uint64_t>(q.Integer(0));
}

Transaction::Transaction(Database& db, bool write) : db_(db) {
  db_.Exec(write ? "BEGIN IMMEDIATE" : "BEGIN");
}

Transaction::~Transaction() {
  if (!committed_)
    sqlite3_exec(db_.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
}

void Transaction::Commit() {
  db_.Exec("COMMIT");
  committed_ = true;
}
}  // namespace capmgr
