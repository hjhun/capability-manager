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

#ifndef CAPABILITY_MANAGER_LAUNCHER_RUNNER_HH_
#define CAPABILITY_MANAGER_LAUNCHER_RUNNER_HH_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>

#include "catalog/catalog.hh"

namespace capmgr {

struct Request {
  Json id;
  std::string capability_id;
  std::string original;
};

Request ParseRequest(const std::string& request);
struct RunLimits {
  std::chrono::milliseconds timeout{30000};
  size_t output_bytes = 1024 * 1024;
};

struct RunResult {
  std::string response;
  int exit_code = -1;
  int signal = 0;
  bool native = false;
};

// Worker operation. Production callers must authorize peer and bind executable
// from the private catalog before calling; never expose arbitrary paths over IPC.
RunResult RunCli(const std::string& executable, const Request& request,
                 const std::atomic<bool>& cancelled, RunLimits limits = {});
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_RUNNER_HH_
