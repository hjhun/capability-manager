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

#include "launcher/action_exchange.hh"

#include <algorithm>
#include <set>
#include <string_view>

namespace capmgr {

namespace {

void Require(bool condition, const char* reason) {
  if (!condition) throw Error(ErrorCode::kInvalid, reason);
}

Json Parse(const std::string& text, size_t limit) {
  if (text.size() > limit)
    throw Error(ErrorCode::kLimit, "Action envelope exceeds limit");
  std::vector<std::set<std::string>> keys;
  auto result = Json::parse(
      text,
      [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 128)
          throw Error(ErrorCode::kLimit, "Action envelope nesting limit");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key)
          Require(keys.back().insert(value.get<std::string>()).second,
                  "Duplicate Action envelope key");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
      },
      false);
  Require(!result.is_discarded() && result.is_object(),
          "Malformed Action envelope");
  return result;
}
// Locate values only after full bounded JSON validation. Replace just the routing
// tokens, retaining every other byte (including high-precision numeric payloads).
struct Span {
  size_t begin, end;
};

size_t Space(const std::string& text, size_t pos) {
  while (pos < text.size() &&
         std::string_view(" \t\r\n").find(text[pos]) != std::string_view::npos)
    ++pos;
  return pos;
}

size_t StringEnd(const std::string& text, size_t pos) {
  ++pos;
  while (pos < text.size()) {
    if (text[pos] == '\\')
      pos += 2;
    else if (text[pos++] == '"')
      return pos;
  }

  throw Error(ErrorCode::kInvalid, "Invalid JSON string boundary");
}

size_t ValueEnd(const std::string& text, size_t pos) {
  if (text.at(pos) == '"') return StringEnd(text, pos);
  if (text[pos] == '{' || text[pos] == '[') {
    size_t depth = 0;
    do {
      if (text[pos] == '"') {
        pos = StringEnd(text, pos);
        continue;
      }
      if (text[pos] == '{' || text[pos] == '[')
        ++depth;
      else if (text[pos] == '}' || text[pos] == ']')
        --depth;
      ++pos;
    } while (depth && pos < text.size());
    return pos;
  }

  while (pos < text.size() && std::string_view(",]} \t\r\n").find(text[pos]) ==
                                  std::string_view::npos)
    ++pos;
  return pos;
}

Span Member(const std::string& text, size_t object, const char* name) {
  size_t pos = Space(text, object) + 1;
  for (;;) {
    pos = Space(text, pos);
    Require(pos < text.size() && text[pos] == '"', "Missing routing field");
    auto key_end = StringEnd(text, pos);
    auto key = Json::parse(text.substr(pos, key_end - pos)).get<std::string>();
    pos = Space(text, key_end) + 1;
    pos = Space(text, pos);
    auto end = ValueEnd(text, pos);
    if (key == name) return {pos, end};
    pos = Space(text, end);
    Require(pos < text.size() && text[pos] == ',', "Missing routing field");
    ++pos;
  }
}

std::string Rewrite(std::string text,
                    std::vector<std::pair<Span, std::string>> changes,
                    size_t limit) {
  std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
    return a.first.begin > b.first.begin;
  });
  for (const auto& [span, value] : changes)
    text.replace(span.begin, span.end - span.begin, value);
  if (text.size() > limit)
    throw Error(ErrorCode::kLimit, "Mapped Action envelope exceeds limit");
  return text;
}
}  // namespace

ActionExchange::ActionExchange(const Entry& entry, const Request& request,
                               int native_id, bool subscription_api_available)
    : native_id_(native_id),
      original_id_(request.id),
      subscription_(entry.detail.contains("eventSchema")) {
  Require(native_id > 0, "Action execution ID must be positive");
  Require(entry.kind == Kind::kAction && entry.id == request.capability_id &&
              entry.id == CanonicalId(Kind::kAction, entry.key),
          "Action binding mismatch");
  if (subscription_ && !subscription_api_available)
    throw Error(ErrorCode::kUnsupported,
                "Action subscription API is unavailable");
  auto parsed = Parse(request.original, 64 * 1024);
  auto validated = ParseRequest(request.original);
  Require(validated.id == request.id &&
              validated.id.is_string() == request.id.is_string() &&
              validated.capability_id == request.capability_id,
          "Request fields disagree");
  Require(!parsed["params"].contains("appid"),
          "Action provider routing is not authorized");
  auto params = Member(request.original, 0, "params");
  auto arguments = Member(request.original, params.begin, "arguments");
  native_request_ = "{\"id\":" + std::to_string(native_id) +
                    ",\"params\":{\"name\":" + Json(entry.key).dump() +
                    ",\"arguments\":" +
                    request.original.substr(arguments.begin,
                                            arguments.end - arguments.begin) +
                    "}}";
  if (native_request_.size() > 64 * 1024)
    throw Error(ErrorCode::kLimit, "Mapped Action request exceeds limit");
}

ActionFrame ActionExchange::Accept(int callback_id, const std::string& text) {
  Require(state_ != State::kComplete, "Action exchange already completed");
  Require(callback_id == native_id_, "Action callback ID mismatch");
  auto reply = Parse(text, 1024 * 1024);
  Require(reply.value("jsonrpc", Json()) == "2.0" && reply.contains("id") &&
              reply["id"].is_number_integer() && reply["id"] == native_id_,
          "Action reply ID or version mismatch");
  unsigned fields = reply.contains("result") + reply.contains("error") +
                    reply.contains("event");
  Require(fields == 1, "Ambiguous Action reply");
  bool event = reply.contains("event"), complete = true;
  if (event) {
    Require(state_ == State::kStreaming && reply["event"].is_object(),
            "Unexpected Action event");
    const auto& body = reply["event"];
    complete = body.contains("closed");
    if (complete)
      Require(body["closed"].is_string() &&
                  !body["closed"].get_ref<const std::string&>().empty(),
              "Invalid Action close reason");
  } else {
    Require(state_ == State::kAwaiting, "Duplicate Action result");
    if (reply.contains("error")) {
      const auto& error = reply["error"];
      Require(error.is_object() && error.contains("code") &&
                  error["code"].is_number_integer() &&
                  (!error["code"].is_number_unsigned() ||
                   error["code"].get<uint64_t>() <=
                       static_cast<uint64_t>(INT64_MAX)) &&
                  error.contains("message") && error["message"].is_string(),
              "Malformed Action error");
    } else if (reply["result"].is_object()) {
      const auto& result = reply["result"];
      if (result.contains("subscription"))
        Require(result["subscription"].is_boolean(),
                "Invalid subscription acknowledgement");
      if (result.contains("isError"))
        Require(result["isError"].is_boolean(),
                "Invalid Action error indicator");
      bool streaming = result.value("subscription", false) &&
                       !result.value("isError", false);
      Require(!streaming || subscription_, "Undeclared Action subscription");
      complete = !streaming;
    }
  }

  auto mapped = Rewrite(text, {{Member(text, 0, "id"), original_id_.dump()}},
                        1024 * 1024);
  state_ = complete ? State::kComplete : State::kStreaming;
  return {std::move(mapped), event, complete};
}
}  // namespace capmgr
