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

#include "fixture.hh"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include "catalog/coordinated_writer.hh"
#include "launcher/worker_catalog_lease.hh"
#include "launcher/owned_children.hh"

extern char** environ;

namespace {

// Test-executable-only faults. No production connection/statement accessor.
thread_local bool observe_reader = false, fail_prepare = false;
thread_local bool retain_statement = false, fail_close = false;
thread_local bool fail_step_allocation = false;
thread_local int allocation_step_skip = 0;
thread_local sqlite3_stmt* retained = nullptr;
thread_local int readonly_opens = 0, close_calls = 0;
thread_local const char* corrupt_metadata = nullptr;
thread_local std::function<void(int)> close_observer;
}  // namespace

extern "C" int __real_sqlite3_open_v2(const char*, sqlite3**, int, const char*);
extern "C" int __real_sqlite3_prepare_v2(sqlite3*, const char*, int,
                                         sqlite3_stmt**, const char**);
extern "C" int __real_sqlite3_step(sqlite3_stmt*);
extern "C" int __real_sqlite3_close(sqlite3*);
extern "C" int __wrap_sqlite3_open_v2(const char* path, sqlite3** db, int flags,
                                      const char* vfs) {
  if (observe_reader && (flags & SQLITE_OPEN_READONLY)) {
    ++readonly_opens;
    EXPECT_EQ(flags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE), 0);
  }

  return __real_sqlite3_open_v2(path, db, flags, vfs);
}

extern "C" int __wrap_sqlite3_prepare_v2(sqlite3* db, const char* sql, int size,
                                         sqlite3_stmt** result,
                                         const char** tail) {
  if (observe_reader && std::exchange(fail_prepare, false)) return SQLITE_IOERR;
  const int rc = __real_sqlite3_prepare_v2(db, sql, size, result, tail);
  if (observe_reader && rc == SQLITE_OK) {
    if (const char* path = std::exchange(corrupt_metadata, nullptr))
      if (chmod(path, 0666)) std::terminate();
    if (std::exchange(retain_statement, false)) {
      if (__real_sqlite3_prepare_v2(db, "SELECT 1", -1, &retained, nullptr) !=
          SQLITE_OK)
        std::terminate();
    }
  }
  return rc;
}

extern "C" int __wrap_sqlite3_step(sqlite3_stmt* statement) {
  if (observe_reader && fail_step_allocation) {
    if (allocation_step_skip == 0) {
      fail_step_allocation = false;
      throw std::bad_alloc();
    }
    --allocation_step_skip;
  }

  return __real_sqlite3_step(statement);
}

extern "C" int __wrap_sqlite3_close(sqlite3* db) {
  if (!observe_reader || !db) return __real_sqlite3_close(db);
  ++close_calls;
  if (close_observer) close_observer(-1);  // Lease must still exclude EX.
  const int rc = std::exchange(fail_close, false) ? SQLITE_IOERR
                                                  : __real_sqlite3_close(db);
  if (close_observer) close_observer(rc);  // Including physical SQLITE_OK.
  return rc;
}

namespace {

using namespace capmgr;
using namespace std::chrono_literals;
struct Labels : GenerationLeaseOperations {
  bool allocation_failure = false;
  std::string Label(int) override {
    if (std::exchange(allocation_failure, false)) throw std::bad_alloc();
    return "Fixture";
  }
};

struct Fd {
  int fd = -1;
  ~Fd() {
    if (fd >= 0) close(fd);
  }
};

class WorkerCatalogLeaseTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    const auto directory = root_ + "/catalog";
    ASSERT_EQ(mkdir(directory.c_str(), 0700), 0);
    const auto lock = root_ + "/generation.lock";
    const int fd =
        open(lock.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fchmod(fd, 0600), 0);
    ASSERT_EQ(close(fd), 0);
    policy_ = {directory, lock, getuid(), getuid(),  getgid(),  getgid(),
               0700,      0600, 0600,     "Fixture", "Fixture", "Fixture"};
    path_ = directory + "/catalog.db";
    CoordinatedCatalogWriter provision(
        policy_, CatalogGenerationLease::Mode::kMaintenance, 0ms, &labels_);
    provision.Close();
    directory_.fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ASSERT_GE(directory_.fd, 0);
    observe_reader = true;
    readonly_opens = close_calls = 0;
  }

  void TearDown() override {
    observe_reader = fail_prepare = retain_statement = fail_close = false;
    fail_step_allocation = false;
    allocation_step_skip = 0;
    corrupt_metadata = nullptr;
    close_observer = {};
    ASSERT_EQ(retained, nullptr) << "Leaked test statement";
    for (auto& record : records_) {
      if (!record.id) continue;
      auto status = children_.StopAndWait(record.id, 1000ms);
      if (status.state != ChildState::Complete) {
        std::cerr << "RETAINED_SCOPE=" << root_ << std::endl;
        std::terminate();  // Never delete from unconfirmed child cleanup.
      }
      children_.Release(record.id);
      record = {};
    }
    CatalogTest::TearDown();
  }

  void ErrorIs(ErrorCode code, const std::function<void()>& action) {
    try {
      action();
      FAIL() << "Expected rejection";
    } catch (const Error& error) {
      EXPECT_EQ(error.code(), code) << error.what();
    }
  }
  struct Record {
    pid_t pid = -1;
    uint64_t id = 0;
  };
  Record& Reserve() {
    for (auto& record : records_)
      if (!record.id) {
        record.id = children_.Reserve();
        return record;
      }
    std::terminate();
  }

  pid_t Fork() {
    auto& record = Reserve();
    const auto child = fork();
    if (child > 0) {
      children_.AttachReserved(record.id, child);
      record.pid = child;
    } else if (child < 0) {
      children_.AbandonUnspawned(record.id);
      record = {};
    }
    return child;
  }

  bool Wait(pid_t child, int expected) {
    Record* owned = nullptr;
    for (auto& record : records_)
      if (record.id && record.pid == child) owned = &record;
    if (!owned) return false;
    auto status = children_.Inspect(owned->id);
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (status.state == ChildState::Running &&
           std::chrono::steady_clock::now() < end) {
      std::this_thread::sleep_for(1ms);
      status = children_.Inspect(owned->id);
    }
    const bool normal = status.state == ChildState::Complete &&
                        status.exit_code == expected && status.signal == 0;
    if (status.state != ChildState::Complete)
      status = children_.StopAndWait(owned->id, 1000ms);
    if (status.state == ChildState::Complete) {
      children_.Release(owned->id);
      *owned = {};
    }
    return normal;  // Uncertain records remain owned for fail-stop teardown.
  }

  void Probe(const char* mode, bool busy) {
    const auto executable =
        std::filesystem::read_symlink("/proc/self/exe").parent_path().string() +
        "/capmgr-sqlite-lock-probe";
    std::string expected = busy ? "BUSY" : "OK";
    char* args[] = {const_cast<char*>(executable.c_str()),
                    path_.data(),
                    policy_.lock_path.data(),
                    const_cast<char*>(mode),
                    expected.data(),
                    nullptr};
    pid_t child = -1;
    auto& record = Reserve();
    const int rc = posix_spawn(&child, executable.c_str(), nullptr, nullptr,
                               args, environ);
    if (rc == 0) {
      children_.AttachReserved(record.id, child);
      record.pid = child;
    } else {
      children_.AbandonUnspawned(record.id);
      record = {};
    }
    ASSERT_EQ(rc, 0);
    EXPECT_TRUE(Wait(child, 0)) << mode << " " << expected;
  }

  std::unique_ptr<WorkerCatalogReader> Reader(int fd = -1) {
    return std::make_unique<WorkerCatalogReader>(fd < 0 ? directory_.fd : fd,
                                                 policy_, &labels_);
  }

  void PublishCli(std::string key, std::string executable) {
    auto e = Make(std::move(key), "pkg.one", Kind::kCli);
    e.executable = std::move(executable);
    CoordinatedCatalogWriter writer(
        policy_, CatalogGenerationLease::Mode::kExisting, 0ms, &labels_);
    const auto op = "publish-" + std::to_string(sequence_.fetch_add(1));
    writer.Stage(op, e.owner, {e});
    writer.Finalize(op, true);
    writer.Close();
  }
  ReadLeasePolicy policy_{};
  Labels labels_;
  Fd directory_;
  OwnedChildren children_{16};
  std::array<Record, 16> records_{};
};

TEST_F(WorkerCatalogLeaseTest,
       IndependentExBusyThroughPhysicalCloseAndSnapshot) {
  static_assert(!std::is_copy_constructible_v<LeasedWorkerCatalogSnapshot>);
  static_assert(!std::is_move_assignable_v<LeasedWorkerCatalogSnapshot>);
  static_assert(
      std::is_nothrow_move_constructible_v<LeasedWorkerCatalogSnapshot>);
  Probe("generation", false);
  close_observer = [&](int) { Probe("generation", true); };
  auto reader = Reader();
  Probe("generation", true);
  std::optional<LeasedWorkerCatalogSnapshot> snapshot(reader->Finish());
  EXPECT_EQ(snapshot->Revision(), 0u);
  EXPECT_EQ(readonly_opens, 1);
  EXPECT_EQ(close_calls, 1);
  reader.reset();
  Probe("generation", true);
  std::optional<LeasedWorkerCatalogSnapshot> moved(std::move(*snapshot));
  snapshot.reset();
  Probe("generation", true);
  moved.reset();
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest,
       RealStatementBusyRetainsBothForFinalizeAndRetry) {
  auto reader = Reader();
  retain_statement = true;
  ErrorIs(ErrorCode::kBusy, [&] { (void)reader->Finish(); });
  ASSERT_NE(retained, nullptr);
  Probe("generation", true);
  EXPECT_EQ(sqlite3_finalize(std::exchange(retained, nullptr)), SQLITE_OK);
  reader->Close();
  Probe("generation", true);
  auto snapshot = reader->Finish();  // Same already materialized registry.
  EXPECT_EQ(snapshot.Revision(), 0u);
  ErrorIs(ErrorCode::kInvalid, [&] { (void)reader->Finish(); });
}

TEST_F(WorkerCatalogLeaseTest, ExplicitAbandonCloseKeepsLeaseUntilOwnerEnds) {
  auto reader = Reader();
  reader->Close();
  Probe("generation", true);
  ErrorIs(ErrorCode::kInvalid, [&] { (void)reader->Finish(); });
  reader.reset();
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, CloseIoFailureCanRetryWithoutReopening) {
  auto reader = Reader();
  fail_close = true;
  ErrorIs(ErrorCode::kDatabase, [&] { (void)reader->Finish(); });
  Probe("generation", true);
  auto snapshot = reader->Finish();
  EXPECT_EQ(snapshot.Revision(), 0u);
  EXPECT_EQ(readonly_opens, 1);
  EXPECT_EQ(close_calls, 2);
}

TEST_F(WorkerCatalogLeaseTest, ConstructorFailureClosesBeforeLeaseRelease) {
  close_observer = [&](int) { Probe("generation", true); };
  fail_prepare = true;
  ErrorIs(ErrorCode::kDatabase, [&] { Reader(); });
  EXPECT_EQ(close_calls, 1);
  close_observer = {};
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, PostOpenMetadataFailureReturnsNoOwner) {
  const auto shm = path_ + "-shm";
  corrupt_metadata = shm.c_str();
  ErrorIs(ErrorCode::kPermission, [&] { Reader(); });
  EXPECT_EQ(close_calls, 1);
  EXPECT_EQ(chmod(shm.c_str(), 0600), 0);
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, AllocationFailureRollsBackAndCanOnlyClose) {
  auto reader = Reader();
  allocation_step_skip =
      3;  // WAL mode, schema and revision precede row decoding.
  fail_step_allocation = true;
  EXPECT_THROW((void)reader->Finish(), std::bad_alloc);
  Probe("generation", true);
  ErrorIs(ErrorCode::kInvalid, [&] { (void)reader->Finish(); });
  reader->Close();
  Probe("generation", true);
  reader.reset();
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, PostCloseAllocationReturnsNoSnapshotOrLeaseGap) {
  auto reader = Reader();
  close_observer = [&](int rc) {
    Probe("generation", true);
    if (rc == SQLITE_OK) labels_.allocation_failure = true;
  };
  EXPECT_THROW((void)reader->Finish(), std::bad_alloc);
  EXPECT_EQ(close_calls, 1);  // Physical close happened; no result escaped.
  Probe("generation", true);
  ErrorIs(ErrorCode::kPermission, [&] { (void)reader->Finish(); });
  close_observer = {};
  reader.reset();
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, BusyDestructorFailsBeforeConnectionOrLeaseLoss) {
  const auto child = Fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::set_terminate([] { _exit(86); });
    auto reader = Reader();
    retain_statement = true;
    try {
      (void)reader->Finish();
      _exit(3);
    } catch (const Error& error) {
      if (error.code() != ErrorCode::kBusy) _exit(4);
    }
    reader.reset();
    _exit(5);
  }

  EXPECT_TRUE(Wait(child, 86));
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, BusyConstructorUnwindDoesNotDropLiveSqlLease) {
  const auto shm = path_ + "-shm";
  const auto child = Fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::set_terminate([] { _exit(86); });
    retain_statement = true;
    corrupt_metadata = shm.c_str();
    close_observer = [&](int) { Probe("generation", true); };
    (void)Reader();  // Policy error + real escaped statement => close BUSY.
    _exit(3);
  }

  EXPECT_TRUE(Wait(child, 86));
  EXPECT_EQ(chmod(shm.c_str(), 0600), 0);
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest,
       WrongBorrowedDataAndPathDirectoryRejectBeforeOpen) {
  // Raw ordinary data descriptor remains open until every SQLite owner closes.
  Fd invalid;
  invalid.fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
  ASSERT_GE(invalid.fd, 0);
  ErrorIs(ErrorCode::kPermission, [&] { Reader(invalid.fd); });
  EXPECT_GE(fcntl(invalid.fd, F_GETFD), 0);
  Fd opath;
  opath.fd = open(policy_.directory.c_str(), O_PATH | O_CLOEXEC);
  ASSERT_GE(opath.fd, 0);
  ErrorIs(ErrorCode::kPermission, [&] { Reader(opath.fd); });
  EXPECT_EQ(readonly_opens, 0);
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, WrongDirectoryGenerationRejectsBeforeOpen) {
  const auto other = root_ + "/other";
  ASSERT_EQ(mkdir(other.c_str(), 0700), 0);
  Fd fd;
  fd.fd = open(other.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  ASSERT_GE(fd.fd, 0);
  ErrorIs(ErrorCode::kPermission, [&] { Reader(fd.fd); });
  EXPECT_EQ(readonly_opens, 0);
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, MissingSidecarsNeverRecreated) {
  ASSERT_EQ(unlink((path_ + "-shm").c_str()), 0);
  ErrorIs(ErrorCode::kPermission, [&] { Reader(); });
  EXPECT_EQ(readonly_opens, 0);
  EXPECT_FALSE(std::filesystem::exists(path_ + "-shm"));
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, SchemaAndCorruptCliDenyWithoutMigration) {
  {
    Database writer(path_, Database::Access::kWriter);
    writer.Exec("PRAGMA user_version=1");
  }
  {
    auto reader = Reader();
    ErrorIs(ErrorCode::kUnsupported, [&] { (void)reader->Finish(); });
  }

  Probe("generation", false);
  {
    Database writer(path_, Database::Access::kWriter);
    writer.Exec("PRAGMA user_version=2");
  }

  PublishCli("first", "/usr/bin/true");
  {
    Database writer(path_, Database::Access::kWriter);
    writer.Exec("UPDATE capability SET stable_key='wrong'");
  }

  auto reader = Reader();
  ErrorIs(ErrorCode::kDatabase, [&] { (void)reader->Finish(); });
}

TEST_F(WorkerCatalogLeaseTest, DifferentialSnapshotAndInPlaceCommitLimits) {
  PublishCli("first", "/usr/bin/true");
  std::optional<LeasedWorkerCatalogSnapshot> original(Reader()->Finish());
  auto legacy =
      LoadWorkerCatalog(directory_.fd, {getuid(), getgid(), 0700, 0600});
  EXPECT_EQ(original->Revision(), legacy.revision);
  EXPECT_EQ(original->Registry().Resolve("cli:first"),
            legacy.registry.Resolve("cli:first"));
  PublishCli("second", "/usr/bin/false");
  auto fresh = Reader()->Finish();
  EXPECT_EQ(fresh.Revision(), original->Revision() + 1);
  EXPECT_TRUE(fresh.Registry().Resolve("cli:first").empty());
  EXPECT_EQ(fresh.Registry().Resolve("cli:second"), "/usr/bin/false");
  EXPECT_EQ(original->Registry().Resolve("cli:first"), "/usr/bin/true");
  EXPECT_TRUE(original->Registry().Resolve("cli:second").empty());
  Probe("generation", true);  // SH freezes neither revision nor registrations.
}

TEST_F(WorkerCatalogLeaseTest, InheritedSqlReaderRejectsAndFailsBeforeCleanup) {
  auto reader = Reader();
  const auto child = Fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::set_terminate([] { _exit(86); });
    try {
      reader->Finish();
      _exit(3);
    } catch (const Error& error) {
      if (error.code() != ErrorCode::kPermission) _exit(4);
    }
    try {
      reader->Close();
      _exit(5);
    } catch (const Error& error) {
      if (error.code() != ErrorCode::kPermission) _exit(6);
    }
    // Check no SQLite close entry occurs in the child before termination.
    close_observer = [](int) { _exit(7); };
    reader.reset();
    _exit(8);
  }

  EXPECT_TRUE(Wait(child, 86));
  auto snapshot = reader->Finish();
  Probe("generation", true);
}

TEST_F(WorkerCatalogLeaseTest,
       ClosedSnapshotChildClosesWithoutUnlockingParent) {
  std::optional<LeasedWorkerCatalogSnapshot> snapshot(Reader()->Finish());
  const auto child = Fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    snapshot.reset();  // SQLite was physically closed BEFORE fork.
    Probe("generation", true);
    _exit(HasFailure() ? 3 : 0);
  }

  EXPECT_TRUE(Wait(child, 0));
  Probe("generation", true);
  snapshot.reset();
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest,
       InheritedClosedSnapshotKeepsExBusyUntilFinalClose) {
  std::optional<LeasedWorkerCatalogSnapshot> snapshot(Reader()->Finish());
  int release[2];
  ASSERT_EQ(pipe2(release, O_CLOEXEC), 0);
  const auto child = Fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(release[1]);
    struct pollfd ready{release[0], POLLIN, 0};
    if (poll(&ready, 1, 2000) != 1 || !(ready.revents & POLLIN)) _exit(3);
    char byte;
    if (read(release[0], &byte, 1) != 1) _exit(4);
    snapshot.reset();
    close(release[0]);
    _exit(0);
  }

  close(release[0]);
  snapshot.reset();
  Probe("generation", true);
  ASSERT_EQ(write(release[1], "x", 1), 1);
  close(release[1]);
  EXPECT_TRUE(Wait(child, 0));
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest,
       LoaderCloseAndFailurePreserveOtherWalWriterLocks) {
  auto active = std::make_unique<Database>(path_, Database::Access::kWriter);
  active->Exec("BEGIN IMMEDIATE; UPDATE catalog_state SET revision=1");
  Probe("begin", true);
  Probe("exclusive", true);
  {
    auto snapshot = Reader()->Finish();
    Probe("begin", true);
    Probe("exclusive", true);
  }
  fail_prepare = true;
  ErrorIs(ErrorCode::kDatabase, [&] { Reader(); });
  Probe("begin", true);
  Probe("exclusive", true);
  active->Exec("ROLLBACK");
  active.reset();
  Probe("begin", false);
}

TEST_F(WorkerCatalogLeaseTest, LoaderCloseAndFailurePreserveOtherReadSnapshot) {
  PublishCli("first", "/usr/bin/true");
  auto active = std::make_unique<Database>(path_, Database::Access::kReadOnly);
  active->Exec("BEGIN");
  EXPECT_EQ(active->Revision(), 1u);
  PublishCli("second", "/usr/bin/false");
  Probe("checkpoint", true);
  {
    auto snapshot = Reader()->Finish();
    EXPECT_EQ(snapshot.Revision(), 2u);
  }

  Probe("checkpoint", true);
  fail_prepare = true;
  ErrorIs(ErrorCode::kDatabase, [&] { Reader(); });
  Probe("checkpoint", true);
  active->Exec("ROLLBACK");
  active.reset();
  Probe("checkpoint", false);
}

TEST_F(WorkerCatalogLeaseTest, PublishedCliOnlyMatchesLegacySelection) {
  PublishCli("published", "/usr/bin/true");
  {
    CoordinatedCatalogWriter writer(
        policy_, CatalogGenerationLease::Mode::kExisting, 0ms, &labels_);
    auto pending = Make("pending", "pkg.two", Kind::kCli);
    pending.executable = "/usr/bin/false";
    writer.Stage("pending-only", pending.owner, {pending});
    auto other = Make("skill", "pkg.three", Kind::kSkill);
    writer.Stage("other-kind", other.owner, {other});
    writer.Finalize("other-kind", true);
    writer.Close();
  }

  auto snapshot = Reader()->Finish();
  auto legacy =
      LoadWorkerCatalog(directory_.fd, {getuid(), getgid(), 0700, 0600});
  EXPECT_EQ(snapshot.Revision(), legacy.revision);
  for (const char* id : {"cli:published", "cli:pending", "skill:skill"})
    EXPECT_EQ(snapshot.Registry().Resolve(id), legacy.registry.Resolve(id));
  EXPECT_EQ(snapshot.Registry().Resolve("cli:published"), "/usr/bin/true");
  EXPECT_TRUE(snapshot.Registry().Resolve("cli:pending").empty());
  EXPECT_TRUE(snapshot.Registry().Resolve("skill:skill").empty());
}

TEST_F(WorkerCatalogLeaseTest, Exact256BoundAndCorruptTypesMatchLegacy) {
  std::vector<Entry> entries;
  for (int i = 0; i < 256; ++i) {
    auto e = Make("entry" + std::to_string(i), "pkg.one", Kind::kCli);
    e.executable = "/usr/bin/true";
    entries.push_back(std::move(e));
  }
  {
    Catalog writer(path_,
                   Database::Access::kWriter);  // Isolated fixture writer.
    Publish(writer, "pkg.one", entries);
  }
  {
    auto snapshot = Reader()->Finish();
    EXPECT_EQ(snapshot.Registry().Resolve("cli:entry255"), "/usr/bin/true");
    EXPECT_NO_THROW(LoadWorkerCatalog(
        directory_.fd,
        (WorkerCatalogFilePolicy{getuid(), getgid(), 0700, 0600})));
  }

  auto extra = Make("extra", "pkg.one", Kind::kCli);
  extra.executable = "/usr/bin/true";
  entries.push_back(std::move(extra));
  {
    Catalog writer(path_, Database::Access::kWriter);
    Publish(writer, "pkg.one", entries);
  }
  {
    auto reader = Reader();
    ErrorIs(ErrorCode::kLimit, [&] { (void)reader->Finish(); });
    ErrorIs(ErrorCode::kLimit, [&] {
      LoadWorkerCatalog(directory_.fd, {getuid(), getgid(), 0700, 0600});
    });
  }
  {
    Database writer(path_, Database::Access::kWriter);
    writer.Exec(
        "DELETE FROM capability WHERE stable_key='extra'; "
        "UPDATE capability SET keywords=X'0102' WHERE stable_key='entry0'");
  }

  auto reader = Reader();
  ErrorIs(ErrorCode::kDatabase, [&] { (void)reader->Finish(); });
  ErrorIs(ErrorCode::kDatabase, [&] {
    LoadWorkerCatalog(directory_.fd, {getuid(), getgid(), 0700, 0600});
  });
}

TEST_F(WorkerCatalogLeaseTest,
       BorrowedDbAndShmRejectionPreservesOtherWalLocks) {
  Fd main, shm;  // Ordinary borrowed descriptors close AFTER all SQLite owners.
  main.fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
  shm.fd = open((path_ + "-shm").c_str(), O_RDONLY | O_CLOEXEC);
  ASSERT_GE(main.fd, 0);
  ASSERT_GE(shm.fd, 0);
  Database active(path_, Database::Access::kWriter);
  active.Exec("BEGIN IMMEDIATE; UPDATE catalog_state SET revision=1");
  Probe("begin", true);
  for (int fd : {main.fd, shm.fd}) {
    ErrorIs(ErrorCode::kPermission, [&] { Reader(fd); });
    EXPECT_GE(fcntl(fd, F_GETFD), 0);
    Probe("begin", true);
    Probe("exclusive", true);
  }

  active.Exec("ROLLBACK");
}

TEST_F(WorkerCatalogLeaseTest, PolicyFailureAfterReadClosesWithoutAnySnapshot) {
  auto reader = Reader();
  ASSERT_EQ(chmod((path_ + "-shm").c_str(), 0666), 0);
  ErrorIs(ErrorCode::kPermission, [&] { (void)reader->Finish(); });
  EXPECT_EQ(chmod((path_ + "-shm").c_str(), 0600), 0);
  ErrorIs(ErrorCode::kInvalid, [&] { (void)reader->Finish(); });
  reader->Close();  // Poisoned admission still permits physical close.
  reader.reset();
  Probe("generation", false);
}

TEST_F(WorkerCatalogLeaseTest, StoredCliDetailJsonRejectionMatchesLegacy) {
  PublishCli("first", "/usr/bin/true");
  const std::string bad_utf8 =
      std::string("{\"field\":\"") + char(0xff) + "\"}";
  for (const auto& detail :
       {std::string("not-json"), std::string("[]"), std::string("null"),
        bad_utf8, std::string("{\"valid\":{\"value\":null}}")}) {
    {
      Database writer(path_, Database::Access::kWriter);
      Statement update(writer.handle(), "UPDATE capability SET detail=?");
      update.Bind(1, detail);
      ASSERT_FALSE(update.Step());
    }
    const bool valid = detail.starts_with("{\"valid\"");
    {
      auto reader = Reader();
      if (valid) {
        auto snapshot = reader->Finish();
        auto legacy =
            LoadWorkerCatalog(directory_.fd, {getuid(), getgid(), 0700, 0600});
        EXPECT_EQ(snapshot.Registry().Resolve("cli:first"),
                  legacy.registry.Resolve("cli:first"));
      } else {
        ErrorIs(ErrorCode::kDatabase, [&] { (void)reader->Finish(); });
        ErrorIs(ErrorCode::kDatabase, [&] {
          LoadWorkerCatalog(directory_.fd, {getuid(), getgid(), 0700, 0600});
        });
        Probe("generation", true);  // Failed load returns no snapshot.
        reader->Close();
        Probe("generation", true);  // Lease ends only at owner retirement.
      }
    }
    Probe("generation", false);
  }
}
}  // namespace
