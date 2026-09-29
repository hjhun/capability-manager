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

#include <gmock/gmock.h>

static_assert(sizeof(capmgr_kind_t) == 4);
static_assert(sizeof(capmgr_error_e) == 4);
static_assert(CAPMGR_KIND_ACTION == 4);
static_assert(CAPMGR_ERROR_OUT_OF_MEMORY == -10);

using namespace capmgr;
class MockGate : public AccessGate {
 public:
  MOCK_METHOD(std::string, AuthorizeAndGetDatabase, (), (override));
};

TEST_F(CatalogTest, AuthorizationFailureDoesNotOpenDatabase) {
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate, AuthorizeAndGetDatabase())
      .WillOnce(testing::Throw(Error(ErrorCode::kPermission, "denied")));
  capmgr_client_h client = nullptr;
  EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(client, nullptr);
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(CatalogTest, ClientQueriesAreLocalAndResultsOutliveClient) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make()});
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate, AuthorizeAndGetDatabase())
      .Times(1)
      .WillOnce(testing::Return(path_));
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  char* detail = nullptr;
  ASSERT_EQ(capmgr_client_get_capability(client, "skill:search", &detail), 0);
  EXPECT_TRUE(Json::parse(detail)["path"].is_null());
  free(detail);
  int count = 0;
  EXPECT_EQ(capmgr_client_foreach_capability(
                client, CAPMGR_KIND_ALL,
                [](const char*, void* data) {
                  ++*static_cast<int*>(data);
                  return false;
                },
                &count),
            0);
  EXPECT_EQ(count, 1);
  capmgr_search_results_h results = nullptr;
  ASSERT_EQ(capmgr_client_search_capabilities(client, "picture", CAPMGR_KIND_ALL, &results),
            0);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  size_t size = 0;
  EXPECT_EQ(capmgr_search_results_count(results, &size), 0);
  EXPECT_EQ(size, 1u);
  const char* item = nullptr;
  EXPECT_EQ(capmgr_search_results_item(results, 0, &item), 0);
  EXPECT_EQ(Json::parse(item)["id"], "skill:search");
  EXPECT_EQ(capmgr_search_results_item(results, 1, &item),
            CAPMGR_ERROR_NOT_FOUND);
  EXPECT_EQ(item, nullptr);
  capmgr_search_results_free(results);
}

TEST_F(CatalogTest, FailureOutputsAreClearedAndReadOnlyCannotBootstrap) {
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate, AuthorizeAndGetDatabase()).WillOnce(testing::Return(path_));
  capmgr_client_h client = nullptr;
  EXPECT_EQ(CreateClient(gate, &client), CAPMGR_ERROR_DATABASE);
  EXPECT_EQ(client, nullptr);
  EXPECT_FALSE(std::filesystem::exists(path_));
  char dummy = 'x';
  char* detail = &dummy;
  EXPECT_EQ(capmgr_client_get_capability(nullptr, "missing", &detail),
            CAPMGR_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(detail, nullptr);
}

TEST_F(CatalogTest, ForeachDestroyReturnsBusyAndKeepsHandleAlive) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make()});
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate, AuthorizeAndGetDatabase()).WillOnce(testing::Return(path_));
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  EXPECT_EQ(capmgr_client_foreach_capability(
                client, CAPMGR_KIND_ALL,
                [](const char*, void* data) {
                  EXPECT_EQ(
                      capmgr_client_destroy(static_cast<capmgr_client_h>(data)),
                      CAPMGR_ERROR_BUSY);
                  return false;
                },
                client),
            0);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
}

TEST_F(CatalogTest, CorruptDetailReturnsDatabaseAndClearsOutput) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make()});
  writer.database().Exec("UPDATE capability SET detail='not-json'");
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate, AuthorizeAndGetDatabase()).WillOnce(testing::Return(path_));
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  char* detail = nullptr;
  EXPECT_EQ(capmgr_client_get_capability(client, "skill:search", &detail),
            CAPMGR_ERROR_DATABASE);
  EXPECT_EQ(detail, nullptr);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
}
