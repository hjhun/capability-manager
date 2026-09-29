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

#include "amd-module/catalog_service.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <thread>
#include <map>

#include "amd-module/module_config.hh"
#include "amd-module/module_task.hh"
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

// Scoped tizen-core adapter mock: explicit callback dispatch, no platform task.
class FakeCore : public CoreOperations {
 public:
  struct Source {
    void* core;
    Callback callback;
    void* data;
  };
  int task_token = 0, owner_token = 0, main_token = 0;
  std::string fail;
  std::string throwing;
  std::vector<std::string> events;
  std::map<void*, std::unique_ptr<Source>> sources;
  unsigned interval = 0;
  bool immediate = false;
  std::function<void()> before_quit;
  void Init() override { events.push_back("init"); }
  void Shutdown() override { events.push_back("shutdown"); }
  int Result(const char* name) {
    events.push_back(name);
    if (throwing == name) throw std::bad_alloc();
    return fail == name ? -1 : 0;
  }
  int Create(void** task) override {
    int result = Result("create");
    if (!result) *task = &task_token;
    return result;
  }
  int GetCore(void*, void** core) override {
    int result = Result("get");
    if (!result) *core = &owner_token;
    return result;
  }
  int Run(void*) override { return Result("run"); }
  int Main(void** core) override {
    int result = Result("main");
    if (!result) *core = &main_token;
    return result;
  }
  int Add(void* core, Callback callback, void* data, void** source,
          const char* name) {
    int result = Result(name);
    if (result) return result;
    auto value = std::make_unique<Source>(Source{core, callback, data});
    *source = value.get();
    sources.emplace(*source, std::move(value));
    if (immediate) Dispatch(*source);
    return 0;
  }
  int Idle(void* core, Callback callback, void* data, void** source) override {
    return Add(core, callback, data, source,
               core == &main_token ? "main-idle" : "owner-idle");
  }
  int Timer(void* core, unsigned milliseconds, Callback callback, void* data,
            void** source) override {
    interval = milliseconds;
    return Add(core, callback, data, source, "timer");
  }
  int Remove(void* core, void* source) override {
    int result = Result("remove");
    if (result) return result;
    auto it = sources.find(source);
    EXPECT_TRUE(it != sources.end());
    if (it == sources.end()) return -1;
    EXPECT_EQ(it->second->core, core);
    sources.erase(it);
    return 0;
  }
  int Quit(void*) override {
    if (before_quit) before_quit();
    return Result("quit");
  }
  int Destroy(void*) override { return Result("destroy"); }
  void Dispatch(void* source) {
    auto* value = sources.at(source).get();
    if (!value->callback(value->data)) sources.erase(source);
  }
  void DispatchNext() {
    ASSERT_FALSE(sources.empty());
    Dispatch(sources.begin()->first);
  }
};

TEST_F(AmdModuleTest, OwnerIdleImportsAndClosesBeforeQuitDestroyShutdown) {
  FakeCore core;
  AmdModuleTask module(core, policy_, source_, &labels_);
  EXPECT_FALSE(module.Available());
  ASSERT_EQ(core.sources.size(), 1u);
  core.DispatchNext();  // AMD main idle only hands off; it does not open SQLite.
  EXPECT_FALSE(std::filesystem::exists(path_));
  core.DispatchNext();  // Dedicated owner startup idle.
  ASSERT_TRUE(module.Available());
  EXPECT_EQ(core.interval, 5000u);
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
  core.before_quit = [&] {
    EXPECT_FALSE(module.Available());
    EXPECT_TRUE(core.sources.empty());
    ExpectExclusiveAvailable();  // Actual physical close/release before quit.
  };
  core.immediate = true;  // Dispatch the stop idle on this SAME mock owner.
  module.Stop();
  module.Stop();
  EXPECT_EQ(std::vector<std::string>(core.events.end() - 3, core.events.end()),
            (std::vector<std::string>{"quit", "destroy", "shutdown"}));
}

TEST_F(AmdModuleTest, FailedStartupRetainsTimerUntilKnownOwnerShutdown) {
  CorruptSource();
  FakeCore core;
  AmdModuleTask module(core, policy_, source_, &labels_);
  core.DispatchNext();
  core.DispatchNext();
  EXPECT_TRUE(module.StartupFailed());
  EXPECT_FALSE(module.Available());
  EXPECT_EQ(core.sources.size(), 1u);
  core.immediate = true;
  module.Stop();
  EXPECT_TRUE(core.sources.empty());
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest, MissingActionSourceAtStartupIsImportedOnRetryTimer) {
  std::vector<std::string> saved;
  for (const char* suffix : {"", "-wal", "-shm"}) {
    const auto path = source_ + suffix;
    if (std::filesystem::exists(path)) {
      std::filesystem::rename(path, path + ".saved");
      saved.push_back(path);
    }
  }
  FakeCore core;
  AmdModuleTask module(core, policy_, source_, &labels_);
  core.DispatchNext();
  core.DispatchNext();
  ASSERT_TRUE(module.StartupFailed());
  EXPECT_FALSE(module.Available());
  EXPECT_EQ(core.interval, 5000u);  // Milliseconds, not seconds.
  for (const auto& path : saved) std::filesystem::rename(path + ".saved", path);
  core.DispatchNext();  // Retry callback after source readiness, no real timer.
  ASSERT_TRUE(module.Available());
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
  core.immediate = true;
  module.Stop();
  ExpectExclusiveAvailable();
}

TEST_F(AmdModuleTest, StopCancelsPendingMainAndOwnerStartupSources) {
  for (bool handoff : {false, true}) {
    FakeCore core;
    AmdModuleTask module(core, policy_, source_, &labels_);
    if (handoff) core.DispatchNext();
    core.immediate = true;
    module.Stop();
    EXPECT_TRUE(core.sources.empty());
    EXPECT_FALSE(std::filesystem::exists(path_));
  }
}

TEST_F(AmdModuleTest, SetupFailuresBalanceOnlyOwnCoreAcquisition) {
  for (const char* operation : {"create", "get", "run", "main", "main-idle"}) {
    FakeCore core;
    core.fail = operation;
    EXPECT_THROW(AmdModuleTask(core, policy_, source_, &labels_), Error);
    EXPECT_EQ(core.events.front(), "init");
    EXPECT_EQ(core.events.back(), "shutdown");
    EXPECT_TRUE(core.sources.empty());
    EXPECT_FALSE(std::filesystem::exists(path_));
  }
}

TEST_F(AmdModuleTest, HandoffAndTimerFailureAreUnavailableWithoutFallback) {
  for (const char* operation : {"owner-idle", "timer"}) {
    FakeCore core;
    AmdModuleTask module(core, policy_, source_, &labels_);
    core.fail = operation;
    core.DispatchNext();
    if (core.sources.size()) core.DispatchNext();
    EXPECT_TRUE(module.StartupFailed());
    EXPECT_FALSE(module.Available());
    EXPECT_FALSE(std::filesystem::exists(path_));
    core.fail.clear();
    core.immediate = true;
    module.Stop();
    EXPECT_TRUE(core.sources.empty());
  }
}

TEST_F(AmdModuleTest, ThrowingIdleAndTimerAreContainedAtNativeCallbacks) {
  for (const char* operation : {"owner-idle", "timer"}) {
    FakeCore core;
    AmdModuleTask module(core, policy_, source_, &labels_);
    core.throwing = operation;
    EXPECT_NO_THROW(core.DispatchNext());
    if (!core.sources.empty()) {
      EXPECT_NO_THROW(core.DispatchNext());
    }
    EXPECT_TRUE(module.StartupFailed());
    EXPECT_FALSE(module.Available());
    EXPECT_FALSE(std::filesystem::exists(path_));
    core.throwing.clear();
    core.immediate = true;
    module.Stop();
    EXPECT_TRUE(core.sources.empty());
  }
}

TEST_F(AmdModuleTest, FailedStopPostRefusesUnsafeTaskUnload) {
  EXPECT_DEATH(
      {
        FakeCore core;
        AmdModuleTask module(core, policy_, source_, &labels_);
        core.fail = "owner-idle";
        module.Stop();
      },
      "cannot safely unload owned task");
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
