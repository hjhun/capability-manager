// SPDX-License-Identifier: Apache-2.0
#pragma once
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
}
