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
// SPDX-License-Identifier: Apache-2.0

#ifndef CAPABILITY_MANAGER_LAUNCHER_ACTION_EXCHANGE_HH_
#define CAPABILITY_MANAGER_LAUNCHER_ACTION_EXCHANGE_HH_

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

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_ACTION_EXCHANGE_HH_
