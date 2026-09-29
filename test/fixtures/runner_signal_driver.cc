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

// Ordinary isolated process only. No parent test process signal mutation.
#include "launcher/runner.hh"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::array kDefaults = {SIGCHLD, SIGPIPE, SIGTERM, SIGINT,
                                  SIGHUP,  SIGALRM, SIGUSR1, SIGUSR2};
const char* fault = nullptr;
int spawn_calls = 0, real_spawn_calls = 0, spawn_error = -1;
std::atomic<pid_t> returned_pid = -1;
pid_t observed_pgid = -1, observed_sid = -1, reaped_pid = -1;
int reaped_status = -1;
unsigned sequence = 0, group_kill_order = 0, reap_order = 0;
pid_t killed_group = 0;
int killed_signal = 0, kill_result = -1;
bool spawn_attributes_valid = false;

void Check(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}

bool Fault(const char* operation) {
  return fault && std::string_view(fault) == operation;
}

struct State {
  pid_t sid = getsid(0), pgid = getpgrp();
  sigset_t mask{};
  std::array<struct sigaction, NSIG> dispositions{};
  std::array<bool, NSIG> valid{};

  State() {
    Check(sid > 0 && pgid > 0, "own session/group query");
    Check(sigprocmask(SIG_SETMASK, nullptr, &mask) == 0, "own mask query");
    for (int signal = 1; signal < NSIG; ++signal) {
      if (sigaction(signal, nullptr, &dispositions[signal]) == 0)
        valid[signal] = true;
      else
        Check(errno == EINVAL, "own disposition query");
    }
  }

  bool Same(const State& other) const {
    if (sid != other.sid || pgid != other.pgid) return false;
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

capmgr::Request Request(int sentinel, bool hang) {
  return capmgr::ParseRequest(capmgr::Json{
      {"jsonrpc", "2.0"},
      {"id", "signal-driver"},
      {"method", "tools/call"},
      {"params",
       {{"name", "cli:test"},
        {"arguments",
         {{"mode", hang ? "hang" : "signal-state"}, {"sentinel", sentinel}}}}}}
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
    if (mode == "cancel-spawn-failure") fault = "spawn";
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
  if (mode == "exec-failure" || mode == "cancel-exec-failure")
    fixture = "/does/not/exist/capmgr-cli-fixture";
  if (mode == "invalid-executable")
    fixture =
        (std::filesystem::path(fixture).parent_path() / "invalid-worker-image")
            .string();  // Fixed configure-generated text.

  std::atomic<bool> cancelled = false;
  const bool lifecycle = mode == "timeout" || mode == "cancel";
  std::atomic<bool> notice_timeout = false;
  std::jthread notifier;
  const bool notice_case = mode.starts_with("cancel");
  if (notice_case) {
    notifier = std::jthread([&](std::stop_token stop) {
      const auto end =
          std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (!stop.stop_requested()) {
        // Positive PID is synchronized only after a real successful spawn return.
        // This is spawn-return readiness, never application-entry/liveness proof.
        if (std::chrono::steady_clock::now() >= end) {
          notice_timeout.store(true);
          return;
        }
        if (returned_pid.load(std::memory_order_acquire) > 0) {
          if (std::chrono::steady_clock::now() >= end)
            notice_timeout.store(true);
          else
            cancelled.store(true);
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }
  const auto timeout = mode == "timeout" ? std::chrono::milliseconds(150)
                                         : std::chrono::milliseconds(2000);
  const auto result = capmgr::RunCli(fixture, Request(sentinel, lifecycle),
                                     cancelled, {timeout, 1024 * 1024});
  if (notifier.joinable()) {
    notifier.request_stop();
    // jthread also requests stop/joins on exceptional unwinding.
    notifier.join();
    std::cout << "SPAWN_RETURN_NOTIFIER_JOINED=true POSITIVE_PID="
              << (returned_pid.load() > 0) << '\n';
  }
  Check(!notice_timeout.load(), "spawn-return notification deadline");
  Check(close(sentinel) == 0, "parent sentinel close");
  Check(before.Same(State()),
        "parent session/group/full mask/dispositions changed");
  EmptyChildren();
  if (mode == "ignored-parent" || mode == "nocldwait-parent") {
    Check(!result.native && !spawn_calls, "auto-reaping parent spawned child");
    Check(capmgr::Json::parse(result.response)["error"]["data"]["cause"] ==
              "unsupported parent SIGCHLD",
          "unsupported parent refusal");
  } else if (Fault("spawn")) {
    Check(spawn_error == EIO && !result.native && spawn_calls == 1 &&
              !real_spawn_calls && returned_pid.load() == -1,
          "injected parent spawn error outcome");
  } else if (fault) {
    Check(!result.native && !spawn_calls, "failed attributes spawned child");
  } else if (mode == "exec-failure" || mode == "cancel-exec-failure" ||
             mode == "invalid-executable") {
    Check(spawn_error == (mode == "invalid-executable" ? ENOEXEC : ENOENT),
          "observed real spawn exec error");
    Check(!result.native && spawn_calls == 1 && real_spawn_calls == 1 &&
              spawn_attributes_valid && returned_pid.load() == -1,
          "exec failure outcome");
  } else if (lifecycle) {
    const auto pid = returned_pid.load();
    Check(!result.native && spawn_calls == 1 && real_spawn_calls == 1 &&
              spawn_attributes_valid && pid > 0 && observed_pgid == pid &&
              observed_sid == pid && observed_sid != before.sid,
          "owned initial CLI session");
    Check(killed_group == -pid && killed_signal == SIGKILL &&
              kill_result == 0 && group_kill_order > 0 &&
              reap_order > group_kill_order && reaped_pid == pid &&
              WIFSIGNALED(reaped_status) &&
              WTERMSIG(reaped_status) == SIGKILL && result.signal == SIGKILL,
          "actual group kill before exact leader reap");
    const auto cause =
        capmgr::Json::parse(result.response)["error"]["data"]["cause"];
    Check(cause == (mode == "timeout" ? "timeout" : "cancelled"),
          "lifecycle failure cause");
    std::cout << "OBSERVED_SPAWN_RETURN_PID=" << pid
              << " PGID=" << observed_pgid << " SID=" << observed_sid
              << " APPLICATION_ENTRY=NOT_PROVED\n";
    std::cout << "GROUP_KILL_TARGET=" << killed_group
              << " SIGNAL=" << killed_signal << " RETURN=" << kill_result
              << " BEFORE_LEADER_REAP=true\n";
    std::cout << "OWNED_CLI_REAP_PID=" << reaped_pid << " SIGNAL=9\n";
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
              data.at("sid") == data.at("pid") &&
              data.at("sid").get<pid_t>() != before.sid &&
              observed_pgid == returned_pid.load() &&
              observed_sid == returned_pid.load() && spawn_attributes_valid,
          "observed new owned SID/PGID");
    std::cout << "OBSERVED_CLI_PID=" << data.at("pid")
              << " PGID=" << data.at("pgid") << " SID=" << data.at("sid")
              << " NEW_SESSION=true\n";
    std::cout << "OWNED_CLI_REAP_PID=" << reaped_pid << " STATUS=0\n";
  }
  std::cout << "PARENT_SID=" << before.sid << " PGID=" << before.pgid
            << " SESSION_GROUP_SIGNAL_STATE_UNCHANGED=true\n";
  std::cout << "ISOLATED_SESSION_CASE_PASS=" << mode
            << " SPAWN_CALLS=" << spawn_calls
            << " REAL_SPAWN_CALLS=" << real_spawn_calls
            << " SPAWN_RETURN=" << spawn_error << '\n';
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
  short flags = 0;
  spawn_attributes_valid =
      posix_spawnattr_getflags(attributes, &flags) == 0 &&
      flags == (POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK |
                POSIX_SPAWN_SETSIGDEF) &&
      !(flags & POSIX_SPAWN_SETPGROUP);
  if (!spawn_attributes_valid) return EINVAL;
  if (Fault("spawn"))
    return spawn_error =
               EIO;  // Injected parent call, not kernel setsid failure.
  ++real_spawn_calls;
  const int result =
      __real_posix_spawn(pid, path, actions, attributes, argv, env);
  spawn_error = result;
  if (!result && *pid > 0) {
    observed_pgid = getpgid(*pid);
    observed_sid = getsid(*pid);
    returned_pid.store(*pid, std::memory_order_release);
  }
  return result;
}

extern "C" pid_t __real_waitpid(pid_t, int*, int);
extern "C" pid_t __wrap_waitpid(pid_t pid, int* status, int options) {
  const pid_t result = __real_waitpid(pid, status, options);
  if (result > 0 && result == returned_pid && status) {
    reaped_pid = result;
    reaped_status = *status;
    reap_order = ++sequence;
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

extern "C" int __real_posix_spawnattr_init(posix_spawnattr_t*);
extern "C" int __wrap_posix_spawnattr_init(posix_spawnattr_t* attr) {
  return Fault("init") ? EINVAL : __real_posix_spawnattr_init(attr);
}

extern "C" int __real_kill(pid_t, int);
extern "C" int __wrap_kill(pid_t target, int signal) {
  killed_group = target;
  killed_signal = signal;
  group_kill_order = ++sequence;
  kill_result = __real_kill(target, signal);
  return kill_result;
}

int main(int argc, char** argv) {
  try {
    Check(argc == 2, "one fixed session case required");
    const std::string mode(argv[1]);
    Check(mode == "normal" || mode == "ignored-parent" ||
              mode == "nocldwait-parent" || mode == "exec-failure" ||
              mode == "invalid-executable" || mode == "fault-mask" ||
              mode == "fault-defaults" || mode == "fault-flags" ||
              mode == "fault-init" || mode == "fault-spawn" ||
              mode == "timeout" || mode == "cancel" ||
              mode == "cancel-exec-failure" || mode == "cancel-spawn-failure",
          "fixed session case");
    Run(mode);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ISOLATED_SESSION_CASE_FAIL=" << error.what() << '\n';
    return 1;
  }
}
