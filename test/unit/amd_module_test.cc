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
// SPDX-License-Identifier: Apache-2.0

#include "amd-module/catalog_service.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <thread>

#include "amd-module/module_config.hh"
#include "amd-module/module_thread.hh"
#include "fixture.hh"

namespace {

using namespace capmgr;

struct Labels : GenerationLeaseOperations {
  std::string Label(int) override { return "Fixture"; }
};

class AmdModuleTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    ASSERT_EQ(mkdir((root_ + "/catalog").c_str(), 0700), 0);
    int lock = open((root_ + "/generation.lock").c_str(),
                    O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    ASSERT_GE(lock, 0);
    ASSERT_EQ(close(lock), 0);
    policy_ = {root_ + "/catalog",
               root_ + "/generation.lock",
               getuid(),
               getuid(),
               getgid(),
               getgid(),
               0700,
               0600,
               0600,
               "Fixture",
               "Fixture",
               "Fixture"};
    path_ = policy_.directory + "/catalog.db";
    source_ = root_ + "/action.db";
    Database source(source_, Database::Access::kWriter);
    source.Exec(
        "CREATE TABLE action(action_name TEXT,json_str TEXT);"
        "CREATE TABLE entity(entity_name TEXT,json_str TEXT);"
        "CREATE TABLE action_provider(action_name TEXT,appid TEXT);");
    Json action = {{"name", "Media.Find"},
                   {"description", "Find pictures"},
                   {"inputSchema", {{"type", "object"}}}};
    Statement insert(source.handle(),
                     "INSERT INTO action VALUES('Media.Find',?)");
    insert.Bind(1, action.dump());
    insert.Step();
  }
  void CorruptSource() {
    Database source(source_, Database::Access::kWriter);
    source.Exec("UPDATE action SET json_str='not JSON'");
  }
  std::unique_ptr<AmdCatalogService> Service(
      std::function<void(uint64_t)> changed = {}) {
    return std::make_unique<AmdCatalogService>(policy_, source_,
                                               std::move(changed), &labels_);
  }
  bool WaitFor(const std::function<bool()>& ready,
               std::chrono::seconds budget = std::chrono::seconds(2)) {
    const auto end = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < end) {
      if (ready()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }
  void ExpectExclusiveAvailable() {
    int fd = open(policy_.lock_path.c_str(), O_RDWR | O_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    EXPECT_EQ(fcntl(fd, F_OFD_SETLK, &lock), 0);
    EXPECT_EQ(close(fd), 0);
  }
  ReadLeasePolicy policy_;
  Labels labels_;
  std::string source_;
};

TEST_F(AmdModuleTest, InitializesImportsAndNotifiesOnlyAfterCatalogCommit) {
  uint64_t observed = 0;
  auto service = Service([&](uint64_t revision) {
    Catalog reader(path_, Database::Access::kReadOnly);
    EXPECT_EQ(reader.Search("pictures").size(), 1u);
    EXPECT_EQ(reader.Revision(), revision);
    observed = revision;
  });
  service->Start();
  EXPECT_EQ(observed, 1u);
  EXPECT_FALSE(service->Reconcile());
  service->Stop();
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest,
       ExistingStartSucceedsWhileRealReaderRetainsSharedGeneration) {
  auto first = Service();
  first->Start();
  first->Stop();
  CatalogReadLease reader(policy_, &labels_);
  auto restarted = Service();
  EXPECT_NO_THROW(
      restarted->Start());  // AMD-01: no maintenance EX on schema2 restart.
  reader.Check();
  restarted->Stop();
}

TEST_F(AmdModuleTest, MissingCatalogInitializationRequiresExclusiveGeneration) {
  int fd = open(policy_.lock_path.c_str(), O_RDONLY | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  struct flock lock{};
  lock.l_type = F_RDLCK;
  lock.l_whence = SEEK_SET;
  ASSERT_EQ(fcntl(fd, F_OFD_SETLK, &lock), 0);
  auto service = Service();
  EXPECT_THROW(service->Start(), Error);
  EXPECT_FALSE(std::filesystem::exists(path_));
  EXPECT_EQ(close(fd), 0);
}

TEST_F(AmdModuleTest, MalformedSourceCannotPublishPartialSnapshot) {
  CorruptSource();
  auto service = Service();
  EXPECT_THROW(service->Start(), Error);
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Revision(), 0u);
  EXPECT_TRUE(reader.Search("pictures").empty());
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest, FailedReconcilePreservesPriorRevisionAndSearch) {
  auto service = Service();
  service->Start();
  CorruptSource();
  EXPECT_THROW(service->Reconcile(), Error);
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Revision(), 1u);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
  service->Stop();
}

TEST_F(AmdModuleTest, UnsupportedExistingSchemaIsNotAutomaticallyRepaired) {
  auto first = Service();
  first->Start();
  first->Stop();
  {
    Database catalog(path_, Database::Access::kWriter);
    catalog.Exec("PRAGMA user_version=99");
  }
  auto second = Service();
  EXPECT_THROW(second->Start(), Error);
  Database catalog(path_, Database::Access::kReadOnly);
  Statement version(catalog.handle(), "PRAGMA user_version");
  ASSERT_TRUE(version.Step());
  EXPECT_EQ(version.Integer(0), 99);
}

TEST_F(AmdModuleTest, IntegrityCheckRejectsRealCheckConstraintCorruption) {
  auto service = Service();
  service->Start();
  service->Stop();
  CoordinatedCatalogWriter writer(policy_,
                                  CatalogGenerationLease::Mode::kExisting,
                                  std::chrono::milliseconds(100), &labels_);
  {
    Database fault(path_, Database::Access::kWriter);
    fault.Exec(
        "PRAGMA ignore_check_constraints=ON; UPDATE catalog_state SET singleton=2");
  }
  EXPECT_THROW(writer.CheckIntegrity(), Error);
  writer.Close();
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest, ReconcileRejectsOtherThreadAndStopIsIdempotent) {
  auto service = Service();
  service->Start();
  bool denied = false;
  std::jthread other([&] {
    try {
      service->Reconcile();
    } catch (const Error&) {
      denied = true;
    }
  });
  other.join();
  EXPECT_TRUE(denied);
  service->Stop();
  service->Stop();
  EXPECT_THROW(service->Reconcile(), Error);
  EXPECT_THROW(service->Start(), Error);
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest,
       ConcreteModuleThreadInitializesAndStopJoinsOwnerBeforeLeaseRelease) {
  AmdModuleThread module(policy_, source_, &labels_);
  ASSERT_TRUE(WaitFor([&] { return module.Available(); }));
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
  module.Stop();
  module.Stop();
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest, FailedModuleThreadIsUnavailableAndCanBeJoined) {
  CorruptSource();
  AmdModuleThread module(policy_, source_, &labels_);
  ASSERT_TRUE(WaitFor([&] { return std::filesystem::exists(path_); }));
  EXPECT_FALSE(module.Available());
  module.Stop();
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest, MissingActionSourceAtStartupIsImportedAfterRetry) {
  std::vector<std::string> saved;
  for (const char* suffix : {"", "-wal", "-shm"}) {
    const auto path = source_ + suffix;
    if (std::filesystem::exists(path)) {
      std::filesystem::rename(path, path + ".saved");
      saved.push_back(path);
    }
  }
  AmdModuleThread module(policy_, source_, &labels_);
  ASSERT_TRUE(WaitFor([&] { return module.StartupFailed(); }));
  EXPECT_FALSE(module.Available());
  for (const auto& path : saved) std::filesystem::rename(path + ".saved", path);
  ASSERT_TRUE(
      WaitFor([&] { return module.Available(); }, std::chrono::seconds(7)));
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
  module.Stop();
  ExpectExclusiveAvailable();
}

TEST(AmdModuleConfigTest, DisabledConfigDoesNotInventPolicy) {
  EXPECT_FALSE(ParseAmdModuleConfig({{"enabled", false}}).enabled);
  EXPECT_THROW(
      ParseAmdModuleConfig({{"enabled", false}, {"command", "anything"}}),
      Error);
}

TEST(AmdModuleConfigTest,
     EnabledConfigRequiresAllLabelsAndForbidsPathCommands) {
  const Json enabled = {{"enabled", true},
                        {"directoryLabel", "CapMgr::Directory"},
                        {"fileLabel", "CapMgr::Catalog"},
                        {"lockLabel", "CapMgr::Lock"}};
  EXPECT_TRUE(ParseAmdModuleConfig(enabled).enabled);
  auto bad = enabled;
  bad["directory"] = "/tmp/anything";
  EXPECT_THROW(ParseAmdModuleConfig(bad), Error);
  bad = enabled;
  bad.erase("lockLabel");
  EXPECT_THROW(ParseAmdModuleConfig(bad), Error);
  bad = enabled;
  bad["fileLabel"] = "line\nbreak";
  EXPECT_THROW(ParseAmdModuleConfig(bad), Error);
  bad["enabled"] = 1;
  EXPECT_THROW(ParseAmdModuleConfig(bad), Error);
}

}  // namespace
