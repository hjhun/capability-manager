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

#include <future>

using namespace capmgr;
TEST_F(CatalogTest, ReadOnlyDoesNotCreateMissingDatabase) {
  EXPECT_THROW(Catalog(path_, Database::Access::kReadOnly), Error);
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(CatalogTest, ReadOnlySeesAtomicCatalogAndFtsUpdate) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make()});
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Search("picture").size(), 1u);
  EXPECT_THROW(reader.database().Exec("DELETE FROM capability"), Error);
  auto entry = Make();
  entry.desc = "Compose messages";
  Publish(writer, "pkg.one", {entry});
  EXPECT_TRUE(reader.Search("picture").empty());
  EXPECT_EQ(reader.Search("messages").size(), 1u);
  EXPECT_EQ(reader.Revision(), 2u);
}

TEST_F(CatalogTest, CollisionRollsBackWholePackageAndFts) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make()});
  EXPECT_THROW(Publish(writer, "pkg.two",
                       {Make("new", "pkg.two"), Make("search", "pkg.two")}),
               Error);
  EXPECT_EQ(writer.Revision(), 1u);
  EXPECT_THROW(writer.Get("skill:new"), Error);
  EXPECT_EQ(writer.Search("pictures").size(), 1u);
}

TEST_F(CatalogTest, AppScopeAllowsDuplicateNames) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "one", {Make("same", "one", Kind::kAppSkill, "app.one")});
  Publish(writer, "two", {Make("same", "two", Kind::kAppSkill, "app.two")});
  EXPECT_EQ(writer.Search("same").size(), 2u);
}

TEST_F(CatalogTest, IdentitySurvivesDisplayNameUpdateAndEncodesDelimiters) {
  EXPECT_NE(CanonicalId(Kind::kAppSkill, "a:b", "c"),
            CanonicalId(Kind::kAppSkill, "b", "c:a"));
  EXPECT_NE(CanonicalId(Kind::kSkill, "%3A"), CanonicalId(Kind::kSkill, ":"));
  Catalog writer(path_, Database::Access::kWriter);
  auto e = Make();
  Publish(writer, e.owner, {e});
  e.name = "New title";
  Publish(writer, e.owner, {e});
  EXPECT_EQ(writer.Get(e.id)["name"], "New title");
}

TEST_F(CatalogTest, PendingNeverVisibleAndFailureRetainsPreviousGeneration) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make()});
  writer.Stage("update", "pkg.one", {Make("replacement")});
  EXPECT_EQ(writer.Search("search").size(), 1u);
  EXPECT_TRUE(writer.Search("replacement").empty());
  writer.Finalize("update", false);
  EXPECT_EQ(writer.Revision(), 1u);
  writer.Stage("remove", "pkg.one", {});
  EXPECT_EQ(writer.Search("search").size(), 1u);
  writer.Finalize("remove", true);
  EXPECT_TRUE(writer.Search("search").empty());
}

TEST_F(CatalogTest, PendingSurvivesReopenAndReplayMustMatch) {
  {
    Catalog db(path_, Database::Access::kWriter);
    db.Stage("install", "pkg.one", {Make()});
  }

  Catalog reopened(path_, Database::Access::kWriter);
  EXPECT_TRUE(reopened.Search("search").empty());
  EXPECT_NO_THROW(reopened.Stage("install", "pkg.one", {Make()}));
  EXPECT_THROW(reopened.Stage("install", "pkg.one", {Make("changed")}), Error);
  reopened.Finalize("install", true);
  EXPECT_EQ(reopened.Search("search").size(), 1u);
}

TEST_F(CatalogTest, PendingReservesOwnershipAcrossWriters) {
  Catalog first(path_, Database::Access::kWriter),
      second(path_, Database::Access::kWriter);
  first.Stage("pending", "pkg.one", {Make()});
  EXPECT_THROW(Publish(second, "pkg.two", {Make("search", "pkg.two")}), Error);
  first.Finalize("pending", false);
  EXPECT_NO_THROW(Publish(second, "pkg.two", {Make("search", "pkg.two")}));
}

TEST_F(CatalogTest, SearchCapsFiveAndRequiresAllTokens) {
  Catalog db(path_, Database::Access::kWriter);
  std::vector<Entry> entries;
  for (int i = 0; i < 8; ++i)
    entries.push_back(Make("photo" + std::to_string(i)));
  Publish(db, "pkg.one", entries);
  EXPECT_EQ(db.Search("pictures").size(), 5u);
  EXPECT_EQ(db.Search("photo7")[0]["id"], "skill:photo7");
  EXPECT_TRUE(db.Search("pictures unrelated").empty());
  EXPECT_TRUE(db.Search("pictures", Kind::kCli).empty());
  EXPECT_TRUE(db.Search("").empty());
  EXPECT_TRUE(db.Search("\" OR *").empty());
}

TEST_F(CatalogTest, ProjectionPreservesNestedTypeAndHidesExecution) {
  Catalog db(path_, Database::Access::kWriter);
  auto e = Make("act", "pkg.one", Kind::kAction);
  e.detail = {{"type", "plugin"},
              {"details", {{"pluginPath", "/private"}}},
              {"inputSchema", {{"type", "object"}}},
              {"providerAppIds", {"app.one"}},
              {"defaultProviderAppId", nullptr},
              {"enabled", true}};
  Publish(db, "pkg.one", {e});
  auto detail = db.Get(e.id);
  EXPECT_FALSE(detail.contains("type"));
  EXPECT_FALSE(detail.contains("details"));
  EXPECT_FALSE(detail.contains("enabled"));
  EXPECT_EQ(detail["inputSchema"]["type"], "object");
  EXPECT_EQ(detail["providerAppIds"][0], "app.one");
}

TEST_F(CatalogTest, SkillNeverExposesSourceBeforeExplicitMount) {
  Catalog db(path_, Database::Access::kWriter);
  auto e = Make();
  e.resource = "/private/resource";
  Publish(db, e.owner, {e});
  auto detail = db.Get(e.id);
  EXPECT_TRUE(detail["path"].is_null());
  EXPECT_EQ(detail["available"], false);
  EXPECT_EQ(detail.dump().find("/private/resource"), std::string::npos);
}

TEST_F(CatalogTest, UnsupportedSchemaIsNotMigratedByReaderOrWriter) {
  {
    Catalog db(path_, Database::Access::kWriter);
    db.database().Exec("PRAGMA user_version=42");
  }

  EXPECT_THROW(Catalog(path_, Database::Access::kReadOnly), Error);
  EXPECT_THROW(Catalog(path_, Database::Access::kWriter), Error);
}

TEST_F(CatalogTest, CompetingWriterBusyIsBoundedAndRollbackReleasesLock) {
  Catalog first(path_, Database::Access::kWriter),
      second(path_, Database::Access::kWriter);
  {
    Transaction tx(first.database());
    EXPECT_THROW(Publish(second, "pkg.two", {Make("other", "pkg.two")}), Error);
  }

  EXPECT_NO_THROW(Publish(second, "pkg.two", {Make("other", "pkg.two")}));
}

TEST_F(CatalogTest, SimultaneousConnectionsPublishWithoutLostUpdates) {
  {
    Catalog init(path_, Database::Access::kWriter);
  }

  auto write = [&](std::string owner) {
    Catalog db(path_, Database::Access::kWriter);
    Publish(db, owner, {Make(owner, owner)});
  };
  auto a = std::async(std::launch::async, write, "one");
  auto b = std::async(std::launch::async, write, "two");
  EXPECT_NO_THROW(a.get());
  EXPECT_NO_THROW(b.get());
  Catalog read(path_, Database::Access::kReadOnly);
  EXPECT_EQ(read.Revision(), 2u);
}

TEST_F(CatalogTest, FinalizationReplayIsIdempotentButCannotChangeOutcome) {
  Catalog db(path_, Database::Access::kWriter);
  db.Stage("op", "pkg.one", {Make()});
  db.Finalize("op", true);
  EXPECT_NO_THROW(db.Finalize("op", true));
  EXPECT_EQ(db.Revision(), 1u);
  EXPECT_THROW(db.Finalize("op", false), Error);
  EXPECT_THROW(db.Stage("op", "pkg.one", {Make()}), Error);
}

TEST_F(CatalogTest, IdentityRejectsInvalidUtf8AndEmbeddedNul) {
  EXPECT_THROW(CanonicalId(Kind::kSkill, std::string("\xff")), Error);
  EXPECT_THROW(CanonicalId(Kind::kAppSkill, "key", std::string("a\0b", 3)),
               Error);
}

TEST_F(CatalogTest,
       VersionOneMigratesTransactionallyAndPreservesPublishedRows) {
  {
    Catalog db(path_, Database::Access::kWriter);
    Publish(db, "pkg.one", {Make()});
    db.database().Exec("DROP TABLE completed; PRAGMA user_version=1;");
  }

  EXPECT_THROW(Catalog(path_, Database::Access::kReadOnly), Error);
  Catalog writer(path_, Database::Access::kWriter);
  EXPECT_EQ(writer.Get("skill:search")["name"], "search");
  EXPECT_EQ(writer.Revision(), 1u);
  writer.Stage("migrated", "pkg.one", {});
  writer.Finalize("migrated", false);
  Catalog reader(path_, Database::Access::kReadOnly);
  EXPECT_EQ(reader.Search("pictures").size(), 1u);
}

TEST_F(CatalogTest, CorruptRevisionAndPendingJsonAreDatabaseErrors) {
  Catalog db(path_, Database::Access::kWriter);
  db.database().Exec(
      "PRAGMA ignore_check_constraints=ON; UPDATE catalog_state SET revision='bad';");
  try {
    db.Revision();
    FAIL() << "malformed revision accepted";
  } catch (const Error& e) {
    EXPECT_EQ(e.code(), ErrorCode::kDatabase);
  }

  db.database().Exec(
      "UPDATE catalog_state SET revision=0; INSERT INTO pending VALUES('bad','pkg','{');");
  try {
    db.Finalize("bad", true);
    FAIL() << "malformed pending accepted";
  } catch (const Error& e) {
    EXPECT_EQ(e.code(), ErrorCode::kDatabase);
  }
}

TEST_F(CatalogTest, RevisionOverflowRollsBackCatalogAndFts) {
  Catalog db(path_, Database::Access::kWriter);
  Publish(db, "pkg.one", {Make()});
  db.database().Exec("UPDATE catalog_state SET revision=9223372036854775807");
  db.Stage("overflow", "pkg.one", {});
  EXPECT_THROW(db.Finalize("overflow", true), Error);
  EXPECT_EQ(db.Get("skill:search")["name"], "search");
  EXPECT_EQ(db.Search("pictures").size(), 1u);
}
