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

using namespace capmgr;
TEST_F(CatalogTest, RepresentativeEnglishSearchAndUnicodeCorpus) {
  Catalog catalog(path_, Database::Access::kWriter);
  struct Row {
    const char* key;
    const char* name;
    const char* desc;
    const char* keywords;
  };
  const Row corpus[] = {
      {"photo-find", "Find photos",
       "Search local photographs by date and location", "image picture photo"},
      {"photo-edit", "Edit photos", "Crop resize and rotate pictures",
       "image photo"},
      {"photo-share", "Share photos", "Send pictures to a contact",
       "image photo"},
      {"song-find", "Find music", "Search songs albums and artists",
       "audio music"},
      {"audio-play", "Play music", "Play a selected song or playlist",
       "music audio"},
      {"alarm", "Set alarm", "Schedule a wakeup reminder", "clock time"},
      {"weather", "Weather forecast", "Show temperature and rain predictions",
       "weather climate"},
      {"calendar", "Create calendar event",
       "Schedule a meeting with a date and attendees", "calendar appointment"},
      {"notes", "Search notes", "Find text in saved notes", "notes text"},
      {"contacts", "Find contacts", "Look up a person by name or phone number",
       "contacts phone"},
      {"cafe", "Café locator", "Find nearby cafés and coffee shops",
       "café coffee"},
      {"korean", "사진 검색", "로컬 사진을 검색합니다", "사진"},
      {"fallback", "Archive export",
       "Export a backup containing photos music and notes", "backup archive"}};
  std::vector<Entry> entries;
  for (const auto& row : corpus) {
    auto entry = Make(row.key);
    entry.name = row.name;
    entry.desc = row.desc;
    entry.keywords = row.keywords;
    entries.push_back(entry);
  }

  Publish(catalog, "pkg.one", entries);
  for (const auto& [query, key] :
       std::vector<std::pair<std::string, std::string>>{
           {"find photos", "photo-find"},
           {"rotating pictures", "photo-edit"},
           {"selected playlist", "audio-play"},
           {"rain temperature", "weather"},
           {"meeting attendees", "calendar"},
           {"phone person", "contacts"},
           {"CAFÉ", "cafe"},
           {"사진", "korean"},
           {"SET ALARM", "alarm"},
           {"skill:photo-find", "photo-find"}}) {
    auto found = catalog.Search(query);
    ASSERT_FALSE(found.empty()) << query;
    EXPECT_EQ(found.front()["id"], "skill:" + key) << query;
    EXPECT_LE(found.size(), 5u);
  }

  EXPECT_TRUE(catalog.Search("photos weather").empty());
  EXPECT_TRUE(catalog.Search("unrelatedxyz").empty());
  EXPECT_TRUE(catalog.Search("OR NOT * : ^ ( ) \"").empty());
  EXPECT_TRUE(catalog.Search("photos", Kind::kCli).empty());
}

TEST_F(CatalogTest, MissingFtsStorageFailsExplicitlyAsDatabaseError) {
  Catalog catalog(path_, Database::Access::kWriter);
  Publish(catalog, "pkg.one", {Make("example")});
  catalog.database().Exec("DROP TABLE capability_fts");
  try {
    (void)catalog.Search("example words");
    FAIL() << "Expected database error";
  } catch (const Error& error) {
    EXPECT_EQ(error.code(), ErrorCode::kDatabase);
  }
}
