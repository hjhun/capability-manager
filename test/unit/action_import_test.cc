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
#include "amd-module/action_import.hh"

using namespace capmgr;
class ImportTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    source_ = root_ + "/source.db";
    Database db(source_, Database::Access::kWriter);
    db.Exec(
        "CREATE TABLE action(action_name TEXT PRIMARY KEY,json_str TEXT);"
        "CREATE TABLE entity(entity_name TEXT PRIMARY KEY,json_str TEXT);"
        "CREATE TABLE action_provider(action_name TEXT,appid TEXT,enabled INTEGER);"
        "PRAGMA user_version=4;");
    Json action = {
        {"name", "Media.Find"},
        {"description", "Find pictures"},
        {"type", "tidl"},
        {"details", {{"appid", "app.default"}, {"pluginPath", "/secret"}}},
        {"autoDispose", true},
        {"inputSchema",
         {{"type", "object"},
          {"properties", {{"query", {{"type", "Media.Query"}}}}}}},
        {"outputSchema", {{"type", "string"}}},
        {"requiresConfirmation", true}};
    Statement a(db.handle(), "INSERT INTO action VALUES(?,?)");
    a.Bind(1, "Media.Find");
    a.Bind(2, action.dump());
    a.Step();
    Json entity = {{"typeName", "Media.Query"},
                   {"base", "Media.Base"},
                   {"dataSchema",
                    {{"type", "object"},
                     {"properties", {{"parent", {{"type", "Media.Query"}}}}}}}};
    Statement e(db.handle(), "INSERT INTO entity VALUES(?,?)");
    e.Bind(1, "Media.Query");
    e.Bind(2, entity.dump());
    e.Step();
    entity = {{"typeName", "Media.Base"}, {"dataSchema", {{"type", "object"}}}};
    Statement base(db.handle(), "INSERT INTO entity VALUES(?,?)");
    base.Bind(1, "Media.Base");
    base.Bind(2, entity.dump());
    base.Step();
    db.Exec(
        "INSERT INTO action_provider VALUES('Media.Find','app.default',1);"
        "INSERT INTO action_provider VALUES('Media.Find','app.disabled',0);");
  }
  std::string source_;
};

TEST_F(ImportTest, SnapshotProjectsEntitiesAndAllRegisteredProviders) {
  auto entries = ReadActionSnapshot(source_);
  ASSERT_EQ(entries.size(), 1u);
  Catalog cap(path_, Database::Access::kWriter);
  ASSERT_TRUE(SynchronizeActions(cap, source_, {}));
  auto detail = cap.Get("action:Media.Find");
  EXPECT_FALSE(detail.contains("type"));
  EXPECT_FALSE(detail.contains("details"));
  EXPECT_FALSE(detail.contains("autoDispose"));
  EXPECT_EQ(detail["inputSchema"]["type"], "object");
  EXPECT_EQ(detail["entities"].size(), 2u);
  EXPECT_EQ(detail["providerAppIds"].size(), 2u);
  EXPECT_EQ(detail["defaultProviderAppId"], "app.default");
}

TEST_F(ImportTest,
       NotificationObservesCommittedCatalogAndFtsAndNoopResyncReplaysRevision) {
  Catalog cap(path_, Database::Access::kWriter);
  int callbacks = 0;
  auto changed = [&](uint64_t revision) {
    Catalog reader(path_, Database::Access::kReadOnly);
    EXPECT_EQ(reader.Revision(), revision);
    EXPECT_EQ(reader.Search("pictures").size(), 1u);
    ++callbacks;
  };
  EXPECT_TRUE(SynchronizeActions(cap, source_, changed));
  EXPECT_FALSE(SynchronizeActions(cap, source_, changed));
  EXPECT_EQ(callbacks, 2);
}

TEST_F(ImportTest,
       MissingEntityFailsWithoutReplacingPreviouslyPublishedGeneration) {
  Catalog cap(path_, Database::Access::kWriter);
  SynchronizeActions(cap, source_, {});
  {
    Database writer(source_, Database::Access::kWriter);
    writer.Exec("DELETE FROM entity WHERE entity_name='Media.Base'");
  }

  EXPECT_THROW(SynchronizeActions(cap, source_, {}), Error);
  EXPECT_EQ(cap.Revision(), 1u);
  EXPECT_EQ(cap.Get("action:Media.Find")["entities"].size(), 2u);
}

TEST_F(ImportTest, BoxedEntityClosureIncludesDerivedAndNestedReferences) {
  std::map<std::string, Json> entities;
  entities["E.Base"] = {{"typeName", "E.Base"},
                        {"dataSchema", {{"type", "object"}}}};
  entities["E.Child"] = {
      {"typeName", "E.Child"},
      {"base", "E.Base"},
      {"dataSchema",
       {{"type", "object"},
        {"properties", {{"nested", {{"type", "E.Other"}}}}}}}};
  entities["E.Other"] = {{"typeName", "E.Other"},
                         {"dataSchema", {{"type", "object"}}}};
  Json action = {
      {"inputSchema",
       {{"type", "object"}, {"properties", {{"box", {{"base", "E.Base"}}}}}}}};
  EXPECT_EQ(EntityClosure(action, entities).size(), 3u);
}

TEST_F(ImportTest, AtomicActionOnlyPublicationRejectsParserKindsAndRollsBack) {
  Catalog cap(path_, Database::Access::kWriter);
  auto entries = ReadActionSnapshot(source_);
  auto wrong = entries;
  wrong[0].kind = Kind::kCli;
  EXPECT_THROW(cap.PublishActions(wrong), Error);
  EXPECT_EQ(cap.Revision(), 0u);
  cap.database().Exec(
      "CREATE TRIGGER fail_import BEFORE INSERT ON capability BEGIN SELECT RAISE(ABORT,'fixture'); END;");
  EXPECT_THROW(SynchronizeActions(cap, source_, {}), Error);
  EXPECT_EQ(cap.Revision(), 0u);
  Statement pending(cap.database().handle(), "SELECT count(*) FROM pending");
  ASSERT_TRUE(pending.Step());
  EXPECT_EQ(pending.Integer(0), 0);
  cap.database().Exec("DROP TRIGGER fail_import");
  EXPECT_TRUE(SynchronizeActions(cap, source_, {}));
}

TEST_F(ImportTest,
       OptionalOutputSchemaIsOmittedButMalformedPresentSchemaFails) {
  {
    Database writer(source_, Database::Access::kWriter);
    writer.Exec(
        "UPDATE action SET json_str=json_remove(json_str,'$.outputSchema')");
  }

  Catalog cap(path_, Database::Access::kWriter);
  ASSERT_TRUE(SynchronizeActions(cap, source_, {}));
  EXPECT_FALSE(cap.Get("action:Media.Find").contains("outputSchema"));
  {
    Database writer(source_, Database::Access::kWriter);
    writer.Exec(
        "UPDATE action SET json_str=json_set(json_str,'$.outputSchema',17)");
  }

  EXPECT_THROW(SynchronizeActions(cap, source_, {}), Error);
  EXPECT_EQ(cap.Revision(), 1u);
}

TEST_F(ImportTest,
       FailedNotificationReplaysCommittedRevisionWithoutRepublishing) {
  Catalog cap(path_, Database::Access::kWriter);
  EXPECT_THROW(SynchronizeActions(
                   cap, source_,
                   [](uint64_t) { throw std::runtime_error("disconnected"); }),
               std::runtime_error);
  EXPECT_EQ(cap.Revision(), 1u);
  uint64_t delivered = 0;
  EXPECT_FALSE(SynchronizeActions(
      cap, source_, [&](uint64_t revision) { delivered = revision; }));
  EXPECT_EQ(delivered, 1u);
  EXPECT_EQ(cap.Revision(), 1u);
}
