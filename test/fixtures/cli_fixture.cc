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

#include <nlohmann/json.hpp>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <cstring>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3 || std::strcmp(argv[1], "--json") != 0) return 90;
  auto request = nlohmann::json::parse(argv[2]);
  auto args = request.at("params").at("arguments");
  std::string mode = args.value("mode", "normal");
  nlohmann::json response = {{"jsonrpc", "2.0"},
                             {"id", request["id"]},
                             {"result", {{"argv", argv[2]}}}};
  if (mode == "fd")
    response["result"]["fdOpen"] =
        fcntl(args.at("fd").get<int>(), F_GETFD) != -1;
  if (mode == "signal-state") {
    sigset_t mask;
    if (sigprocmask(SIG_SETMASK, nullptr, &mask) != 0) return 93;
    bool empty_mask = true, defaults = true;
    for (int signal = 1; signal < NSIG; ++signal)
      if (sigismember(&mask, signal) == 1) empty_mask = false;
    for (int signal : {SIGCHLD, SIGPIPE, SIGTERM, SIGINT, SIGHUP, SIGALRM,
                       SIGUSR1, SIGUSR2}) {
      struct sigaction action{};
      if (sigaction(signal, nullptr, &action) != 0) return 94;
      if (action.sa_handler != SIG_DFL) defaults = false;
    }
    response["result"]["emptyMask"] = empty_mask;
    response["result"]["eightDefaults"] = defaults;
    response["result"]["pid"] = getpid();
    response["result"]["ppid"] = getppid();
    response["result"]["pgid"] = getpgrp();
    response["result"]["sid"] = getsid(0);
    response["result"]["sentinelOpen"] =
        fcntl(args.at("sentinel").get<int>(), F_GETFD) != -1;
  }
  if (mode == "error") {
    response.erase("result");
    response["error"] = {
        {"code", -77}, {"message", "native"}, {"data", {{"unchanged", true}}}};
  }
  if (mode == "is-error")
    response["result"] = {{"isError", true}, {"nativeCode", "KEEP"}};
  if (mode == "wrong-id") response["id"] = "other";
  if (mode == "hang") {
    for (;;) pause();
  }
  if (mode == "fork") {
    pid_t pid = fork();
    if (pid == 0) {
      for (;;) pause();
    }
    std::ofstream(args.at("pidFile").get<std::string>()) << pid;
  }
  std::string out = response.dump();
  if (mode == "invalid") out = "not JSON";
  if (mode == "multiple") out += out;
  if (mode == "large") out = std::string(1024 * 1024 + 1, 'x');
  int fd = mode == "stderr" ? STDERR_FILENO : STDOUT_FILENO;
  size_t offset = 0;
  while (offset < out.size()) {
    size_t chunk = mode == "partial" ? 1 : out.size() - offset;
    ssize_t written = write(fd, out.data() + offset, chunk);
    if (written <= 0) return 91;
    offset += static_cast<size_t>(written);
  }
  if (mode == "dual" || mode == "conflict") {
    if (mode == "conflict") {
      response["result"] = false;
      out = response.dump();
    }
    if (write(STDERR_FILENO, out.data(), out.size()) < 0) return 92;
  }
  return mode == "error" || mode == "nonzero" ? 7 : 0;
}
