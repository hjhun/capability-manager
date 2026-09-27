// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <fcntl.h>
#include <future>
#include <thread>
#include "launcher/runner.hh"
#define CAPMGR_CLI_FIXTURE                                         \
  (std::filesystem::read_symlink("/proc/self/exe").parent_path() / \
   "capmgr-cli-fixture")                                           \
      .string()
using namespace capmgr;
namespace {
Request MakeRequest(std::string mode = "normal") {
  return ParseRequest(
      Json{{"jsonrpc", "2.0"},
           {"id", "request-1"},
           {"method", "tools/call"},
           {"params", {{"name", "cli:test"}, {"arguments", {{"mode", mode}}}}}}
          .dump());
}
RunResult RunFixture(std::string mode) {
  std::atomic<bool> cancel = false;
  return RunCli(CAPMGR_CLI_FIXTURE, MakeRequest(mode), cancel,
                {std::chrono::seconds(2), 1024 * 1024});
}
}
TEST(Rpc, StrictRequestShapeAndIdBoundaries) {
  auto request = MakeRequest();
  auto j = Json::parse(request.original);
  for (auto bad : std::vector<Json>{nullptr, 1.5, Json::array(), Json::object(),
                                    "", UINT64_MAX}) {
    j["id"] = bad;
    EXPECT_THROW(ParseRequest(j.dump()), Error);
  }
  j["id"] = INT64_MIN;
  EXPECT_EQ(ParseRequest(j.dump()).id, INT64_MIN);
  j["id"] = INT64_MAX;
  EXPECT_EQ(ParseRequest(j.dump()).id, INT64_MAX);
  EXPECT_THROW(ParseRequest("[]"), Error);
  EXPECT_THROW(ParseRequest(std::string(65537, 'x')), Error);
}
TEST(Cli, FullRequestIsOneLiteralArgument) {
  auto request = MakeRequest();
  auto j = Json::parse(request.original);
  j["params"]["arguments"]["literal"] =
      "x ; $(touch /tmp/never-capmgr) `echo nope` \" quoted\n한글";
  request = ParseRequest(j.dump());
  std::atomic<bool> cancel = false;
  auto result = RunCli(CAPMGR_CLI_FIXTURE, request, cancel);
  ASSERT_TRUE(result.native);
  EXPECT_EQ(Json::parse(result.response)["result"]["argv"], request.original);
}
TEST(Cli, ValidStdoutStderrPartialAndDuplicateReplies) {
  for (const char* mode : {"normal", "stderr", "partial", "dual", "nonzero"}) {
    auto result = RunFixture(mode);
    EXPECT_TRUE(result.native) << mode << result.response;
  }
}
TEST(Cli, NativeErrorsAndResultIsErrorRemainDistinct) {
  auto native = RunFixture("error");
  ASSERT_TRUE(native.native);
  EXPECT_EQ(native.exit_code, 7);
  auto j = Json::parse(native.response);
  EXPECT_EQ(j["error"]["code"], -77);
  EXPECT_EQ(j["error"]["data"]["unchanged"], true);
  auto tool = RunFixture("is-error");
  ASSERT_TRUE(tool.native);
  j = Json::parse(tool.response);
  EXPECT_EQ(j["result"]["isError"], true);
  EXPECT_FALSE(j.contains("error"));
}
TEST(Cli, RejectsMalformedMultipleMismatchedAndConflictingReplies) {
  for (const char* mode :
       {"invalid", "multiple", "wrong-id", "conflict", "large"}) {
    auto result = RunFixture(mode);
    EXPECT_FALSE(result.native) << mode;
    EXPECT_TRUE(Json::parse(result.response).contains("error"));
  }
}
TEST(Cli, TimeoutAndCancellationAreBounded) {
  std::atomic<bool> cancel = false;
  auto timeout = RunCli(CAPMGR_CLI_FIXTURE, MakeRequest("hang"), cancel,
                        {std::chrono::milliseconds(40), 1024 * 1024});
  EXPECT_EQ(Json::parse(timeout.response)["error"]["data"]["cause"], "timeout");
  auto future = std::async(std::launch::async, [&] {
    return RunCli(CAPMGR_CLI_FIXTURE, MakeRequest("hang"), cancel);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  cancel = true;
  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(Json::parse(future.get().response)["error"]["data"]["cause"],
            "cancelled");
}
TEST(Cli, SpawnErrorDoesNotProduceNativeReply) {
  std::atomic<bool> cancel = false;
  auto result = RunCli("/does/not/exist", MakeRequest(), cancel);
  EXPECT_FALSE(result.native);
  EXPECT_EQ(Json::parse(result.response)["error"]["data"]["cause"],
            "exec failed");
}
TEST(Cli, ForkedChildHoldingPipeIsKilledAndReapedBySupervisor) {
  ASSERT_EQ(prctl(PR_SET_CHILD_SUBREAPER, 1), 0);
  char file[] = "/tmp/capmgr-child-XXXXXX";
  int fd = mkstemp(file);
  ASSERT_GE(fd, 0);
  close(fd);
  auto j = Json::parse(MakeRequest("fork").original);
  j["params"]["arguments"]["pidFile"] = file;
  std::atomic<bool> cancel = false;
  auto result = RunCli(CAPMGR_CLI_FIXTURE, ParseRequest(j.dump()), cancel,
                       {std::chrono::milliseconds(100), 1024 * 1024});
  pid_t child = 0;
  std::ifstream(file) >> child;
  unlink(file);
  ASSERT_GT(child, 0);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFSIGNALED(status));
  EXPECT_EQ(WTERMSIG(status), SIGKILL);
  EXPECT_FALSE(result.native);
  prctl(PR_SET_CHILD_SUBREAPER, 0);
}

TEST(Cli, UnrelatedParentFileDescriptorsAreNotInherited) {
  int fd = open("/dev/null", O_RDONLY);
  ASSERT_GE(fd, 0);
  auto json = Json::parse(MakeRequest("fd").original);
  json["params"]["arguments"]["fd"] = fd;
  std::atomic<bool> cancel = false;
  auto result = RunCli(CAPMGR_CLI_FIXTURE, ParseRequest(json.dump()), cancel);
  close(fd);
  ASSERT_TRUE(result.native);
  EXPECT_EQ(Json::parse(result.response)["result"]["fdOpen"], false);
}
