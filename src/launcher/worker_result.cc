// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_result.hh"
#include <set>
#include <string_view>
#include <type_traits>
namespace capmgr {
namespace {
static_assert(std::is_nothrow_move_constructible_v<RunResult>);
constexpr size_t kLimit = 1024 * 1024;
void Require(bool ok, const char* why) {
  if (!ok) throw Error(ErrorCode::kInvalid, why);
}
Json Parse(const std::string& text, size_t limit, int maximum_depth = 128) {
  Require(text.size() <= limit, "Worker JSON limit");
  std::vector<std::set<std::string>> keys;
  auto json = Json::parse(
      text,
      [&](int depth, Json::parse_event_t event, Json& value) {
        Require(depth <= maximum_depth, "Worker JSON nesting limit");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key)
          Require(keys.back().insert(value.get<std::string>()).second,
                  "Duplicate worker JSON key");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
      },
      false);
  Require(!json.is_discarded() && json.is_object(), "Malformed worker JSON");
  return json;
}
bool Integer(const Json& value) {
  return value.is_number_integer() &&
         (!value.is_number_unsigned() ||
          value.get<uint64_t>() <= uint64_t{INT64_MAX});
}
void Envelope(const Json& value, const Json& id) {
  Require(
      value.value("jsonrpc", Json()) == "2.0" && value.contains("id") &&
          (id.is_string() ? value["id"].is_string() : Integer(value["id"])) &&
          value["id"] == id &&
          value.contains("result") != value.contains("error") &&
          !value.contains("event"),
      "Invalid worker response");
  if (value.contains("error")) {
    const auto& error = value["error"];
    Require(error.is_object() && error.contains("code") &&
                Integer(error["code"]) && error.contains("message") &&
                error["message"].is_string(),
            "Malformed worker error");
  }
}
// Called only after bounded JSON validation. Keep numeric lexemes in the private
// comparison tree as binary nodes, a type JSON input cannot forge. Object order,
// whitespace and decoded string escapes compare structurally; numbers compare
// exact lexemes except integer -0/0. In particular 1.0 vs 1e0 conservatively
// conflicts, and two decimals cannot become equal through double rounding.
Json Comparison(const std::string& text) {
  std::vector<std::string> numbers;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '"') {
      ++i;
      while (i < text.size()) {
        if (text[i] == '\\')
          i += 2;
        else if (text[i++] == '"')
          break;
      }
    } else if (text[i] == '-' || (text[i] >= '0' && text[i] <= '9')) {
      size_t begin = i++;
      while (i < text.size() &&
             std::string_view("0123456789.eE+-").find(text[i]) !=
                 std::string_view::npos)
        ++i;
      auto token = text.substr(begin, i - begin);
      numbers.push_back(token == "-0" ? "0" : std::move(token));
    } else
      ++i;
  }
  size_t index = 0;
  auto tree =
      Json::parse(text, [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::value && value.is_number()) {
          const auto& token = numbers.at(index++);
          value =
              Json::binary(std::vector<uint8_t>(token.begin(), token.end()));
        }
        return true;
      });
  Require(index == numbers.size(), "Worker numeric comparison mismatch");
  return tree;
}
const char* Cause(WorkerFailure failure) {
  switch (failure) {
    case WorkerFailure::None:
      return "none";
    case WorkerFailure::Rejected:
      return "rejected";
    case WorkerFailure::Clone:
      return "clone failed";
    case WorkerFailure::Setup:
      return "setup failed";
    case WorkerFailure::Timeout:
      return "timeout";
    case WorkerFailure::Cancelled:
      return "cancelled";
    case WorkerFailure::ParentLost:
      return "parent lost";
    case WorkerFailure::Protocol:
      return "worker protocol";
    case WorkerFailure::OutputLimit:
      return "output limit";
    case WorkerFailure::Backpressure:
      return "backpressure";
    case WorkerFailure::Channel:
      return "worker channel";
  }
  return "worker protocol";
}
RunResult Failure(const Request& request, const WorkerEvent& event,
                  const char* cause) {
  Json json = {{"jsonrpc", "2.0"},
               {"id", request.id},
               {"error",
                {{"code", -32090},
                 {"message", "Capability transport failure"},
                 {"data",
                  {{"cause", cause},
                   {"workerFailure", Cause(event.failure)},
                   {"exitCode", event.code},
                   {"signal", event.signal},
                   {"systemError", event.error}}}}}};
  return {json.dump(), event.code, event.signal, false};
}
}
WorkerResult::WorkerResult(uint64_t client_token, const std::string& text,
                           WorkerResultOperations* operations)
    : operations_(operations), client_token_(client_token) {
  Require(client_token != 0, "Missing client token");
  Parse(text, 64 * 1024, 64);
  request_ = ParseRequest(text);
  Require(request_.capability_id.starts_with("cli:") &&
              request_.capability_id.size() > 4,
          "Worker result requires CLI request");
}
bool WorkerResult::Bind(uint64_t token) noexcept {
  if (!token || worker_token_ || complete_ || uncertain_) return false;
  worker_token_ = token;
  return true;
}
bool WorkerResult::NeedsCancellation() const noexcept {
  return failure_ && !complete_ && !uncertain_ && !terminal_pending_;
}
void WorkerResult::LoseSession() noexcept {
  if (!complete_ && !terminal_pending_) uncertain_ = true;
}
std::optional<RunResult> WorkerResult::Accept(const WorkerEvent& event) {
  Require(worker_token_ && event.token == worker_token_ && !complete_ &&
              !uncertain_ && !terminal_pending_,
          "Worker result correlation/lifetime");
  if (event.kind == WorkerReplyKind::Accepted) return std::nullopt;
  if (event.kind == WorkerReplyKind::Stdout ||
      event.kind == WorkerReplyKind::Stderr) {
    if (event.size > event.bytes.size() || event.size > kLimit - total_)
      failure_ = "output limit";
    if (!failure_) {
      try {
        streams_[event.kind == WorkerReplyKind::Stdout ? 0 : 1].append(
            event.bytes.data(), event.size);
        total_ += event.size;
      } catch (const std::bad_alloc&) {
        failure_ = "buffer allocation";
      }
    }
    return std::nullopt;
  }
  Require(event.kind == WorkerReplyKind::Complete, "Unexpected worker event");
  terminal_ = event;
  terminal_pending_ = true;
  return RetryTerminal();
}
RunResult WorkerResult::RetryTerminal() {
  Require(terminal_pending_ && !complete_ && !uncertain_,
          "No pending worker terminal");
  auto result = BuildTerminal();  // All allocating work precedes sealing.
  complete_ = true;
  terminal_pending_ = false;
  return result;
}
RunResult WorkerResult::BuildTerminal() {
  const auto& event = terminal_;
  auto fail = [&](const char* cause) {
    if (operations_) operations_->BeforeBuild(WorkerResultBuildStage::Failure);
    return Failure(request_, event, cause);
  };
  if (event.failure != WorkerFailure::None) return fail(Cause(event.failure));
  if (event.signal) return fail("signal termination");
  if (failure_) return fail(failure_);
  bool present[2] = {false, false};
  try {
    if (operations_) operations_->BeforeBuild(WorkerResultBuildStage::Parse);
    for (size_t i = 0; i < 2; ++i) {
      present[i] =
          streams_[i].find_first_not_of(" \t\r\n") != std::string::npos;
      if (present[i]) Envelope(Parse(streams_[i], kLimit), request_.id);
    }
    if (!present[0] && !present[1]) return fail("no response");
    if (present[0] && present[1]) {
      if (operations_)
        operations_->BeforeBuild(WorkerResultBuildStage::Compare);
      if (Comparison(streams_[0]) != Comparison(streams_[1]))
        return fail("conflicting responses");
    }
  } catch (const Error&) {
    return fail("invalid response");
  } catch (const Json::exception&) {
    return fail("invalid response");
  } catch (const std::out_of_range&) {
    return fail("response comparison");
  }
  return RunResult{std::move(streams_[present[0] ? 0 : 1]), event.code,
                   event.signal, true};
}
}
