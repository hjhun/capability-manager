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
#include "pkgmgr-plugin/parser.hh"

#include <fstream>

#include <sys/stat.h>

using namespace capmgr;
class ParserTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    std::filesystem::create_directories(root_ + "/res/skills/example");
    std::filesystem::create_directories(root_ + "/bin");
    std::ofstream(root_ + "/res/skills/example/SKILL.md") << "# Example\n";
    Json skill = {{"version", 1},
                  {"key", "example"},
                  {"name", "Example"},
                  {"desc", "Find pictures"},
                  {"resource", "res/skills/example"}};
    Write("skill.json", skill);
    std::ofstream(root_ + "/bin/cli") << "fixture";
    chmod((root_ + "/bin/cli").c_str(), 0755);
    Json cli = {{"version", 1},
                {"key", "example"},
                {"name", "CLI"},
                {"desc", "Find pictures"},
                {"executable", "bin/cli"},
                {"inputSchema", {{"type", "object"}}},
                {"outputSchema", {{"type", "object"}}}};
    Write("cli.json", cli);
  }

  void Write(const std::string& name, const Json& value) {
    std::ofstream(root_ + "/" + name) << value.dump();
  }
};

TEST_F(ParserTest, RepeatedAndSemicolonMetadataAreCollectedOnce) {
  auto entries = ParsePackage(root_, "pkg",
                              {{Kind::kSkill, " skill.json ; skill.json ", ""},
                               {Kind::kSkill, "skill.json", ""},
                               {Kind::kCli, "cli.json", ""}});
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].id, "skill:example");
  EXPECT_EQ(entries[1].id, "cli:example");
}

TEST_F(ParserTest, AppScopeComesFromCallbackAndAllowsSameNames) {
  auto entries = ParsePackage(root_, "pkg",
                              {{Kind::kAppSkill, "skill.json", "app.one"},
                               {Kind::kAppSkill, "skill.json", "app.two"}});
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_NE(entries[0].id, entries[1].id);
}

TEST_F(ParserTest, EmptyTraversalAbsoluteAndSymlinkPathsAreRejected) {
  std::filesystem::create_symlink(root_ + "/skill.json", root_ + "/link.json");
  for (const char* path : {"", "skill.json;", ";skill.json", "../skill.json",
                           "/skill.json", "link.json"})
    EXPECT_THROW(ParsePackage(root_, "pkg", {{Kind::kSkill, path, ""}}), Error)
        << path;
}

TEST_F(ParserTest, LastBadDescriptorLeavesNoPartialPendingPublication) {
  Catalog catalog(path_, Database::Access::kWriter);
  EXPECT_THROW(StagePackage(catalog, "op", root_, "pkg",
                            {{Kind::kSkill, "skill.json;missing.json", ""}},
                            FinalizationAuthority::kOfflineHarness),
               Error);
  EXPECT_EQ(catalog.Revision(), 0u);
  EXPECT_TRUE(catalog.Search("picture").empty());
  EXPECT_THROW(catalog.Finalize("op", true), Error);
}

TEST_F(ParserTest, OfflineSuccessAndFailureNeedNoService) {
  Catalog catalog(path_, Database::Access::kWriter);
  StagePackage(catalog, "op", root_, "pkg", {{Kind::kSkill, "skill.json", ""}},
               FinalizationAuthority::kOfflineHarness);
  EXPECT_TRUE(catalog.Search("picture").empty());
  catalog.Finalize("op", false);
  EXPECT_TRUE(catalog.Search("picture").empty());
  StagePackage(catalog, "success", root_, "pkg",
               {{Kind::kSkill, "skill.json", ""}},
               FinalizationAuthority::kOfflineHarness);
  catalog.Finalize("success", true);
  EXPECT_EQ(catalog.Search("picture").size(), 1u);
}

TEST_F(ParserTest, ActionMetadataAndNonExecutableCliAreRejected) {
  EXPECT_THROW(ParsePackage(root_, "pkg", {{Kind::kAction, "skill.json", ""}}),
               Error);
  chmod((root_ + "/bin/cli").c_str(), 0644);
  EXPECT_THROW(ParsePackage(root_, "pkg", {{Kind::kCli, "cli.json", ""}}),
               Error);
}

TEST_F(ParserTest, ProductionWithoutFinalizerFailsBeforeStagingEvenUninstall) {
  Catalog catalog(path_, Database::Access::kWriter);
  EXPECT_THROW(StagePackage(catalog, "install", root_, "pkg",
                            {{Kind::kSkill, "skill.json", ""}}),
               Error);
  EXPECT_THROW(StagePackage(catalog, "uninstall", root_, "pkg", {}), Error);
  Statement pending(catalog.database().handle(),
                    "SELECT count(*) FROM pending");
  ASSERT_TRUE(pending.Step());
  EXPECT_EQ(pending.Integer(0), 0);
  EXPECT_EQ(catalog.Revision(), 0u);
}
