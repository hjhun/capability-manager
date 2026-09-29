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

#include "fixture.hh"
#include "api/client.hh"
#include "api/read_admission.hh"
#include "catalog/read_lease.hh"

#include <fcntl.h>

#include <future>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace capmgr;
using namespace std::chrono_literals;

namespace {

struct Labels : ReadLeaseOperations {
  std::string label = "fixture";
  std::string Label(int) override { return label; }
};

class LeaseTest : public CatalogTest {
 protected:
  Labels labels;
  std::string directory, lock_path;
  std::unique_ptr<Catalog> writer;
  void SetUp() override {
    CatalogTest::SetUp();
    directory = root_ + "/catalog";
    lock_path = root_ + "/lease";
    ASSERT_EQ(mkdir(directory.c_str(), 0700), 0);
    path_ = directory + "/catalog.db";
    int fd =
        open(lock_path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    close(fd);
    writer = std::make_unique<Catalog>(path_, Database::Access::kWriter);
    auto e = Make("fixture", "pkg", Kind::kCli);
    e.executable = "/fixed";
    Publish(*writer, e.owner, {e});
    Secure();
  }

  void TearDown() override {
    writer.reset();
    CatalogTest::TearDown();
  }

  void Secure() {
    for (auto suffix : {"", "-wal", "-shm"})
      ASSERT_EQ(chmod((path_ + suffix).c_str(), 0600), 0);
  }

  ReadLeasePolicy Policy() {
    return {directory, lock_path, geteuid(), geteuid(), getegid(), getegid(),
            0700,      0600,      0600,      "fixture", "fixture", "fixture"};
  }

  auto Lease() { return std::make_unique<CatalogReadLease>(Policy(), &labels); }
  bool Exclusive() {
    int fd = open(lock_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    int result = fcntl(fd, F_OFD_SETLK, &lock);
    close(fd);
    return result == 0;
  }
  class Channel : public CatalogAdmissionChannel {
   public:
    explicit Channel(LeaseTest& test) : test_(test) {}
    std::string AuthorizeCatalog() override {
      issuer = test_.Lease();
      auto descriptor = issuer->Descriptor();
      if (!override_descriptor.empty()) descriptor = override_descriptor;
      if (lose_after_receipt) live = false;
      return descriptor;
    }
    void CheckSameLive() override {
      ++checks;
      if (!live || checks == fail_at)
        throw Error(ErrorCode::kPermission, "Connection lost or changed");
    }
    void ConfirmCatalog(std::string_view descriptor) override {
      ++confirms;
      if (!live || !issuer || fail_confirm || confirmed)
        throw Error(ErrorCode::kPermission, "Confirmation denied or lost");
      issuer->MatchDescriptor(descriptor);
      confirmed = true;
      issuer.reset();
      overlap = !test_.Exclusive();
    }
    void Finish() override {
      ++finishes;
      if (!confirmed)
        throw Error(ErrorCode::kPermission, "Confirmation missing");
      live = false;
      if (fail_finish) throw Error(ErrorCode::kPermission, "Teardown failed");
    }
    std::unique_ptr<CatalogReadLease> issuer;
    std::string override_descriptor;
    int checks = 0, confirms = 0, finishes = 0, fail_at = 0;
    bool live = true, overlap = false, fail_finish = false,
         lose_after_receipt = false, fail_confirm = false, confirmed = false;

   private:
    LeaseTest& test_;
  };
  class Gate : public AccessGate {
   public:
    explicit Gate(LeaseTest& test) : test(test) {}
    std::string AuthorizeAndGetDatabase() override {
      throw Error(ErrorCode::kPermission, "No path-only fallback");
    }
    std::unique_ptr<ReadAccess> AuthorizeReadAccess() override {
      return test.Lease();
    }

   private:
    LeaseTest& test;
  };
};

}  // namespace

TEST_F(LeaseTest, IndependentReadersExcludeMaintenanceUntilBothRelease) {
  EXPECT_TRUE(Exclusive());
  auto first = Lease(), second = Lease();
  EXPECT_FALSE(Exclusive());
  first.reset();
  EXPECT_FALSE(Exclusive());
  second.reset();
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, ExclusiveMaintenanceRejectsReadAdmissionWithoutWaiting) {
  int fd = open(lock_path.c_str(), O_RDWR | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  struct flock lock{};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  ASSERT_EQ(fcntl(fd, F_OFD_SETLK, &lock), 0);
  Gate gate(*this);
  capmgr_client_h client = nullptr;
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_BUSY);
  EXPECT_EQ(client, nullptr);
  EXPECT_LT(std::chrono::steady_clock::now() - start, 200ms);
  close(fd);
}

TEST_F(LeaseTest, InvalidPoliciesAndWriterDirectoryLockFailClosed) {
  auto p = Policy();
  p.lock_path = directory + "/lease";
  EXPECT_THROW(CatalogReadLease(p, &labels), Error);
  p = Policy();
  p.file_label.clear();
  EXPECT_THROW(CatalogReadLease(p, &labels), Error);
  p = Policy();
  p.lock_mode = 0660;
  EXPECT_THROW(CatalogReadLease(p, &labels), Error);
  p = Policy();
  p.directory += "/../catalog";
  EXPECT_THROW(CatalogReadLease(p, &labels), Error);
  p = Policy();
  ++p.writer;
  EXPECT_THROW(CatalogReadLease(p, &labels), Error);
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, SpecialModesAndMissingSidecarsRejectBeforeHandlePublication) {
  Gate gate(*this);
  for (auto path : {lock_path, path_, path_ + "-wal", path_ + "-shm"}) {
    ASSERT_EQ(chmod(path.c_str(), 04600), 0);
    capmgr_client_h client = nullptr;
    EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
    EXPECT_EQ(client, nullptr);
    ASSERT_EQ(chmod(path.c_str(), 0600), 0);
    EXPECT_TRUE(Exclusive());
  }

  ASSERT_EQ(rename((path_ + "-shm").c_str(), (path_ + "-saved").c_str()), 0);
  capmgr_client_h client = nullptr;
  EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(client, nullptr);
  ASSERT_EQ(rename((path_ + "-saved").c_str(), (path_ + "-shm").c_str()), 0);
}

TEST_F(LeaseTest, ReplacementAndPolicyFailurePoisonExistingLease) {
  auto lease = Lease();
  labels.label = "different";
  EXPECT_THROW(lease->Check(), Error);
  labels.label = "fixture";
  EXPECT_THROW(lease->Check(), Error);
  lease.reset();
  lease = Lease();
  ASSERT_EQ(rename(lock_path.c_str(), (lock_path + ".saved").c_str()), 0);
  int fd = open(lock_path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
  ASSERT_GE(fd, 0);
  close(fd);
  EXPECT_THROW(lease->Check(), Error);
  ASSERT_EQ(unlink(lock_path.c_str()), 0);
  ASSERT_EQ(rename((lock_path + ".saved").c_str(), lock_path.c_str()), 0);
  EXPECT_THROW(lease->Check(), Error);
}

TEST_F(LeaseTest, SymlinkOrHardlinkedFilesAreNeverAccepted) {
  auto saved = path_ + "-wal.saved";
  ASSERT_EQ(rename((path_ + "-wal").c_str(), saved.c_str()), 0);
  ASSERT_EQ(symlink(saved.c_str(), (path_ + "-wal").c_str()), 0);
  EXPECT_THROW(Lease(), Error);
  ASSERT_EQ(unlink((path_ + "-wal").c_str()), 0);
  ASSERT_EQ(link(saved.c_str(), (path_ + "-wal").c_str()), 0);
  EXPECT_THROW(Lease(), Error);
  ASSERT_EQ(unlink((path_ + "-wal").c_str()), 0);
  ASSERT_EQ(rename(saved.c_str(), (path_ + "-wal").c_str()), 0);
}

TEST_F(LeaseTest, LiveWalCommitsRemainVisibleThroughPublicLocalQueries) {
  Gate gate(*this);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  EXPECT_FALSE(Exclusive());
  char* detail = nullptr;
  ASSERT_EQ(capmgr_client_get_capability(client, "cli:fixture", &detail), 0);
  std::free(detail);
  auto entry = Make("new", "pkg", Kind::kCli);
  entry.executable = "/fixed";
  Publish(*writer, entry.owner, {entry});
  ASSERT_EQ(capmgr_client_get_capability(client, "cli:new", &detail), 0);
  std::free(detail);
  EXPECT_EQ(capmgr_client_get_capability(client, "cli:fixture", &detail),
            CAPMGR_ERROR_NOT_FOUND);
  EXPECT_EQ(detail, nullptr);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, PersistentWriterCloseAllowsFreshReadOnlyWalOpen) {
  int persistent = 1;
  ASSERT_EQ(sqlite3_file_control(writer->database().handle(), "main",
                                 SQLITE_FCNTL_PERSIST_WAL, &persistent),
            SQLITE_OK);
  writer.reset();
  ASSERT_TRUE(std::filesystem::exists(path_ + "-wal"));
  ASSERT_TRUE(std::filesystem::exists(path_ + "-shm"));
  auto lease = Lease();
  {
    Catalog reader(lease->Path(), Database::Access::kReadOnly);
    lease->Opened(reader.database());
    EXPECT_EQ(reader.Revision(), 1u);
    EXPECT_THROW(reader.database().Exec("DELETE FROM capability"), Error);
    EXPECT_FALSE(Exclusive());
  }

  lease.reset();
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, OpenValidationRejectsWriterAndWrongDatabase) {
  auto lease = Lease();
  EXPECT_THROW(lease->Opened(writer->database()), Error);
  lease = Lease();
  Catalog other(root_ + "/other.db", Database::Access::kWriter);
  Catalog reader(root_ + "/other.db", Database::Access::kReadOnly);
  EXPECT_THROW(lease->Opened(reader.database()), Error);
}

TEST_F(LeaseTest, QueryRejectsReplacedFileAndLeavesOutputsNull) {
  Gate gate(*this);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  ASSERT_EQ(rename((path_ + "-wal").c_str(), (path_ + "-saved").c_str()), 0);
  char* detail = nullptr;
  EXPECT_EQ(capmgr_client_get_capability(client, "cli:fixture", &detail),
            CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(detail, nullptr);
  capmgr_search_results_h results = nullptr;
  EXPECT_EQ(capmgr_client_search_capabilities(client, "fixture",
                                              CAPMGR_KIND_ALL, &results),
            CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(results, nullptr);
  int calls = 0;
  EXPECT_EQ(capmgr_client_foreach_capability(
                client, CAPMGR_KIND_ALL,
                [](const char*, void* data) {
                  ++*static_cast<int*>(data);
                  return true;
                },
                &calls),
            CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  ASSERT_EQ(rename((path_ + "-saved").c_str(), (path_ + "-wal").c_str()), 0);
}

TEST_F(LeaseTest, DestroyIoRetainsLeaseUntilTrackedWorkerAndSqliteClose) {
  struct Backend : ExecutionBackend {
    std::promise<void> started, release;
    std::shared_future<void> gate = release.get_future().share();
    void Admit(const Entry&, const Request&) override {}
    std::string Execute(const Entry&, const Request&, const std::atomic<bool>&,
                        const Dispatcher::Emit&) override {
      started.set_value();
      gate.wait();
      return R"({"jsonrpc":"2.0","id":1,"result":{}})";
    }
  };
  Gate gate(*this);
  auto backend = std::make_shared<Backend>();
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, backend, &client), 0);
  uint64_t token = 0;
  ASSERT_EQ(
      capmgr_client_execute(
          client,
          R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})",
          [](uint64_t, const char*, bool, void*) {}, nullptr, &token),
      0);
  ASSERT_EQ(backend->started.get_future().wait_for(2s),
            std::future_status::ready);
  EXPECT_EQ(capmgr_client_destroy(client), CAPMGR_ERROR_IO);
  EXPECT_FALSE(Exclusive());
  backend->release.set_value();
  int result = CAPMGR_ERROR_IO;
  for (int i = 0; i < 20 && result == CAPMGR_ERROR_IO; ++i)
    result = capmgr_client_destroy(client);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, InheritedLeaseRejectsUseButChildRetainsMaintenanceLock) {
  auto lease = Lease();
  int commands[2], reply[2];
  ASSERT_EQ(pipe(commands), 0);
  ASSERT_EQ(pipe(reply), 0);
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    alarm(5);
    close(commands[1]);
    close(reply[0]);
    try {
      lease->Check();
      _exit(2);
    } catch (const Error&) {
    }
    char value = 'r';
    if (write(reply[1], &value, 1) != 1) _exit(3);
    if (read(commands[0], &value, 1) != 1) _exit(4);
    lease.reset();
    _exit(0);
  }

  close(commands[0]);
  close(reply[1]);
  char value;
  ASSERT_EQ(read(reply[0], &value, 1), 1);
  lease.reset();
  EXPECT_FALSE(Exclusive());
  ASSERT_EQ(write(commands[1], "x", 1), 1);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(Exclusive());
  close(commands[1]);
  close(reply[0]);
}

TEST_F(LeaseTest, PostOpenAdmissionFailurePublishesNoHandleAndReleasesLease) {
  class Refused : public ReadAccess {
   public:
    explicit Refused(std::unique_ptr<ReadAccess> lease)
        : lease_(std::move(lease)) {}
    const std::string& Path() const noexcept override { return lease_->Path(); }
    void Check() override { lease_->Check(); }
    void Opened(Database& db) override {
      lease_->Opened(db);
      throw Error(ErrorCode::kPermission, "Lost handoff authorization");
    }

   private:
    std::unique_ptr<ReadAccess> lease_;
  };
  class FailingGate : public AccessGate {
   public:
    std::unique_ptr<ReadAccess> lease;
    std::string AuthorizeAndGetDatabase() override {
      throw std::logic_error("path fallback");
    }
    std::unique_ptr<ReadAccess> AuthorizeReadAccess() override {
      return std::make_unique<Refused>(std::move(lease));
    }
  } gate;
  gate.lease = Lease();
  capmgr_client_h client = nullptr;
  EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(client, nullptr);
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, InheritedClientRejectsEveryCallBeforeDispatcherOrSqliteUse) {
  Gate gate(*this);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    alarm(3);
    char* detail = nullptr;
    capmgr_search_results_h results = nullptr;
    uint64_t token = 99;
    const int denied = CAPMGR_ERROR_PERMISSION_DENIED;
    if (capmgr_client_destroy(client) != denied ||
        capmgr_client_cancel(client, 1) != denied ||
        capmgr_client_set_changed_callback(client, nullptr, nullptr) !=
            denied ||
        capmgr_client_remount_resources(client, "/unused") != denied)
      _exit(2);
    if (capmgr_client_get_capability(client, "cli:fixture", &detail) !=
            denied ||
        detail ||
        capmgr_client_search_capabilities(client, "fixture", CAPMGR_KIND_ALL,
                                          &results) != denied ||
        results)
      _exit(3);
    if (capmgr_client_foreach_capability(
            client, CAPMGR_KIND_ALL,
            [](const char*, void*) {
              _exit(5);
              return false;
            },
            nullptr) != denied)
      _exit(4);
    if (capmgr_client_execute(
            client, "{}", [](uint64_t, const char*, bool, void*) { _exit(7); },
            nullptr, &token) != denied ||
        token)
      _exit(6);
    _exit(0);
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(Exclusive());
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest, CanonicalDescriptorMatchesOnlyThePinnedFileGeneration) {
  auto lease = Lease();
  auto receipt = lease->Descriptor();
  ASSERT_EQ(receipt.size(), 165u);
  EXPECT_EQ(receipt.substr(0, 5), "CMR1:");
  EXPECT_NO_THROW(lease->MatchDescriptor(receipt));
  for (auto bad :
       {std::string("CMR1:"), receipt + "0",
        std::string("CMR2:") + receipt.substr(5),
        receipt.substr(0, 164) + (receipt.back() == '0' ? "1" : "0")}) {
    auto other = Lease();
    EXPECT_THROW(other->MatchDescriptor(bad), Error);
    EXPECT_THROW(other->Check(), Error);
  }
}

TEST_F(LeaseTest,
       HandoffHasOverlappingLeasesThenNoTransportInLocalQueriesOrDestroy) {
  Channel channel(*this);
  LeasedCatalogGate gate(Policy(), channel, &labels);
  capmgr_client_h client = nullptr;
  EXPECT_THROW(gate.AuthorizeAndGetDatabase(), Error);
  ASSERT_EQ(CreateClient(gate, &client), 0);
  EXPECT_EQ(channel.confirms, 1);
  EXPECT_EQ(channel.finishes, 1);
  EXPECT_TRUE(channel.overlap);
  int checks = channel.checks;
  EXPECT_FALSE(Exclusive());
  char* detail = nullptr;
  EXPECT_EQ(capmgr_client_get_capability(client, "cli:fixture", &detail), 0);
  std::free(detail);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  EXPECT_EQ(channel.checks, checks);
  EXPECT_TRUE(Exclusive());
}

TEST_F(LeaseTest,
       HandoffRejectsLossAtEveryObservedValidationPointBeforePublication) {
  int checks = 0;
  {
    Channel channel(*this);
    LeasedCatalogGate gate(Policy(), channel, &labels);
    capmgr_client_h client = nullptr;
    ASSERT_EQ(CreateClient(gate, &client), 0);
    checks = channel.checks;
    ASSERT_EQ(capmgr_client_destroy(client), 0);
  }

  ASSERT_GT(checks, 0);
  for (int at = 1; at <= checks; ++at) {
    Channel channel(*this);
    channel.fail_at = at;
    LeasedCatalogGate gate(Policy(), channel, &labels);
    capmgr_client_h client = nullptr;
    EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
    EXPECT_EQ(client, nullptr);
    EXPECT_EQ(channel.finishes, 0);
    channel.issuer.reset();
    EXPECT_TRUE(Exclusive());
  }
}

TEST_F(LeaseTest, HandoffRejectsReceiptLossMalformedIdentityAndFailedFinish) {
  for (int mode = 0; mode < 4; ++mode) {
    Channel channel(*this);
    if (mode == 0) channel.lose_after_receipt = true;
    if (mode == 1) channel.override_descriptor = "bad";
    if (mode == 2) channel.override_descriptor = std::string(165, '0');
    if (mode == 3) channel.fail_finish = true;
    LeasedCatalogGate gate(Policy(), channel, &labels);
    capmgr_client_h client = nullptr;
    EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
    EXPECT_EQ(client, nullptr);
    channel.issuer.reset();
    EXPECT_TRUE(Exclusive());
  }
}

TEST_F(LeaseTest, ConnectionMustStillBeLiveAfterLocalSqliteOpen) {
  Channel channel(*this);
  LeasedCatalogGate gate(Policy(), channel, &labels);
  auto access = gate.AuthorizeReadAccess();
  Catalog local(access->Path(), Database::Access::kReadOnly);
  channel.live = false;
  EXPECT_THROW(access->Opened(local.database()), Error);
  EXPECT_EQ(channel.finishes, 0);
}

TEST_F(LeaseTest, ConfirmationFailureDespiteLiveChecksNeverPublishesHandle) {
  Channel channel(*this);
  channel.fail_confirm = true;
  LeasedCatalogGate gate(Policy(), channel, &labels);
  capmgr_client_h client = nullptr;
  EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(client, nullptr);
  EXPECT_TRUE(channel.live);
  EXPECT_EQ(channel.confirms, 1);
  EXPECT_EQ(channel.finishes, 0);
  channel.issuer.reset();
  EXPECT_TRUE(Exclusive());
}
