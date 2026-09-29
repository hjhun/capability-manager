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
#include "launcher/worker_catalog.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace capmgr;

namespace {

class WorkerCatalogTest : public CatalogTest {
 protected:
  int directory = -1;
  std::unique_ptr<Catalog> writer;
  void SetUp() override {
    CatalogTest::SetUp();
    chmod(root_.c_str(), 0700);
    directory = open(root_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    writer = std::make_unique<Catalog>(path_, Database::Access::kWriter);
    // Establish all sidecars and retain this independent writer connection.
    auto e = Make("fixture", "pkg.cli", Kind::kCli);
    e.executable = "/usr/bin/fixed-cli";
    Publish(*writer, e.owner, {e});
    Secure();
  }

  WorkerCatalogFilePolicy Policy() {
    return {geteuid(), getegid(), 0700, 0600};
  }

  void Secure() {
    for (auto suffix : {"", "-wal", "-shm"})
      ASSERT_EQ(chmod((path_ + suffix).c_str(), 0600), 0);
  }

  void TearDown() override {
    writer.reset();
    if (directory >= 0) close(directory);
    CatalogTest::TearDown();
  }
};

}  // namespace

TEST_F(WorkerCatalogTest, ReadsLiveWalAndPinsOneBoundedCliRevision) {
  auto first = LoadWorkerCatalog(directory, Policy());
  EXPECT_EQ(first.revision, 1u);
  EXPECT_EQ(first.registry.Resolve("cli:fixture"), "/usr/bin/fixed-cli");
  auto e = Make("fixture", "pkg.cli", Kind::kCli);
  e.executable = "/usr/bin/updated-cli";
  Publish(*writer, e.owner, {e});
  Secure();
  auto second = LoadWorkerCatalog(directory, Policy());
  EXPECT_EQ(second.revision, 2u);
  EXPECT_EQ(second.registry.Resolve("cli:fixture"), "/usr/bin/updated-cli");
  EXPECT_EQ(first.registry.Resolve("cli:fixture"),
            "/usr/bin/fixed-cli");  // caller must invalidate; no hidden I/O
}

TEST_F(WorkerCatalogTest, PendingRegistrationAndOtherKindsNeverEnterSnapshot) {
  auto pending = Make("pending", "pkg.pending", Kind::kCli);
  pending.executable = "/pending";
  writer->Stage("pending", pending.owner, {pending});
  auto skill = Make("skill", "pkg.skill");
  Publish(*writer, skill.owner, {skill});
  Secure();
  auto snapshot = LoadWorkerCatalog(directory, Policy());
  EXPECT_TRUE(snapshot.registry.Resolve("cli:pending").empty());
  EXPECT_TRUE(snapshot.registry.Resolve("skill:skill").empty());
  EXPECT_EQ(snapshot.registry.Resolve("cli:fixture"), "/usr/bin/fixed-cli");
}

TEST_F(WorkerCatalogTest, RejectsGroupWritableAndWrongOwnerStorage) {
  for (auto mode : {0770, 0777, 04700}) {
    ASSERT_EQ(chmod(root_.c_str(), mode), 0);
    EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
  }

  ASSERT_EQ(chmod(root_.c_str(), 0700), 0);
  for (auto suffix : {"", "-wal", "-shm"}) {
    ASSERT_EQ(chmod((path_ + suffix).c_str(), 0660), 0);
    EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
    ASSERT_EQ(chmod((path_ + suffix).c_str(), 0600), 0);
  }

  auto wrong = Policy();
  wrong.writer = geteuid() == 0 ? 1 : 0;
  EXPECT_THROW(LoadWorkerCatalog(directory, wrong), Error);
  wrong = Policy();
  wrong.group = getegid() == 0 ? 1 : 0;
  EXPECT_THROW(LoadWorkerCatalog(directory, wrong), Error);
}

TEST_F(WorkerCatalogTest,
       RejectsMissingSymlinkAndHardlinkedSidecarBeforeSqliteOpen) {
  auto shm = path_ + "-shm", saved = path_ + "-shm.saved";
  ASSERT_EQ(rename(shm.c_str(), saved.c_str()), 0);
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
  ASSERT_EQ(symlink(saved.c_str(), shm.c_str()), 0);
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
  ASSERT_EQ(unlink(shm.c_str()), 0);
  ASSERT_EQ(link(saved.c_str(), shm.c_str()), 0);
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
  ASSERT_EQ(unlink(shm.c_str()), 0);
  ASSERT_EQ(rename(saved.c_str(), shm.c_str()), 0);
  EXPECT_NO_THROW(LoadWorkerCatalog(directory, Policy()));
}

TEST_F(WorkerCatalogTest, RejectsUnboundedCatalogBeforeWorkerAdmission) {
  std::vector<Entry> entries;
  for (int i = 0; i < 257; ++i) {
    auto e = Make("item" + std::to_string(i), "pkg.cli", Kind::kCli);
    e.executable = "/fixed";
    entries.push_back(e);
  }

  Publish(*writer, "pkg.cli", entries);
  Secure();
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
}

TEST_F(WorkerCatalogTest, AnchoredDirectorySurvivesParentPathRename) {
  auto renamed = root_ + "-moved";
  ASSERT_EQ(rename(root_.c_str(), renamed.c_str()), 0);
  root_ = renamed;
  path_ = root_ + "/catalog.db";
  auto snapshot = LoadWorkerCatalog(directory, Policy());
  EXPECT_EQ(snapshot.registry.Resolve("cli:fixture"), "/usr/bin/fixed-cli");
}

TEST_F(WorkerCatalogTest,
       ReadOnlySnapshotLeavesCatalogDataAndRevisionUnchanged) {
  auto before = writer->Get("cli:fixture");
  auto rev = writer->Revision();
  LoadWorkerCatalog(directory, Policy());
  EXPECT_EQ(writer->Revision(), rev);
  EXPECT_EQ(writer->Get("cli:fixture"), before);
}

TEST_F(WorkerCatalogTest,
       ProvisionedGroupReadableModesRemainWritableByLiveOwner) {
  ASSERT_EQ(chmod(root_.c_str(), 02750), 0);
  for (auto suffix : {"", "-wal", "-shm"})
    ASSERT_EQ(chmod((path_ + suffix).c_str(), 0640), 0);
  auto policy = Policy();
  policy.directory_mode = 02750;
  policy.file_mode = 0640;
  auto first = LoadWorkerCatalog(directory, policy);
  EXPECT_EQ(first.revision, 1u);
  auto e = Make("fixture", "pkg.cli", Kind::kCli);
  e.executable = "/updated";
  Publish(*writer, e.owner, {e});
  auto second = LoadWorkerCatalog(directory, policy);
  EXPECT_EQ(second.revision, 2u);
  EXPECT_EQ(second.registry.Resolve("cli:fixture"), "/updated");
}

TEST_F(WorkerCatalogTest,
       MissingSidecarsRequireVerifiedRecreationBeforeReload) {
  writer.reset();
  // Persistence keeps sidecars after ordinary close. Simulate explicit offline
  // removal with no remaining connections; loader must still reject the gap.
  ASSERT_TRUE(std::filesystem::remove(path_ + "-wal"));
  ASSERT_TRUE(std::filesystem::remove(path_ + "-shm"));
  EXPECT_FALSE(std::filesystem::exists(path_ + "-wal"));
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
  writer = std::make_unique<Catalog>(path_, Database::Access::kWriter);
  auto e = Make("fixture", "pkg.cli", Kind::kCli);
  e.executable = "/recreated";
  Publish(*writer, e.owner, {e});
  Secure();
  auto snapshot = LoadWorkerCatalog(directory, Policy());
  EXPECT_EQ(snapshot.revision, 2u);
  EXPECT_EQ(snapshot.registry.Resolve("cli:fixture"), "/recreated");
}

TEST_F(WorkerCatalogTest,
       UnsupportedSchemaAndCorruptIdentityDoNotReachRegistry) {
  writer->database().Exec("PRAGMA user_version=1");
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
  writer->database().Exec("PRAGMA user_version=2");
  writer->database().Exec("UPDATE capability SET owner='' WHERE kind=3");
  EXPECT_THROW(LoadWorkerCatalog(directory, Policy()), Error);
}
