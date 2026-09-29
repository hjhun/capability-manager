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

#include <gtest/gtest.h>

#include "launcher/worker_result.hh"

#include <cstring>

#include <signal.h>

using namespace capmgr;

namespace {

std::string RequestText(std::string id = "1") {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
         ",\"method\":\"tools/call\",\"params\":{\"name\":\"cli:fixture\",\"arguments\":{}}}";
}

WorkerEvent Event(WorkerReplyKind kind,
                  WorkerFailure failure = WorkerFailure::None, int code = -1,
                  int signal = 0, int error = 0) {
  return {kind, 42, failure, code, signal, error, {}, 0};
}

void Feed(WorkerResult& result, const std::string& text,
          WorkerReplyKind kind = WorkerReplyKind::Stdout) {
  for (size_t at = 0; at < text.size();) {
    auto event = Event(kind);
    event.size = std::min<size_t>(23, text.size() - at);
    std::memcpy(event.bytes.data(), text.data() + at, event.size);
    at += event.size;
    EXPECT_FALSE(result.Accept(event));
  }
}

RunResult Finish(WorkerResult& result, int code = 0) {
  auto value = result.Accept(
      Event(WorkerReplyKind::Complete, WorkerFailure::None, code));
  EXPECT_TRUE(value);
  return value.value();
}

std::string Cause(const RunResult& result) {
  return Json::parse(result.response)["error"]["data"]["cause"]
      .get<std::string>();
}
}  // namespace

TEST(WorkerResult, ConstructBeforeAdmissionBindBeforeEventsAndCorrelateTokens) {
  auto text = RequestText();
  WorkerResult result(7, text);
  EXPECT_EQ(result.Original(), text);
  EXPECT_EQ(result.ClientToken(), 7u);
  EXPECT_FALSE(result.Bind(0));
  EXPECT_THROW(result.Accept(Event(WorkerReplyKind::Accepted)), Error);
  ASSERT_TRUE(result.Bind(42));
  EXPECT_FALSE(result.Bind(43));
  auto foreign = Event(WorkerReplyKind::Complete);
  foreign.token = 43;
  EXPECT_THROW(result.Accept(foreign), Error);
  EXPECT_FALSE(result.Complete());
  EXPECT_FALSE(result.Accept(Event(WorkerReplyKind::Accepted)));
  Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})");
  EXPECT_FALSE(result.Complete());
  EXPECT_TRUE(Finish(result).native);
  EXPECT_THROW(result.Accept(Event(WorkerReplyKind::Complete)), Error);
  EXPECT_THROW(result.Accept(Event(WorkerReplyKind::Stdout)), Error);
}

TEST(WorkerResult, RequestDuplicateKeysDepthAndIdentityAreRejectedBeforeStart) {
  EXPECT_THROW(WorkerResult(0, RequestText()), Error);
  EXPECT_THROW(
      WorkerResult(
          7,
          R"({"jsonrpc":"2.0","id":1,"id":2,"method":"tools/call","params":{"name":"cli:x","arguments":{}}})"),
      Error);
  auto text = RequestText();
  text.replace(text.find("cli:fixture"), 11, "action:fixture");
  EXPECT_THROW(WorkerResult(7, text), Error);
  text = RequestText();
  text.replace(
      text.rfind("{}"), 2,
      "{\"x\":" + std::string(130, '[') + "0" + std::string(130, ']') + "}");
  EXPECT_THROW(WorkerResult(7, text), Error);
  text = RequestText();
  text.replace(
      text.rfind("{}"), 2,
      "{\"x\":" + std::string(65, '[') + "0" + std::string(65, ']') + "}");
  EXPECT_THROW(WorkerResult(7, text), Error);
  text = RequestText();
  text.replace(text.find("cli:fixture"), 11, "cli:");
  EXPECT_THROW(WorkerResult(7, text), Error);
}

TEST(WorkerResult, NativeBytesAndIsErrorSurviveNonzeroExitFromEitherStream) {
  for (auto kind : {WorkerReplyKind::Stdout, WorkerReplyKind::Stderr}) {
    WorkerResult result(7, RequestText());
    ASSERT_TRUE(result.Bind(42));
    std::string native =
        " \n{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"isError\":true,\"n\":1.23456789012345678912345},\"extra\":true}\n";
    Feed(result, native, kind);
    auto done = Finish(result, 7);
    EXPECT_TRUE(done.native);
    EXPECT_EQ(done.response, native);
    EXPECT_EQ(done.exit_code, 7);
  }

  WorkerResult result(7, RequestText());
  ASSERT_TRUE(result.Bind(42));
  std::string native =
      R"({"jsonrpc":"2.0","id":1,"error":{"code":-77,"message":"tool","data":{"native":true}}})";
  Feed(result, native);
  auto done = Finish(result, 8);
  EXPECT_TRUE(done.native);
  EXPECT_EQ(done.response, native);
}

TEST(WorkerResult, FormattingObjectOrderAndStringEscapesDeduplicateLosslessly) {
  WorkerResult result(7, RequestText("0"));
  ASSERT_TRUE(result.Bind(42));
  std::string first =
      R"({"jsonrpc":"2.0","id":-0,"result":{"x":"a","n":1.234567890123456789}})";
  std::string second =
      R"( {"result":{"n":1.234567890123456789,"x":"\u0061"},"id":0,"jsonrpc":"2.0"} )";
  Feed(result, first);
  Feed(result, second, WorkerReplyKind::Stderr);
  auto done = Finish(result);
  ASSERT_TRUE(done.native) << done.response;
  EXPECT_EQ(done.response, first);
}

TEST(WorkerResult, HighPrecisionAndConservativeNumericLexemeConflicts) {
  for (auto values : std::vector<std::pair<std::string, std::string>>{
           {"1.234567890123456789", "1.234567890123456788"},
           {"1.0", "1e0"},
           {"1", "[\"1\"]"},
           {"18446744073709551616", "18446744073709551617"}}) {
    WorkerResult result(7, RequestText());
    ASSERT_TRUE(result.Bind(42));
    Feed(result,
         "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + values.first + "}");
    Feed(result,
         "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + values.second + "}",
         WorkerReplyKind::Stderr);
    auto done = Finish(result);
    EXPECT_FALSE(done.native);
    EXPECT_EQ(Cause(done), "conflicting responses");
  }
}

TEST(WorkerResult, InvalidSecondStreamCannotBeIgnoredAndWhitespaceCan) {
  for (const auto& second :
       {std::string("log"),
        std::string(R"({"jsonrpc":"2.0","id":2,"result":true})"),
        std::string("{} {}")}) {
    WorkerResult result(7, RequestText());
    ASSERT_TRUE(result.Bind(42));
    Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})");
    Feed(result, second, WorkerReplyKind::Stderr);
    EXPECT_EQ(Cause(Finish(result)), "invalid response");
  }

  WorkerResult result(7, RequestText());
  ASSERT_TRUE(result.Bind(42));
  Feed(result, " \t\n");
  Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})",
       WorkerReplyKind::Stderr);
  EXPECT_TRUE(Finish(result).native);
}

TEST(WorkerResult, MalformedDuplicateKeysDepthAndNumericRangeFailClosed) {
  for (
      const auto& text : std::vector<std::string>{
          "", R"({"jsonrpc":"2.0","id":1,"result":1,"result":2})",
          R"({"jsonrpc":"2.0","id":1,"result":{"x":1,"\u0078":2}})",
          R"({"jsonrpc":"2.0","id":1,"result":1e9999})",
          R"({"jsonrpc":"2.0","id":1,"error":{"code":18446744073709551615,"message":"x"}})",
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + std::string(130, '[') +
              "0" + std::string(130, ']') + "}"}) {
    WorkerResult result(7, RequestText());
    ASSERT_TRUE(result.Bind(42));
    Feed(result, text);
    auto done = Finish(result);
    EXPECT_FALSE(done.native);
  }
}

TEST(WorkerResult, SignedIntegerAndUtf8StringIdsRemainExact) {
  for (const std::string id :
       {"-9223372036854775808", "9223372036854775807", "\"한글\""}) {
    WorkerResult result(7, RequestText(id));
    ASSERT_TRUE(result.Bind(42));
    Feed(result, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":true}");
    EXPECT_TRUE(Finish(result).native);
  }

  WorkerResult result(7, RequestText("\"1\""));
  ASSERT_TRUE(result.Bind(42));
  Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})");
  EXPECT_FALSE(Finish(result).native);
}

TEST(WorkerResult, OverflowRequestsCancelButNoResultUntilComplete) {
  WorkerResult result(7, RequestText());
  ASSERT_TRUE(result.Bind(42));
  auto data = Event(WorkerReplyKind::Stdout);
  data.bytes.fill('x');
  data.size = data.bytes.size();
  for (int i = 0; i < 256; ++i) EXPECT_FALSE(result.Accept(data));
  EXPECT_FALSE(result.NeedsCancellation());
  data.size = 1;
  data.kind = WorkerReplyKind::Stderr;
  EXPECT_FALSE(result.Accept(data));
  EXPECT_TRUE(result.NeedsCancellation());
  EXPECT_FALSE(result.Complete());
  auto done = Finish(result);
  EXPECT_EQ(Cause(done), "output limit");
  EXPECT_FALSE(result.NeedsCancellation());
}

TEST(WorkerResult, WorkerFailureAndSignalWinOverProvisionalNativeBytes) {
  for (auto failure : {WorkerFailure::Cancelled, WorkerFailure::Timeout,
                       WorkerFailure::Setup, WorkerFailure::Backpressure}) {
    WorkerResult result(7, RequestText());
    ASSERT_TRUE(result.Bind(42));
    Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})");
    auto done = result.Accept(
        Event(WorkerReplyKind::Complete, failure, -1, SIGKILL, EIO));
    ASSERT_TRUE(done);
    EXPECT_FALSE(done->native);
    auto json = Json::parse(done->response);
    EXPECT_EQ(json["error"]["data"]["systemError"], EIO);
    EXPECT_EQ(json["error"]["data"]["signal"], SIGKILL);
  }

  WorkerResult result(7, RequestText());
  ASSERT_TRUE(result.Bind(42));
  Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})");
  auto done = result.Accept(
      Event(WorkerReplyKind::Complete, WorkerFailure::None, -1, SIGTERM));
  ASSERT_TRUE(done);
  EXPECT_EQ(Cause(*done), "signal termination");
}

TEST(WorkerResult, SessionLossRetainsUncertaintyWithoutTerminalOrReset) {
  WorkerResult result(7, RequestText());
  ASSERT_TRUE(result.Bind(42));
  Feed(result, R"({"jsonrpc":"2.0","id":1,"result":true})");
  result.LoseSession();
  EXPECT_TRUE(result.Uncertain());
  EXPECT_FALSE(result.Complete());
  EXPECT_FALSE(result.Bind(43));
  EXPECT_THROW(result.Accept(Event(WorkerReplyKind::Complete)), Error);
  EXPECT_FALSE(result.Complete());
}

namespace {

struct BuildFault : WorkerResultOperations {
  WorkerResultBuildStage stage = WorkerResultBuildStage::Parse;
  int type = 1;
  bool armed = true;
  void BeforeBuild(WorkerResultBuildStage current) override {
    if (current != stage || !armed) return;
    armed = false;
    if (type == 1) throw std::bad_alloc();
    if (type == 2) throw std::length_error("injected materialization length");
    throw std::out_of_range("injected comparison range");
  }
};

}  // namespace

TEST(WorkerResult, TerminalAllocationFailureRetainsProofAndRetriesExactlyOnce) {
  for (auto stage :
       {WorkerResultBuildStage::Parse, WorkerResultBuildStage::Compare,
        WorkerResultBuildStage::Failure}) {
    for (int type : {1, 2}) {
      BuildFault fault;
      fault.stage = stage;
      fault.type = type;
      WorkerResult result(7, RequestText(), &fault);
      ASSERT_TRUE(result.Bind(42));
      std::string native = R"({"jsonrpc":"2.0","id":1,"result":true})";
      Feed(result, native);
      Feed(result, native, WorkerReplyKind::Stderr);
      auto event = Event(WorkerReplyKind::Complete,
                         stage == WorkerResultBuildStage::Failure
                             ? WorkerFailure::Timeout
                             : WorkerFailure::None,
                         0);
      if (type == 1) {
        EXPECT_THROW(result.Accept(event), std::bad_alloc);
      } else {
        EXPECT_THROW(result.Accept(event), std::length_error);
      }
      EXPECT_FALSE(result.Complete());
      EXPECT_TRUE(result.TerminalPending());
      EXPECT_FALSE(result.NeedsCancellation());
      EXPECT_THROW(result.Accept(event), Error);
      EXPECT_THROW(result.Accept(Event(WorkerReplyKind::Stdout)), Error);
      // Cleanup is already proven; later session loss cannot erase that proof.
      result.LoseSession();
      EXPECT_FALSE(result.Uncertain());
      auto done = result.RetryTerminal();
      EXPECT_TRUE(result.Complete());
      EXPECT_FALSE(result.TerminalPending());
      EXPECT_EQ(done.native, stage != WorkerResultBuildStage::Failure);
      if (done.native)
        EXPECT_EQ(done.response, native);
      else
        EXPECT_EQ(Cause(done), "timeout");
      EXPECT_THROW(result.RetryTerminal(), Error);
    }
  }
}

TEST(WorkerResult,
     ComparisonRangeFailureBecomesOneSyntheticTerminalAfterProof) {
  BuildFault fault;
  fault.stage = WorkerResultBuildStage::Compare;
  fault.type = 3;
  WorkerResult result(7, RequestText(), &fault);
  ASSERT_TRUE(result.Bind(42));
  std::string native = R"({"jsonrpc":"2.0","id":1,"result":true})";
  Feed(result, native);
  Feed(result, native, WorkerReplyKind::Stderr);
  auto done = Finish(result);
  EXPECT_FALSE(done.native);
  EXPECT_EQ(Cause(done), "response comparison");
  EXPECT_TRUE(result.Complete());
  EXPECT_FALSE(result.TerminalPending());
  EXPECT_THROW(result.RetryTerminal(), Error);
}
