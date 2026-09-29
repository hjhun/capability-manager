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

#include "launcher/action_exchange.hh"

#include <gtest/gtest.h>

#include <climits>

using namespace capmgr;

namespace {

Entry Action(bool stream = false) {
  Entry entry;
  entry.kind = Kind::kAction;
  entry.key = "Example:Action%Name";
  entry.id = CanonicalId(entry.kind, entry.key);
  entry.detail = Json::object();
  if (stream) entry.detail["eventSchema"] = {{"type", "object"}};
  return entry;
}

Request Call(Json id = "original") {
  return ParseRequest(
      Json({{"jsonrpc", "2.0"},
            {"id", id},
            {"method", "tools/call"},
            {"params", {{"name", Action().id}, {"arguments", Json::object()}}}})
          .dump());
}
}  // namespace

TEST(ActionExchange, MapsIndependentIdsAndRegisteredName) {
  for (const Json& id :
       {Json("same:☃"), Json(INT64_MIN), Json(INT64_MAX), Json(0)}) {
    ActionExchange exchange(Action(), Call(id), INT_MAX, false);
    auto native = Json::parse(exchange.NativeRequest());
    EXPECT_EQ(native["id"], INT_MAX);
    EXPECT_EQ(native["params"]["name"], Action().key);
    EXPECT_EQ(native.size(), 2u);
    auto frame = exchange.Accept(
        INT_MAX,
        "{\"jsonrpc\":\"2.0\",\"id\":2147483647,\"result\":{\"isError\":true}}");
    EXPECT_EQ(Json::parse(frame.json)["id"], id);
    EXPECT_FALSE(frame.is_event);
    EXPECT_TRUE(frame.complete);
    EXPECT_TRUE(Json::parse(frame.json)["result"]["isError"]);
    EXPECT_THROW(exchange.Accept(INT_MAX, "{}"), Error);
  }

  EXPECT_THROW(ActionExchange(Action(), Call(), 0, false), Error);
  EXPECT_THROW(ActionExchange(Action(), Call(), -1, false), Error);
}

TEST(ActionExchange,
     PreservesArgumentAndReplyPayloadBytesWhileReplacingOnlyOuterId) {
  auto request = Call();
  auto source = Json::parse(request.original);
  std::string args =
      R"({"precise":1.2345678901234567890123,"nested":{"id":91,"text":"}\\\"["}})";
  request.original =
      "{\"params\":{\"arguments\":" + args +
      ",\"name\":" + Json(Action().id).dump() +
      "},\"id\":\"original\",\"method\":\"tools/call\",\"jsonrpc\":\"2.0\"}";
  ActionExchange exchange(Action(), request, 7, false);
  EXPECT_NE(exchange.NativeRequest().find(args), std::string::npos);
  std::string before =
      R"( { "unknown": [{"id":44}], "jsonrpc":"2.0", "i\u0064" : 7 , "result":{"number":1.2345678901234567890123,"text":"id:7"} } )";
  auto expected = before;
  auto at = expected.find(" : 7 ");
  expected.replace(at, 5, " : \"original\" ");
  EXPECT_EQ(exchange.Accept(7, before).json, expected);
}

TEST(ActionExchange, NativeErrorsRemainNativeAndWrongIdsDoNotAdvanceState) {
  ActionExchange exchange(Action(), Call(-5), 8, false);
  EXPECT_THROW(exchange.Accept(9, R"({"jsonrpc":"2.0","id":8,"result":{}})"),
               Error);
  EXPECT_THROW(exchange.Accept(8, R"({"jsonrpc":"2.0","id":9,"result":{}})"),
               Error);
  auto result = exchange.Accept(
      8,
      R"({"jsonrpc":"2.0","id":8,"error":{"code":-42,"message":"native","data":{"cause":"provider"}}})");
  EXPECT_EQ(Json::parse(result.json)["error"]["code"], -42);
  EXPECT_EQ(Json::parse(result.json)["id"], -5);
}

TEST(ActionExchange, SubscriptionCapabilityRequiredBeforeAdmission) {
  EXPECT_THROW(ActionExchange(Action(true), Call(), 1, false), Error);
  auto request = Call();
  request.capability_id = "action:other";
  EXPECT_THROW(ActionExchange(Action(), request, 1, true), Error);
  auto json = Json::parse(Call().original);
  json["params"]["appid"] = "unverified.provider";
  EXPECT_THROW(ActionExchange(Action(), ParseRequest(json.dump()), 1, true),
               Error);
}

TEST(ActionExchange, AckEventsAndCloseHaveIndependentTerminalFlag) {
  ActionExchange exchange(Action(true), Call(), 1, true);
  EXPECT_THROW(
      exchange.Accept(1, R"({"jsonrpc":"2.0","id":1,"event":{"seq":0}})"),
      Error);
  auto ack = exchange.Accept(
      1,
      R"({"jsonrpc":"2.0","id":1,"result":{"subscription":true,"isError":false}})");
  EXPECT_FALSE(ack.is_event);
  EXPECT_FALSE(ack.complete);
  EXPECT_THROW(exchange.Accept(1, R"({"jsonrpc":"2.0","id":1,"result":{}})"),
               Error);
  auto event = exchange.Accept(
      1,
      R"({"jsonrpc":"2.0","id":1,"event":{"seq":1,"isError":true,"detail":"native"}})");
  EXPECT_TRUE(event.is_event);
  EXPECT_FALSE(event.complete);
  EXPECT_EQ(Json::parse(event.json)["event"]["detail"], "native");
  auto closed = exchange.Accept(
      1, R"({"jsonrpc":"2.0","id":1,"event":{"seq":2,"closed":"cancelled"}})");
  EXPECT_TRUE(closed.is_event);
  EXPECT_TRUE(closed.complete);
  EXPECT_THROW(exchange.Accept(1, R"({"jsonrpc":"2.0","id":1,"event":{}})"),
               Error);
}

TEST(ActionExchange, PreAckFailureTerminatesWithoutClosedEvent) {
  ActionExchange exchange(Action(true), Call(), 1, true);
  auto reply = exchange.Accept(
      1,
      R"({"jsonrpc":"2.0","id":1,"result":{"subscription":true,"isError":true}})");
  EXPECT_FALSE(reply.is_event);
  EXPECT_TRUE(reply.complete);
}

TEST(ActionExchange, MalformedDuplicateNestedAndOversizedRepliesAreRejected) {
  ActionExchange exchange(Action(), Call(), 1, false);
  for (const char* reply :
       {"", "not-json", R"({"jsonrpc":"2.0","id":1,"id":1,"result":{}})",
        R"({"jsonrpc":"2.0","id":1,"result":{},"error":{}})",
        R"({"jsonrpc":"2.0","id":1.0,"result":{}})",
        R"({"jsonrpc":"2.0","id":1,"error":{"code":0}})",
        R"({"jsonrpc":"2.0","id":1,"result":{"subscription":true}})"})
    EXPECT_THROW(exchange.Accept(1, reply), Error) << reply;
  EXPECT_THROW(exchange.Accept(1, std::string(1024 * 1024 + 1, ' ')), Error);
  auto nested = std::string(130, '[') + "0" + std::string(130, ']');
  EXPECT_THROW(exchange.Accept(1, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" +
                                      nested + "}"),
               Error);
}
