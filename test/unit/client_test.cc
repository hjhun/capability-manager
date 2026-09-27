// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "api/client.hh"
#include <gmock/gmock.h>
using namespace capmgr;
class MockGate : public AccessGate {
 public: MOCK_METHOD(std::string,AuthorizeAndGetDatabase,(),(override));
};
TEST_F(CatalogTest, AuthorizationFailureDoesNotOpenDatabase) {
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate,AuthorizeAndGetDatabase()).WillOnce(testing::Throw(
    Error(ErrorCode::kPermission,"denied")));
  capmgr_client_h client=nullptr;
  EXPECT_EQ(CreateClient(gate,&client),CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(client,nullptr); EXPECT_FALSE(std::filesystem::exists(path_));
}
TEST_F(CatalogTest, ClientQueriesAreLocalAndResultsOutliveClient) {
  Catalog writer(path_,Database::Access::kWriter);
  Publish(writer,"pkg.one",{Make()});
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate,AuthorizeAndGetDatabase()).Times(1).WillOnce(testing::Return(path_));
  capmgr_client_h client=nullptr; ASSERT_EQ(CreateClient(gate,&client),0);
  char* detail=nullptr; ASSERT_EQ(capmgr_client_get_capability(client,"skill:search",&detail),0);
  EXPECT_TRUE(Json::parse(detail)["path"].is_null()); free(detail);
  int count=0;
  EXPECT_EQ(capmgr_client_foreach_capability(client,0,[](const char*,void* data) {
    ++*static_cast<int*>(data); return false;
  },&count),0); EXPECT_EQ(count,1);
  capmgr_search_results_h results=nullptr;
  ASSERT_EQ(capmgr_client_search_capabilities(client,"picture",0,&results),0);
  EXPECT_EQ(capmgr_client_destroy(client),0);
  size_t size=0; EXPECT_EQ(capmgr_search_results_count(results,&size),0); EXPECT_EQ(size,1u);
  const char* item=nullptr; EXPECT_EQ(capmgr_search_results_item(results,0,&item),0);
  EXPECT_EQ(Json::parse(item)["id"],"skill:search");
  EXPECT_EQ(capmgr_search_results_item(results,1,&item),CAPMGR_ERROR_NOT_FOUND);
  EXPECT_EQ(item,nullptr); capmgr_search_results_free(results);
}
TEST_F(CatalogTest, FailureOutputsAreClearedAndReadOnlyCannotBootstrap) {
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate,AuthorizeAndGetDatabase()).WillOnce(testing::Return(path_));
  capmgr_client_h client=nullptr;
  EXPECT_EQ(CreateClient(gate,&client),CAPMGR_ERROR_DATABASE);
  EXPECT_EQ(client,nullptr); EXPECT_FALSE(std::filesystem::exists(path_));
  char dummy='x'; char* detail=&dummy;
  EXPECT_EQ(capmgr_client_get_capability(nullptr,"missing",&detail),CAPMGR_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(detail,nullptr);
}
TEST_F(CatalogTest, ForeachDestroyReturnsBusyAndKeepsHandleAlive) {
  Catalog writer(path_,Database::Access::kWriter); Publish(writer,"pkg.one",{Make()});
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate,AuthorizeAndGetDatabase()).WillOnce(testing::Return(path_));
  capmgr_client_h client=nullptr; ASSERT_EQ(CreateClient(gate,&client),0);
  EXPECT_EQ(capmgr_client_foreach_capability(client,0,[](const char*,void* data) {
    EXPECT_EQ(capmgr_client_destroy(static_cast<capmgr_client_h>(data)),CAPMGR_ERROR_BUSY);
    return false;
  },client),0);
  EXPECT_EQ(capmgr_client_destroy(client),0);
}
TEST_F(CatalogTest, CorruptDetailReturnsDatabaseAndClearsOutput) {
  Catalog writer(path_,Database::Access::kWriter);Publish(writer,"pkg.one",{Make()});
  writer.database().Exec("UPDATE capability SET detail='not-json'");
  testing::StrictMock<MockGate> gate;
  EXPECT_CALL(gate,AuthorizeAndGetDatabase()).WillOnce(testing::Return(path_));
  capmgr_client_h client=nullptr;ASSERT_EQ(CreateClient(gate,&client),0);
  char* detail=nullptr;
  EXPECT_EQ(capmgr_client_get_capability(client,"skill:search",&detail),CAPMGR_ERROR_DATABASE);
  EXPECT_EQ(detail,nullptr);EXPECT_EQ(capmgr_client_destroy(client),0);
}
