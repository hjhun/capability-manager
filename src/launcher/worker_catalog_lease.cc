// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_catalog_lease.hh"

#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <exception>
#include <optional>
#include <type_traits>

#include "catalog/catalog.hh"

namespace capmgr {
namespace {
[[noreturn]] void SqlFailure(sqlite3* db, int code) {
  const int primary = code & 255;
  throw Error(primary == SQLITE_BUSY || primary == SQLITE_LOCKED
                  ? ErrorCode::kBusy
                  : ErrorCode::kDatabase,
              db ? sqlite3_errmsg(db) : sqlite3_errstr(code));
}
void Require(bool ok, const char* message) {
  if (!ok) throw Error(ErrorCode::kPermission, message);
}
void BorrowedDirectory(int source) {
  // Reject a caller-owned SQLite data FD WITHOUT duplication or close.
  struct stat info{};
  const int flags = fcntl(source, F_GETFL);
  Require(flags >= 0 && (flags & O_ACCMODE) == O_RDONLY && !(flags & O_PATH) &&
              !fstat(source, &info) && S_ISDIR(info.st_mode),
          "Unreadable worker catalog directory");
}
// Only this translation unit can construct/use SQL resources for the reader.
// Every entry and destructor checks the creator, including prepare failure and
// transaction rollback. No callbacks, fork or credential transition are allowed.
struct Connection {
  pid_t creator = getpid();
  sqlite3* db = nullptr;
  bool rollback_failed = false;
  void Check() const {
    Require(creator == getpid(), "Inherited worker catalog reader");
  }
  void CleanupCheck() const noexcept {
    if (creator != getpid()) std::terminate();
  }
  [[noreturn]] void Fail(int rc) const {
    Check();
    SqlFailure(db, rc);
  }
  ~Connection() {
    CleanupCheck();
    if (db && sqlite3_close(db) != SQLITE_OK) std::terminate();
  }
  void Exec(const char* sql) {
    Check();
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) Fail(rc);
  }
  void Close() {
    Check();
    if (!db) return;
    const int rc = sqlite3_close(db);
    if (rc != SQLITE_OK) Fail(rc);  // Retain both owners for retry.
    db = nullptr;
  }
};
class Query final {
 public:
  Query(Connection& connection, const char* sql) : connection_(connection) {
    connection_.Check();
    const int rc =
        sqlite3_prepare_v2(connection_.db, sql, -1, &query_, nullptr);
    if (rc != SQLITE_OK) {
      connection_.CleanupCheck();
      sqlite3_finalize(query_);
      query_ = nullptr;
      connection_.Fail(rc);
    }
  }
  ~Query() {
    connection_.CleanupCheck();
    sqlite3_finalize(query_);
  }
  Query(const Query&) = delete;
  Query& operator=(const Query&) = delete;
  bool Next() {
    connection_.Check();
    const int rc = sqlite3_step(query_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    connection_.Fail(rc);
  }
  int Type(int column) const {
    connection_.Check();
    return sqlite3_column_type(query_, column);
  }
  int64_t Integer(int column) const {
    connection_.Check();
    return sqlite3_column_int64(query_, column);
  }
  std::string Text(int column) const {
    connection_.Check();
    const auto* bytes = sqlite3_column_text(query_, column);
    if (!bytes) throw Error(ErrorCode::kDatabase, "Unreadable catalog text");
    connection_.Check();
    const int size = sqlite3_column_bytes(query_, column);
    return {reinterpret_cast<const char*>(bytes), static_cast<size_t>(size)};
  }

 private:
  Connection& connection_;
  sqlite3_stmt* query_ = nullptr;
};
class ReadTransaction final {
 public:
  explicit ReadTransaction(Connection& connection) : connection_(connection) {
    connection_.Exec("BEGIN");
  }
  ~ReadTransaction() {
    connection_.CleanupCheck();
    if (!committed_) {
      const int rc =
          sqlite3_exec(connection_.db, "ROLLBACK", nullptr, nullptr, nullptr);
      // Preserve the load failure. Ordinary rollback IO errors are not a
      // no-escape invariant violation: physical close still owns both resources.
      // No snapshot can be published on this exception path.
      connection_.rollback_failed = rc != SQLITE_OK;
    }
  }
  void Commit() {
    connection_.Exec("COMMIT");
    committed_ = true;
  }

 private:
  Connection& connection_;
  bool committed_ = false;
};
}  // namespace

struct WorkerCatalogReader::Impl {
  // Reverse destruction: physical SQLite close precedes the SAME lease release.
  std::unique_ptr<CatalogReadLease> lease;
  Connection connection;
  std::optional<WorkerCatalogSnapshot> pending;
  bool failed = false, transferred = false;
  Impl(int directory, ReadLeasePolicy policy, ReadLeaseOperations* operations) {
    BorrowedDirectory(directory);
    lease = std::make_unique<CatalogReadLease>(std::move(policy), operations);
    lease->MatchDirectory(directory);
    connection.Check();
    const int rc =
        sqlite3_open_v2(lease->Path().c_str(), &connection.db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK) connection.Fail(rc);
    connection.Check();
    if (sqlite3_extended_result_codes(connection.db, 1) != SQLITE_OK)
      throw Error(ErrorCode::kDatabase, "Worker reader configuration failed");
    connection.Check();
    if (sqlite3_busy_timeout(connection.db, 1000) != SQLITE_OK)
      throw Error(ErrorCode::kDatabase, "Worker reader configuration failed");
    connection.Exec("PRAGMA query_only=ON; PRAGMA cache_size=-1024;");
    ValidateOpen();
  }
  void ValidateOpen() {
    connection.Check();
    lease->Check();
    Require(sqlite3_db_readonly(connection.db, "main") == 1,
            "Worker catalog must be read-only");
    connection.Check();
    const char* path = sqlite3_db_filename(connection.db, "main");
    Require(path && lease->Path() == path, "Worker SQLite path mismatch");
    // Check() validates all named DB/WAL/SHM objects against the retained pins.
    Query mode(connection, "PRAGMA journal_mode");
    Require(mode.Next() && mode.Type(0) == SQLITE_TEXT && mode.Text(0) == "wal",
            "Worker catalog requires existing WAL");
    lease->Check();
  }
  void Read() {
    ReadTransaction read(connection);
    uint64_t revision = 0;
    {
      Query version(connection, "PRAGMA user_version");
      if (!version.Next() || version.Type(0) != SQLITE_INTEGER ||
          version.Integer(0) != 2)
        throw Error(ErrorCode::kUnsupported, "Unsupported worker schema");
      Query state(connection,
                  "SELECT revision FROM catalog_state WHERE singleton=1 "
                  "AND typeof(revision)='integer'");
      if (!state.Next() || state.Type(0) != SQLITE_INTEGER ||
          state.Integer(0) < 0)
        throw Error(ErrorCode::kDatabase, "Invalid worker catalog revision");
      revision = static_cast<uint64_t>(state.Integer(0));
    }
    std::vector<RegisteredCli> entries;
    {
      // Same published table, order and eleven type checks as GetPrivate;
      // pending rows and non-CLI entries are excluded, never auto-published.
      Query rows(connection,
                 "SELECT id,name,description,keywords,kind,owner,app_id,"
                 "detail,resource,executable,stable_key FROM capability "
                 "WHERE kind=3 ORDER BY id");
      while (rows.Next()) {
        if (entries.size() == 256)
          throw Error(ErrorCode::kLimit,
                      "Worker catalog snapshot exceeds 256 entries");
        for (int column = 0; column < 11; ++column)
          if (rows.Type(column) != (column == 4 ? SQLITE_INTEGER : SQLITE_TEXT))
            throw Error(ErrorCode::kDatabase, "Corrupt worker catalog entry");
        auto id = rows.Text(0);
        if (rows.Text(1).empty() || rows.Integer(4) != 3 ||
            rows.Text(5).empty() ||
            CanonicalId(Kind::kCli, rows.Text(10)) != id)
          throw Error(ErrorCode::kDatabase, "Worker catalog identity mismatch");
        // GetPrivate validates stored detail, even though this startup registry
        // does not retain it. Preserve its malformed/non-object rejection.
        const auto detail = Json::parse(rows.Text(7), nullptr, false);
        if (detail.is_discarded() || !detail.is_object())
          throw Error(ErrorCode::kDatabase, "Corrupt stored catalog JSON");
        entries.push_back({std::move(id), rows.Text(9)});
      }
    }
    WorkerCatalogSnapshot captured{revision,
                                   WorkerRegistry(std::move(entries))};
    // Registry allocation and all schema/content checks finish within the one
    // read transaction. No SQL resources remain when physical close is tried.
    read.Commit();
    lease->Check();
    pending.emplace(std::move(captured));
  }
};

LeasedWorkerCatalogSnapshot::LeasedWorkerCatalogSnapshot(
    WorkerCatalogSnapshot&& snapshot,
    std::unique_ptr<CatalogReadLease>&& lease) noexcept
    : snapshot_(std::move(snapshot)), lease_(std::move(lease)) {
  static_assert(std::is_nothrow_move_constructible_v<WorkerCatalogSnapshot>);
}
LeasedWorkerCatalogSnapshot::LeasedWorkerCatalogSnapshot(
    LeasedWorkerCatalogSnapshot&&) noexcept = default;
LeasedWorkerCatalogSnapshot::~LeasedWorkerCatalogSnapshot() = default;
WorkerCatalogReader::WorkerCatalogReader(int directory, ReadLeasePolicy policy,
                                         ReadLeaseOperations* operations)
    : impl_(std::make_unique<Impl>(directory, std::move(policy), operations)) {}
WorkerCatalogReader::~WorkerCatalogReader() = default;
void WorkerCatalogReader::Close() { impl_->connection.Close(); }
LeasedWorkerCatalogSnapshot WorkerCatalogReader::Finish() {
  impl_->connection.Check();
  if (impl_->failed || impl_->connection.rollback_failed ||
      impl_->transferred || (!impl_->connection.db && !impl_->pending))
    throw Error(ErrorCode::kInvalid, "Unavailable worker catalog reader");
  if (!impl_->pending) {
    try {
      impl_->ValidateOpen();
      impl_->Read();
    } catch (...) {
      impl_->failed = true;
      throw;
    }
  }
  impl_->connection
      .Close();           // BUSY/error leaves pending and both owners intact.
  impl_->lease->Check();  // Failure after close still returns no snapshot.
  impl_->transferred = true;
  return LeasedWorkerCatalogSnapshot(std::move(*impl_->pending),
                                     std::move(impl_->lease));
}
}  // namespace capmgr
