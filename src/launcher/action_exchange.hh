// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "launcher/runner.hh"
namespace capmgr {
struct ActionFrame {
  std::string json;
  bool is_event = false;
  bool complete = true;
};
// Serialized per-execution protocol state, independent of callback thread/transport.
// The adapter must reserve a unique positive native ID for the connection until
// complete, and must not retry execution on reconnect. No Action API call occurs here.
class ActionExchange {
 public:
  ActionExchange(const Entry& entry, const Request& request, int native_id,
                 bool subscription_api_available);
  const std::string& NativeRequest() const { return native_request_; }
  ActionFrame Accept(int callback_id, const std::string& native_reply);

 private:
  enum class State { kAwaiting, kStreaming, kComplete };
  State state_ = State::kAwaiting;
  int native_id_;
  Json original_id_;
  bool subscription_;
  std::string native_request_;
};
}
