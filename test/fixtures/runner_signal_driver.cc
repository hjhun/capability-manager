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

// Ordinary isolated process only. No parent test process signal mutation.
#include "launcher/runner.hh"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::array kDefaults = {SIGCHLD, SIGPIPE, SIGTERM, SIGINT,
                                  SIGHUP,  SIGALRM, SIGUSR1, SIGUSR2};
const char* fault = nullptr;
int spawn_calls = 0;
pid_t returned_pid = -1, reaped_pid = -1;
int reaped_status = -1;

void Check(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}

bool Fault(const char* operation) {
  return fault && std::string_view(fault) == operation;
}

struct State {
  sigset_t mask{};
  std::array<struct sigaction, NSIG> dispositions{};
  std::array<bool, NSIG> valid{};

  State() {
    Check(sigprocmask(SIG_SETMASK, nullptr, &mask) == 0, "own mask query");
    for (int signal = 1; signal < NSIG; ++signal) {
      if (sigaction(signal, nullptr, &dispositions[signal]) == 0)
        valid[signal] = true;
      else
        Check(errno == EINVAL, "own disposition query");
    }
  }

  bool Same(const State& other) const {
    for (int signal = 1; signal < NSIG; ++signal) {
      if (sigismember(&mask, signal) != sigismember(&other.mask, signal) ||
          valid[signal] != other.valid[signal])
        return false;
      if (!valid[signal]) continue;
      const auto& a = dispositions[signal];
      const auto& b = other.dispositions[signal];
      if (a.sa_handler != b.sa_handler || a.sa_flags != b.sa_flags)
        return false;
      for (int member = 1; member < NSIG; ++member)
        if (sigismember(&a.sa_mask, member) != sigismember(&b.sa_mask, member))
          return false;
    }
    return true;
  }
};

void EmptyChildren() {
  siginfo_t info{};
  errno = 0;
  Check(waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT) == -1 &&
            errno == ECHILD,
        "exclusive empty child boundary");
}

capmgr::Request Request(int sentinel) {
  return capmgr::ParseRequest(capmgr::Json{
      {"jsonrpc", "2.0"},
      {"id", "signal-driver"},
      {"method", "tools/call"},
      {"params",
       {{"name", "cli:test"},
        {"arguments", {{"mode", "signal-state"}, {"sentinel", sentinel}}}}}}
                                  .dump());
}

void Run(const std::string& mode) {
  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  Check(sigemptyset(&action.sa_mask) == 0 &&
            sigaction(SIGCHLD, &action, nullptr) == 0,
        "isolated default SIGCHLD");
  EmptyChildren();
  if (mode == "ignored-parent" || mode == "nocldwait-parent") {
    if (mode == "ignored-parent") action.sa_handler = SIG_IGN;
    if (mode == "nocldwait-parent") action.sa_flags = SA_NOCLDWAIT;
    Check(sigaction(SIGCHLD, &action, nullptr) == 0, "parent auto-reap case");
  } else {
    sigset_t blocked{};
    Check(sigemptyset(&blocked) == 0, "blocked mask init");
    for (int signal : kDefaults) {
      Check(sigaddset(&blocked, signal) == 0, "blocked mask member");
      if (signal == SIGCHLD) continue;
      action.sa_handler = SIG_IGN;
      Check(sigaction(signal, &action, nullptr) == 0, "parent ignored signal");
    }
    Check(sigprocmask(SIG_SETMASK, &blocked, nullptr) == 0,
          "isolated parent blocked mask");
    if (mode.starts_with("fault-")) fault = mode.c_str() + 6;
  }
  const State before;
  const int source = open("/dev/null", O_RDONLY | O_CLOEXEC);
  Check(source >= 0, "sentinel source");
  const int sentinel = fcntl(source, F_DUPFD, 1000);
  Check(close(source) == 0 && sentinel >= 1000 && fcntl(sentinel, F_GETFD) == 0,
        "inheritable high sentinel");
  auto fixture =
      (std::filesystem::read_symlink("/proc/self/exe").parent_path() /
       "capmgr-cli-fixture")
          .string();
  if (mode == "exec-failure") fixture = "/does/not/exist/capmgr-cli-fixture";
  std::atomic<bool> cancelled = false;
  const auto result = capmgr::RunCli(fixture, Request(sentinel), cancelled,
                                     {std::chrono::seconds(2), 1024 * 1024});
  Check(close(sentinel) == 0, "parent sentinel close");
  Check(before.Same(State()), "parent full mask/dispositions changed");
  EmptyChildren();
  if (mode == "ignored-parent" || mode == "nocldwait-parent") {
    Check(!result.native && !spawn_calls, "auto-reaping parent spawned child");
    Check(capmgr::Json::parse(result.response)["error"]["data"]["cause"] ==
              "unsupported parent SIGCHLD",
          "unsupported parent refusal");
  } else if (fault) {
    Check(!result.native && !spawn_calls, "failed attributes spawned child");
  } else if (mode == "exec-failure") {
    Check(!result.native && spawn_calls == 1, "exec failure outcome");
  } else {
    Check(result.native && result.exit_code == 0 && !result.signal &&
              spawn_calls == 1 && returned_pid > 0 &&
              reaped_pid == returned_pid && WIFEXITED(reaped_status) &&
              WEXITSTATUS(reaped_status) == 0,
          "normal exact owned CLI result");
    const auto data = capmgr::Json::parse(result.response).at("result");
    Check(data.at("emptyMask") == true && data.at("eightDefaults") == true,
          "child signal reset");
    Check(data.at("sentinelOpen") == false, "inheritable high FD survived");
    Check(data.at("pid").get<pid_t>() > 0 &&
              data.at("pid").get<pid_t>() == returned_pid &&
              data.at("pid") == data.at("pgid") &&
              data.at("ppid").get<pid_t>() == getpid() &&
              data.at("sid").get<pid_t>() == getsid(0),
          "observed inherited SID/owned PGID");
    std::cout << "OBSERVED_CLI_PID=" << data.at("pid")
              << " PGID=" << data.at("pgid") << " SID=" << data.at("sid")
              << " SID_INHERITED=true\n";
    std::cout << "OWNED_CLI_REAP_PID=" << reaped_pid << " STATUS=0\n";
  }
  std::cout << "ISOLATED_SIGNAL_CASE_PASS=" << mode
            << " SPAWN_CALLS=" << spawn_calls << '\n';
}

}  // namespace

extern "C" int __real_posix_spawn(pid_t*, const char*,
                                  const posix_spawn_file_actions_t*,
                                  const posix_spawnattr_t*, char* const[],
                                  char* const[]);
extern "C" int __wrap_posix_spawn(pid_t* pid, const char* path,
                                  const posix_spawn_file_actions_t* actions,
                                  const posix_spawnattr_t* attributes,
                                  char* const argv[], char* const env[]) {
  ++spawn_calls;
  const int result =
      __real_posix_spawn(pid, path, actions, attributes, argv, env);
  if (!result && *pid > 0) returned_pid = *pid;
  return result;
}

extern "C" pid_t __real_waitpid(pid_t, int*, int);
extern "C" pid_t __wrap_waitpid(pid_t pid, int* status, int options) {
  const pid_t result = __real_waitpid(pid, status, options);
  if (result > 0 && result == returned_pid && status) {
    reaped_pid = result;
    reaped_status = *status;
  }
  return result;
}

extern "C" int __real_posix_spawnattr_setsigmask(posix_spawnattr_t*,
                                                 const sigset_t*);
extern "C" int __wrap_posix_spawnattr_setsigmask(posix_spawnattr_t* attr,
                                                 const sigset_t* set) {
  return Fault("mask") ? EINVAL : __real_posix_spawnattr_setsigmask(attr, set);
}

extern "C" int __real_posix_spawnattr_setsigdefault(posix_spawnattr_t*,
                                                    const sigset_t*);
extern "C" int __wrap_posix_spawnattr_setsigdefault(posix_spawnattr_t* attr,
                                                    const sigset_t* set) {
  return Fault("defaults") ? EINVAL
                           : __real_posix_spawnattr_setsigdefault(attr, set);
}

extern "C" int __real_posix_spawnattr_setflags(posix_spawnattr_t*, short);
extern "C" int __wrap_posix_spawnattr_setflags(posix_spawnattr_t* attr,
                                               short flags) {
  return Fault("flags") ? EINVAL : __real_posix_spawnattr_setflags(attr, flags);
}

extern "C" int __real_posix_spawnattr_setpgroup(posix_spawnattr_t*, pid_t);
extern "C" int __wrap_posix_spawnattr_setpgroup(posix_spawnattr_t* attr,
                                                pid_t group) {
  return Fault("group") ? EINVAL
                        : __real_posix_spawnattr_setpgroup(attr, group);
}

int main(int argc, char** argv) {
  try {
    Check(argc == 2, "one fixed signal case required");
    const std::string mode(argv[1]);
    Check(mode == "normal" || mode == "ignored-parent" ||
              mode == "nocldwait-parent" || mode == "exec-failure" ||
              mode == "fault-mask" || mode == "fault-defaults" ||
              mode == "fault-flags" || mode == "fault-group",
          "fixed signal case");
    Run(mode);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ISOLATED_SIGNAL_CASE_FAIL=" << error.what() << '\n';
    return 1;
  }
}
