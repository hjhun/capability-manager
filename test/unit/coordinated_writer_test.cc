// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"

#include <grp.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <exception>
#include <fstream>
#include <thread>
#include <poll.h>
#include <spawn.h>

#include "amd-module/action_import.hh"
#include "catalog/coordinated_writer.hh"
#include "catalog/file_metadata.hh"
#include "pkgmgr-plugin/parser.hh"
#include "launcher/worker_catalog.hh"

extern char** environ;

namespace {
// Only the unit executable interposes prepare. The retained statement is a test
// violation of the private no-escape invariant, not a production accessor.
thread_local bool retain_statement = false;
thread_local sqlite3_stmt* retained_statement = nullptr;
thread_local bool fail_prepare = false;
thread_local const char* chmod_on_prepare = nullptr;
}
extern "C" int __real_sqlite3_prepare_v2(sqlite3*, const char*, int,
                                         sqlite3_stmt**, const char**);
extern "C" int __wrap_sqlite3_prepare_v2(sqlite3* db, const char* sql, int size,
                                         sqlite3_stmt** result,
                                         const char** tail) {
  if (std::exchange(fail_prepare, false)) return SQLITE_IOERR;
  const int rc = __real_sqlite3_prepare_v2(db, sql, size, result, tail);
  if (rc == SQLITE_OK && chmod_on_prepare) {
    const char* name = std::exchange(chmod_on_prepare, nullptr);
    if (chmod(name, 0666) != 0) std::terminate();
  }
  if (rc == SQLITE_OK && std::exchange(retain_statement, false)) {
    const int extra = __real_sqlite3_prepare_v2(db, "SELECT 1", -1,
                                                &retained_statement, nullptr);
    if (extra != SQLITE_OK) std::terminate();
  }
  return rc;
}

namespace {
using namespace capmgr;
using Mode = CatalogGenerationLease::Mode;
using namespace std::chrono_literals;
struct Labels : GenerationLeaseOperations {
  int failures = 0, failure = EINTR;
  bool wrong = false;
  std::string Label(int) override { return wrong ? "Wrong" : "Fixture"; }
  int Lock(int fd, short type) override {
    if (failures > 0) {
      --failures;
      errno = failure;
      return -1;
    }
    return GenerationLeaseOperations::Lock(fd, type);
  }
};
struct HeldFd {
  int value = -1;
  ~HeldFd() {
    if (value >= 0) close(value);
  }
};
class CoordinatedWriterTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    const auto directory = root_ + "/catalog";
    ASSERT_EQ(mkdir(directory.c_str(), 0700), 0);
    const auto lock = root_ + "/generation.lock";
    const int fd =
        open(lock.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(close(fd), 0);
    policy_ = {directory, lock, getuid(), getuid(),  getgid(),  getgid(),
               0700,      0600, 0600,     "Fixture", "Fixture", "Fixture"};
    path_ = directory + "/catalog.db";
  }
  std::unique_ptr<CoordinatedCatalogWriter> Writer(
      Mode mode = Mode::kExisting, std::chrono::milliseconds b = 0ms) {
    return std::make_unique<CoordinatedCatalogWriter>(policy_, mode, b,
                                                      &labels_);
  }
  void Provision() {
    auto writer = Writer(Mode::kMaintenance);
    EXPECT_EQ(writer->Revision(), 0u);
    writer->Close();
  }
  void ExpectError(ErrorCode code, const std::function<void()>& action) {
    try {
      action();
      FAIL() << "Expected failure";
    } catch (const Error& error) {
      EXPECT_EQ(error.code(), code) << error.what();
    }
  }
  bool Wait(pid_t child, int expected) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      if (waitpid(child, &status, WNOHANG) == child)
        return WIFEXITED(status) && WEXITSTATUS(status) == expected;
      std::this_thread::sleep_for(1ms);
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return false;
  }
  void Probe(const std::string& mode, bool busy) {
    const auto executable =
        std::filesystem::read_symlink("/proc/self/exe").parent_path().string() +
        "/capmgr-sqlite-lock-probe";
    std::string expected = busy ? "BUSY" : "OK";
    char* args[] = {const_cast<char*>(executable.c_str()),
                    path_.data(),
                    policy_.lock_path.data(),
                    const_cast<char*>(mode.c_str()),
                    expected.data(),
                    nullptr};
    pid_t child;
    ASSERT_EQ(posix_spawn(&child, executable.c_str(), nullptr, nullptr, args,
                          environ),
              0);
    EXPECT_TRUE(Wait(child, 0)) << mode << " " << expected;
  }
  void LoadSnapshot() {
    const int directory =
        open(policy_.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ASSERT_GE(directory, 0);
    try {
      (void)LoadWorkerCatalog(directory, {getuid(), getgid(), 0700, 0600});
    } catch (...) {
      close(directory);
      throw;
    }
    close(directory);
  }
  void PinClose() {
    for (const char* suffix : {"", "-wal", "-shm"}) {
      const int fd =
          open((path_ + suffix).c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
      ASSERT_GE(fd, 0);
      EXPECT_NO_THROW(ValidateDataPin(fd));
      EXPECT_EQ(close(fd), 0);
    }
  }
  ReadLeasePolicy policy_{};
  Labels labels_;
};

TEST_F(CoordinatedWriterTest, OnlyExclusiveBootstrapCreatesCatalog) {
  ExpectError(ErrorCode::kPermission, [&] { Writer(); });
  EXPECT_FALSE(std::filesystem::exists(path_));
  Provision();
  for (const char* suffix : {"", "-wal", "-shm"}) {
    struct stat info{};
    ASSERT_EQ(stat((path_ + suffix).c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 07777, 0600u);
  }
  auto writer = Writer();
  EXPECT_EQ(writer->Revision(), 0u);
}

TEST_F(CoordinatedWriterTest, SharedReadersAndWritersExcludeGenerationChange) {
  Provision();
  auto first = Writer();
  auto second = Writer();
  auto lease = std::make_unique<CatalogReadLease>(policy_, &labels_);
  auto reader = std::make_unique<Catalog>(path_, Database::Access::kReadOnly);
  first->Stage("one", "pkg.one", {Make()});
  EXPECT_TRUE(reader->Search("pictures").empty());
  first->Finalize("one", true);
  EXPECT_EQ(second->Revision(), 1u);
  EXPECT_EQ(reader->Search("pictures").size(), 1u);
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  first->Close();
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  second->Close();
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  // Physical read close precedes its corresponding independent lease release.
  reader.reset();
  lease.reset();
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, ExclusiveOwnerDeniesReadersAndExistingWriters) {
  Provision();
  auto owner = Writer(Mode::kMaintenance);
  ExpectError(ErrorCode::kBusy, [&] { Writer(); });
  ExpectError(ErrorCode::kBusy,
              [&] { CatalogReadLease reader(policy_, &labels_); });
  owner->Close();
  auto reader = std::make_unique<CatalogReadLease>(policy_, &labels_);
  reader.reset();
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, FiniteBusyRetriesAndUnsupportedNeverAdmit) {
  Provision();
  auto reader = std::make_unique<CatalogReadLease>(policy_, &labels_);
  const auto start = std::chrono::steady_clock::now();
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance, 20ms); });
  EXPECT_GE(std::chrono::steady_clock::now() - start, 20ms);
  EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
  reader.reset();
  labels_.failures = 2;
  EXPECT_NO_THROW(Writer(Mode::kMaintenance, 50ms));
  labels_.failure = EINVAL;
  labels_.failures = 1;
  ExpectError(ErrorCode::kUnsupported,
              [&] { Writer(Mode::kMaintenance, 20ms); });
  labels_.failure = EIO;
  labels_.failures = 1;
  ExpectError(ErrorCode::kIo, [&] { Writer(Mode::kMaintenance); });
}

TEST_F(CoordinatedWriterTest,
       MissingSidecarsCannotBeRecreatedUnderSharedLease) {
  Provision();
  ASSERT_EQ(unlink((path_ + "-wal").c_str()), 0);
  ASSERT_EQ(unlink((path_ + "-shm").c_str()), 0);
  ExpectError(ErrorCode::kPermission, [&] { Writer(); });
  EXPECT_FALSE(std::filesystem::exists(path_ + "-wal"));
  EXPECT_FALSE(std::filesystem::exists(path_ + "-shm"));
  auto repaired = Writer(Mode::kMaintenance);
  repaired->Close();
  EXPECT_NO_THROW(Writer());
}

TEST_F(CoordinatedWriterTest, SchemaMigrationRequiresExclusiveOwnership) {
  Provision();
  {
    Database isolated(path_, Database::Access::kWriter);
    isolated.Exec("DROP TABLE completed; PRAGMA user_version=1");
  }
  ExpectError(ErrorCode::kUnsupported, [&] { Writer(); });
  {
    Database read(path_, Database::Access::kReadOnly);
    Statement version(read.handle(), "PRAGMA user_version");
    ASSERT_TRUE(version.Step());
    EXPECT_EQ(version.Integer(0), 1);
  }
  auto maintenance = Writer(Mode::kMaintenance);
  maintenance->Close();
  EXPECT_NO_THROW(Writer());
}

TEST_F(CoordinatedWriterTest, FailedConstructorClosesBeforeReleasingLease) {
  Provision();
  fail_prepare = true;
  ExpectError(ErrorCode::kDatabase, [&] { Writer(); });
  EXPECT_FALSE(fail_prepare);
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
  {
    Database isolated(path_, Database::Access::kWriter);
    isolated.Exec("PRAGMA user_version=99");
  }
  ExpectError(ErrorCode::kUnsupported, [&] { Writer(Mode::kMaintenance); });
  // Rollback/physical close occurred. An independent EX description is available.
  EXPECT_NO_THROW(CatalogGenerationLease::Acquire(policy_, Mode::kMaintenance,
                                                  0ms, &labels_));
}

TEST_F(CoordinatedWriterTest,
       StatementBusyRetainsConnectionAndLeaseUntilRetry) {
  Provision();
  auto writer = Writer();
  retain_statement = true;
  EXPECT_EQ(writer->Revision(), 0u);
  ASSERT_NE(retained_statement, nullptr);
  ExpectError(ErrorCode::kBusy, [&] { writer->Close(); });
  EXPECT_EQ(writer->Revision(), 0u);
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  ASSERT_EQ(sqlite3_finalize(std::exchange(retained_statement, nullptr)),
            SQLITE_OK);
  writer->Close();
  ExpectError(ErrorCode::kInvalid, [&] { writer->Revision(); });
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, BusyDestructorFailsBeforeOwnershipIsLost) {
  Provision();
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::set_terminate([] { _exit(86); });
    auto writer = Writer();
    retain_statement = true;
    writer->Revision();
    writer.reset();
    _exit(3);
  }
  EXPECT_TRUE(Wait(child, 86));
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, InheritedOperationsRejectAndDestructorFailStops) {
  Provision();
  auto writer = Writer();
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::set_terminate([] { _exit(86); });
    try {
      writer->Revision();
      _exit(3);
    } catch (const Error& error) {
      if (error.code() != ErrorCode::kPermission) _exit(4);
    }
    try {
      writer->Close();
      _exit(5);
    } catch (const Error& error) {
      if (error.code() != ErrorCode::kPermission) _exit(6);
    }
    writer.reset();
    _exit(7);
  }
  EXPECT_TRUE(Wait(child, 86));
  EXPECT_EQ(writer->Revision(), 0u);
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  writer->Close();
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, ChildCloseOnlyDoesNotUnlockParentDescription) {
  Provision();
  auto lease =
      CatalogGenerationLease::Acquire(policy_, Mode::kExisting, 0ms, &labels_);
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    lease.reset();
    try {
      Writer(Mode::kMaintenance);
      _exit(3);
    } catch (const Error& error) {
      _exit(error.code() == ErrorCode::kBusy ? 0 : 4);
    }
  }
  EXPECT_TRUE(Wait(child, 0));
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  lease.reset();
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, ReplacedLockPoisonsWriterEvenAfterNameRestored) {
  Provision();
  auto writer = Writer();
  ASSERT_EQ(
      rename(policy_.lock_path.c_str(), (policy_.lock_path + ".old").c_str()),
      0);
  const int replacement = open(policy_.lock_path.c_str(),
                               O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
  ASSERT_GE(replacement, 0);
  close(replacement);
  ExpectError(ErrorCode::kPermission, [&] { writer->Revision(); });
  ASSERT_EQ(unlink(policy_.lock_path.c_str()), 0);
  ASSERT_EQ(
      rename((policy_.lock_path + ".old").c_str(), policy_.lock_path.c_str()),
      0);
  ExpectError(ErrorCode::kPermission, [&] { writer->Revision(); });
  // Poisoned owners may still physically close their own resources.
  writer->Close();
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, UnsafeModesLinksAndLabelsDenyBeforeBootstrap) {
  ASSERT_EQ(chmod(policy_.lock_path.c_str(), 0666), 0);
  ExpectError(ErrorCode::kPermission, [&] { Writer(Mode::kMaintenance); });
  EXPECT_FALSE(std::filesystem::exists(path_));
  ASSERT_EQ(chmod(policy_.lock_path.c_str(), 0600), 0);
  ASSERT_EQ(link(policy_.lock_path.c_str(), (root_ + "/linked").c_str()), 0);
  ExpectError(ErrorCode::kPermission, [&] { Writer(Mode::kMaintenance); });
  ASSERT_EQ(unlink((root_ + "/linked").c_str()), 0);
  labels_.wrong = true;
  ExpectError(ErrorCode::kPermission, [&] { Writer(Mode::kMaintenance); });
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(CoordinatedWriterTest,
       ParserStagingUsesProtectedWriterAndNeedsFinalizer) {
  Provision();
  std::filesystem::create_directories(root_ + "/res/skills/example");
  std::ofstream(root_ + "/res/skills/example/SKILL.md") << "# Example\n";
  std::ofstream(root_ + "/skill.json")
      << Json({{"version", 1},
               {"key", "example"},
               {"name", "Example"},
               {"desc", "Find pictures"},
               {"resource", "res/skills/example"}});
  auto writer = Writer();
  const std::vector<Metadata> metadata{{Kind::kSkill, "skill.json", ""}};
  ExpectError(ErrorCode::kUnsupported,
              [&] { StagePackage(*writer, "deny", root_, "pkg", metadata); });
  StagePackage(*writer, "offline", root_, "pkg", metadata,
               FinalizationAuthority::kOfflineHarness);
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_TRUE(reader.Search("pictures").empty());
  writer->Finalize("offline", true);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
}

TEST_F(CoordinatedWriterTest, ActionImportCommitsBeforeNotifyWithSharedReader) {
  Provision();
  const auto source_path = root_ + "/source.db";
  {
    Database source(source_path, Database::Access::kWriter);
    source.Exec(
        "CREATE TABLE action(action_name TEXT,json_str TEXT);"
        "CREATE TABLE entity(entity_name TEXT,json_str TEXT);"
        "CREATE TABLE action_provider(action_name TEXT,appid TEXT);"
        "PRAGMA user_version=4;");
    const Json action{{"name", "Media.Find"},
                      {"description", "Find pictures"},
                      {"inputSchema", {{"type", "object"}}}};
    Statement insert(source.handle(), "INSERT INTO action VALUES(?,?)");
    insert.Bind(1, "Media.Find");
    insert.Bind(2, action.dump());
    insert.Step();
  }
  auto writer = Writer();
  auto lease = std::make_unique<CatalogReadLease>(policy_, &labels_);
  Catalog reader(path_, Database::Access::kReadOnly);
  int notifications = 0;
  const auto changed = [&](uint64_t revision) {
    EXPECT_EQ(reader.Revision(), revision);
    EXPECT_EQ(reader.Search("pictures").size(), 1u);
    ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
    ++notifications;
  };
  EXPECT_TRUE(SynchronizeActions(*writer, source_path, changed));
  EXPECT_FALSE(SynchronizeActions(*writer, source_path, changed));
  EXPECT_EQ(notifications, 2);
}
TEST_F(CoordinatedWriterTest, PostOpenMismatchFailsBeforeWriterPublication) {
  Provision();
  const auto sidecar = path_ + "-shm";
  chmod_on_prepare = sidecar.c_str();
  ExpectError(ErrorCode::kPermission, [&] { Writer(); });
  EXPECT_EQ(chmod_on_prepare, nullptr);
  ASSERT_EQ(chmod(sidecar.c_str(), 0600), 0);
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, ForkRetentionBlocksUntilLastInheritedClose) {
  Provision();
  auto lease =
      CatalogGenerationLease::Acquire(policy_, Mode::kExisting, 0ms, &labels_);
  int release[2];
  ASSERT_EQ(pipe2(release, O_CLOEXEC), 0);
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(release[1]);
    struct pollfd ready{release[0], POLLIN, 0};
    if (poll(&ready, 1, 1000) != 1 || !(ready.revents & POLLIN)) _exit(3);
    char byte;
    if (read(release[0], &byte, 1) != 1) _exit(4);
    lease.reset();
    _exit(0);
  }
  close(release[0]);
  lease.reset();
  ExpectError(ErrorCode::kBusy, [&] { Writer(Mode::kMaintenance); });
  ASSERT_EQ(write(release[1], "x", 1), 1);
  close(release[1]);
  EXPECT_TRUE(Wait(child, 0));
  EXPECT_NO_THROW(Writer(Mode::kMaintenance));
}

TEST_F(CoordinatedWriterTest, ExclusiveIsRetainedDuringCompletedPolicyCheck) {
  struct ReentrantLabels : Labels {
    ReadLeasePolicy policy;
    Labels observer;
    bool tested = false;
    std::string Label(int fd) override {
      struct stat info{};
      if (!fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_size > 0) {
        try {
          CatalogReadLease early(policy, &observer);
          ADD_FAILURE() << "EX released before completed file policy";
        } catch (const Error& error) {
          EXPECT_EQ(error.code(), ErrorCode::kBusy);
        }
        tested = true;
      }
      return Labels::Label(fd);
    }
  } operations;
  operations.policy = policy_;
  CoordinatedCatalogWriter owner(policy_, Mode::kMaintenance, 0ms, &operations);
  EXPECT_TRUE(operations.tested);
  owner.Close();
  EXPECT_NO_THROW(CatalogReadLease(policy_, &labels_));
}

TEST_F(CoordinatedWriterTest, AccessAclAndUnsafeSidecarRejectAdmission) {
  // Linux POSIX ACL xattr v2: user_obj rw, named uid r, group_obj none,
  // mask r, other none. Little endian fixture needs no external ACL command.
  policy_.lock_mode = 0640;
  ASSERT_EQ(chmod(policy_.lock_path.c_str(), 0640), 0);
  const unsigned char acl[] = {
      2, 0,   0,   0,   1,   0,   6,  0, 255, 255, 255, 255, 2,   0,  4,
      0, 254, 255, 0,   0,   4,   0,  0, 0,   255, 255, 255, 255, 16, 0,
      4, 0,   255, 255, 255, 255, 32, 0, 0,   0,   255, 255, 255, 255};
  ASSERT_EQ(setxattr(policy_.lock_path.c_str(), "system.posix_acl_access", acl,
                     sizeof(acl), 0),
            0);
  struct stat info{};
  ASSERT_EQ(stat(policy_.lock_path.c_str(), &info), 0);
  ASSERT_EQ(info.st_mode & 07777, 0640u);
  ExpectError(ErrorCode::kPermission, [&] { Writer(Mode::kMaintenance); });
  ASSERT_EQ(removexattr(policy_.lock_path.c_str(), "system.posix_acl_access"),
            0);
  policy_.lock_mode = 0600;
  ASSERT_EQ(chmod(policy_.lock_path.c_str(), 0600), 0);
  Provision();
  ASSERT_EQ(chmod((path_ + "-shm").c_str(), 0666), 0);
  ExpectError(ErrorCode::kPermission, [&] { Writer(); });
  ExpectError(ErrorCode::kPermission, [&] { Writer(Mode::kMaintenance); });
  ASSERT_EQ(chmod((path_ + "-shm").c_str(), 0600), 0);
  EXPECT_NO_THROW(Writer());
}
TEST_F(CoordinatedWriterTest,
       MetadataClosePreservesActiveWalWriteAndMainLocks) {
  Provision();
  auto lease = std::make_unique<CatalogReadLease>(policy_, &labels_);
  auto active = std::make_unique<Catalog>(path_, Database::Access::kWriter);
  active->database().Exec(
      "BEGIN IMMEDIATE; UPDATE catalog_state SET revision=1");
  Probe("begin", true);
  Probe("exclusive", true);
  {
    CatalogReadLease temporary(policy_, &labels_);
  }
  Probe("begin", true);
  Probe("exclusive", true);
  auto second = Writer();
  second->Close();
  Probe("begin", true);
  Probe("exclusive", true);
  fail_prepare = true;
  ExpectError(ErrorCode::kDatabase, [&] { Writer(); });
  Probe("begin", true);
  Probe("exclusive", true);
  LoadSnapshot();
  Probe("begin", true);
  Probe("exclusive", true);
  PinClose();
  Probe("begin", true);
  Probe("exclusive", true);
  active->database().Exec("ROLLBACK");
  active.reset();
  lease.reset();
  Probe("begin", false);
  Probe("exclusive", false);
}

TEST_F(CoordinatedWriterTest,
       MetadataClosePreservesReadSnapshotCheckpointLock) {
  Provision();
  auto writer = Writer();
  writer->Stage("first", "pkg.one", {Make()});
  writer->Finalize("first", true);
  auto lease = std::make_unique<CatalogReadLease>(policy_, &labels_);
  Catalog reader(path_, Database::Access::kReadOnly);
  reader.database().Exec("BEGIN");
  ASSERT_EQ(reader.Revision(), 1u);  // Retained WAL read snapshot.
  auto changed = Make();
  changed.desc = "Second pictures";
  writer->Stage("second", "pkg.one", {changed});
  writer->Finalize("second", true);
  EXPECT_EQ(reader.Revision(), 1u);
  Probe("checkpoint", true);
  {
    CatalogReadLease temporary(policy_, &labels_);
  }
  Probe("checkpoint", true);
  auto second = Writer();
  second->Close();
  Probe("checkpoint", true);
  fail_prepare = true;
  ExpectError(ErrorCode::kDatabase, [&] { Writer(); });
  Probe("checkpoint", true);
  LoadSnapshot();
  Probe("checkpoint", true);
  PinClose();
  Probe("checkpoint", true);
  reader.database().Exec("ROLLBACK");
  EXPECT_EQ(reader.Revision(), 2u);
  Probe("checkpoint", false);
}

TEST_F(CoordinatedWriterTest,
       PathMetadataReadsRealAttributesAndRejectsBadPins) {
  Provision();
  // No SQLite object lives here. This attribute is fixture-only, not SMACK policy.
  const char value[] = "metadata";
  const int installed =
      setxattr(path_.c_str(), "user.capmgr_metadata", value, sizeof(value), 0);
  const int install_error = errno;
  ASSERT_TRUE(installed == 0 || install_error == EOPNOTSUPP)
      << "Fixture xattr errno=" << install_error;
  std::cout << "USER_METADATA_XATTR="
            << (installed == 0 ? "SUPPORTED" : "UNSUPPORTED") << std::endl;
  const int fd = open(path_.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  char bytes[32]{};
  if (installed == 0) {
    EXPECT_EQ(
        MetadataAttribute(fd, "user.capmgr_metadata", bytes, sizeof(bytes)),
        static_cast<ssize_t>(sizeof(value)));
    EXPECT_EQ(std::string(bytes), "metadata");
  } else {
    errno = 0;
    EXPECT_EQ(
        MetadataAttribute(fd, "user.capmgr_metadata", bytes, sizeof(bytes)),
        -1);
    EXPECT_EQ(errno, EOPNOTSUPP);
  }
  errno = 0;
  EXPECT_EQ(MetadataAttribute(fd, "user.capmgr_absent", bytes, sizeof(bytes)),
            -1);
  EXPECT_EQ(errno, installed == 0 ? ENODATA : EOPNOTSUPP);
  // ACL lookup uses the real default metadata route on either filesystem.
  errno = 0;
  EXPECT_EQ(
      MetadataAttribute(fd, "system.posix_acl_access", bytes, sizeof(bytes)),
      -1);
  EXPECT_EQ(errno, ENODATA);
  ReadLeaseOperations real;
  try {
    const auto label = real.Label(fd);
    EXPECT_FALSE(label.empty());
    std::cout << "REAL_METADATA_LABEL=" << label << std::endl;
  } catch (const Error& error) {
    EXPECT_EQ(error.code(), ErrorCode::kPermission);
    EXPECT_EQ(getenv("CAPMGR_REQUIRE_METADATA_TESTS"), nullptr)
        << "Required native SMACK metadata unavailable";
  }
  ASSERT_EQ(chmod(path_.c_str(), 0000), 0);
  if (getuid() == 0) {
    // Only an already-created O_PATH metadata pin survives; all SQLite was
    // physically closed in Provision. No policy/label/mount changes occur.
    const auto child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
      if (setgroups(0, nullptr) || setresgid(301, 301, 301) ||
          setresuid(301, 301, 301) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        _exit(3);
      __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
      __user_cap_data_struct caps[2]{};
      // setresuid clears effective/permitted, but target root may have inherited
      // inheritable bits. Explicitly remove them in this child before verifying.
      if (syscall(SYS_capset, &header, caps)) _exit(6);
      if (syscall(SYS_capget, &header, caps) || caps[0].effective ||
          caps[1].effective || caps[0].permitted || caps[1].permitted ||
          caps[0].inheritable || caps[1].inheritable)
        _exit(4);
      errno = 0;
      const auto size =
          MetadataAttribute(fd, "user.capmgr_metadata", bytes, sizeof(bytes));
      _exit(size < 0 && errno == EACCES ? 0 : 5);
    }
    EXPECT_TRUE(Wait(child, 0));
  } else {
    errno = 0;
    EXPECT_EQ(
        MetadataAttribute(fd, "user.capmgr_metadata", bytes, sizeof(bytes)),
        -1);
    EXPECT_EQ(errno, EACCES);
  }
  ASSERT_EQ(chmod(path_.c_str(), 0600), 0);
  const int bad_flags = open(path_.c_str(), O_PATH | O_CLOEXEC);
  ASSERT_GE(bad_flags, 0);
  EXPECT_THROW(ValidateDataPin(bad_flags), Error);
  close(bad_flags);
  ASSERT_EQ(fcntl(fd, F_SETFD, 0), 0);
  EXPECT_THROW(
      MetadataAttribute(fd, "user.capmgr_metadata", bytes, sizeof(bytes)),
      Error);
  close(fd);
  EXPECT_THROW(
      MetadataAttribute(-1, "user.capmgr_metadata", bytes, sizeof(bytes)),
      Error);
  const auto symlink = root_ + "/symlink";
  ASSERT_EQ(symlinkat(path_.c_str(), AT_FDCWD, symlink.c_str()), 0);
  const int link_fd = open(symlink.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
  ASSERT_GE(link_fd, 0);
  EXPECT_THROW(
      MetadataAttribute(link_fd, "user.capmgr_metadata", bytes, sizeof(bytes)),
      Error);
  close(link_fd);
  const int directory =
      open(policy_.directory.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
  ASSERT_GE(directory, 0);
  EXPECT_NO_THROW(RequireDataPinSupport(directory));
  close(directory);
}
TEST_F(CoordinatedWriterTest, LoaderRejectionsPreserveActiveWalWriterLocks) {
  Provision();
  // Declare raw metadata descriptors BEFORE SQLite so all unwind paths physically
  // close SQLite first. Keep the invalid borrowed DB/SHM FDs open during probes.
  HeldFd directory, main, shm;
  auto lease = std::make_unique<CatalogReadLease>(policy_, &labels_);
  auto active = std::make_unique<Database>(path_, Database::Access::kWriter);
  active->Exec(
      "PRAGMA user_version=99; BEGIN IMMEDIATE;"
      "UPDATE catalog_state SET revision=1");
  directory.value =
      open(policy_.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  main.value = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
  shm.value = open((path_ + "-shm").c_str(), O_RDONLY | O_CLOEXEC);
  ASSERT_GE(directory.value, 0);
  ASSERT_GE(main.value, 0);
  ASSERT_GE(shm.value, 0);
  Probe("begin", true);
  for (int fd : {main.value, shm.value}) {
    ExpectError(ErrorCode::kPermission, [&] {
      LoadWorkerCatalog(fd, {getuid(), getgid(), 0700, 0600});
    });
    EXPECT_GE(fcntl(fd, F_GETFD), 0);  // Caller-owned FD was not closed.
    Probe("begin", true);
    Probe("exclusive", true);
  }
  // Directory validation/data pins succeeded, actual SQLite was opened, then
  // schema99 rejection must unwind without dropping the active connection locks.
  ExpectError(ErrorCode::kUnsupported, [&] {
    LoadWorkerCatalog(directory.value, {getuid(), getgid(), 0700, 0600});
  });
  Probe("begin", true);
  Probe("exclusive", true);
  active->Exec("ROLLBACK");
  active.reset();
  Probe("begin", false);
}

TEST_F(CoordinatedWriterTest, LoaderRejectionsPreserveRetainedReadSnapshot) {
  Provision();
  HeldFd directory, main, shm;
  auto lease = std::make_unique<CatalogReadLease>(policy_, &labels_);
  auto writer = std::make_unique<Catalog>(path_, Database::Access::kWriter);
  Publish(*writer, "pkg.one", {Make()});
  writer->database().Exec("PRAGMA user_version=99");
  auto reader = std::make_unique<Database>(path_, Database::Access::kReadOnly);
  reader->Exec("BEGIN");
  ASSERT_EQ(reader->Revision(), 1u);
  writer->database().Exec(
      "BEGIN IMMEDIATE; UPDATE catalog_state SET revision=2; COMMIT");
  directory.value =
      open(policy_.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  main.value = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
  shm.value = open((path_ + "-shm").c_str(), O_RDONLY | O_CLOEXEC);
  ASSERT_GE(directory.value, 0);
  ASSERT_GE(main.value, 0);
  ASSERT_GE(shm.value, 0);
  Probe("checkpoint", true);
  for (int fd : {main.value, shm.value}) {
    ExpectError(ErrorCode::kPermission, [&] {
      LoadWorkerCatalog(fd, {getuid(), getgid(), 0700, 0600});
    });
    EXPECT_GE(fcntl(fd, F_GETFD), 0);
    Probe("checkpoint", true);
  }
  ExpectError(ErrorCode::kUnsupported, [&] {
    LoadWorkerCatalog(directory.value, {getuid(), getgid(), 0700, 0600});
  });
  Probe("checkpoint", true);
  reader->Exec("ROLLBACK");
  reader.reset();
  writer.reset();
  Probe("checkpoint", false);
}
}  // namespace
