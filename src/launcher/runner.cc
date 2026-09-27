// SPDX-License-Identifier: Apache-2.0
#include "launcher/runner.hh"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>
namespace capmgr {
namespace {
class Fd {
 public:
  Fd() = default;
  explicit Fd(int value) : value_(value) {}
  ~Fd() { Close(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int get() const { return value_; }
  void Reset(int value) {
    Close();
    value_ = value;
  }
  void Close() {
    if (value_ >= 0) {
      close(value_);
      value_ = -1;
    }
  }

 private:
  int value_ = -1;
};
RunResult Failure(const Request& request, const char* reason,
                  int exit_code = -1, int signal = 0) {
  Json response = {
      {"jsonrpc", "2.0"},
      {"id", request.id},
      {"error",
       {{"code", -32090},
        {"message", "Capability transport failure"},
        {"data",
         {{"cause", reason}, {"exitCode", exit_code}, {"signal", signal}}}}}};
  return {response.dump(), exit_code, signal, false};
}
bool ValidId(const Json& id) {
  if (id.is_string()) return !id.get_ref<const std::string&>().empty();
  if (!id.is_number_integer()) return false;
  return !id.is_number_unsigned() ||
         id.get<uint64_t>() <= static_cast<uint64_t>(INT64_MAX);
}
bool SameId(const Json& a, const Json& b) {
  if (a.is_string() != b.is_string()) return false;
  return a == b;
}
bool ValidResponse(const Json& json, const Json& id) {
  if (!json.is_object() || json.value("jsonrpc", Json()) != "2.0" ||
      !json.contains("id") || !ValidId(json["id"]) || !SameId(json["id"], id) ||
      json.contains("result") == json.contains("error"))
    return false;
  if (json.contains("error")) {
    const auto& error = json["error"];
    if (!error.is_object() || !error.contains("code") ||
        !error["code"].is_number_integer() || !error.contains("message") ||
        !error["message"].is_string())
      return false;
  }
  return true;
}
}
Request ParseRequest(const std::string& text) {
  if (text.size() > 64 * 1024)
    throw Error(ErrorCode::kLimit, "Request exceeds 64 KiB");
  Json json = Json::parse(text, nullptr, false);
  if (json.is_discarded() || !json.is_object() ||
      json.value("jsonrpc", Json()) != "2.0" || !json.contains("id") ||
      !ValidId(json["id"]) || json.value("method", Json()) != "tools/call" ||
      !json.contains("params") || !json["params"].is_object() ||
      !json["params"].contains("name") || !json["params"]["name"].is_string() ||
      json["params"]["name"].get_ref<const std::string&>().empty() ||
      !json["params"].contains("arguments") ||
      !json["params"]["arguments"].is_object())
    throw Error(ErrorCode::kInvalid, "Invalid tools/call request");
  return {json["id"], json["params"]["name"].get<std::string>(), text};
}
RunResult RunCli(const std::string& executable, const Request& request,
                 const std::atomic<bool>& cancelled, RunLimits limits) {
  if (executable.empty() || executable[0] != '/' ||
      executable.find('\0') != std::string::npos)
    return Failure(request, "invalid executable");
  if (request.original.size() > 64 * 1024)
    return Failure(request, "request limit");
  if (limits.timeout.count() <= 0 || limits.output_bytes > 1024 * 1024)
    return Failure(request, "invalid limits");
  if (cancelled.load()) return Failure(request, "cancelled");
  std::array<Fd, 2> readers, writers;
  for (size_t i = 0; i < 2; ++i) {
    int pair[2];
    if (pipe2(pair, O_CLOEXEC) != 0) return Failure(request, "pipe failed");
    // Keep pipes away from stdio slots even when the parent closed stdio.
    for (int& fd : pair)
      if (fd < 3) {
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        close(fd);
        fd = moved;
      }
    readers[i].Reset(pair[0]);
    writers[i].Reset(pair[1]);
    if (pair[0] < 0 || pair[1] < 0)
      return Failure(request, "pipe duplication failed");
    if (fcntl(pair[0], F_SETFL, O_NONBLOCK) < 0)
      return Failure(request, "pipe flags failed");
  }
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  if (posix_spawn_file_actions_init(&actions) != 0)
    return Failure(request, "spawn setup failed");
  if (posix_spawnattr_init(&attributes) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    return Failure(request, "spawn setup failed");
  }
  int setup = 0;
  setup |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                            O_RDONLY, 0);
  for (int i = 0; i < 2; ++i)
    setup |= posix_spawn_file_actions_adddup2(&actions, writers[i].get(),
                                              STDOUT_FILENO + i);
  // glibc >= 2.34 on the verified host and Tizen target closes unrelated FDs.
  setup |= posix_spawn_file_actions_addclosefrom_np(&actions, 3);
  setup |= posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  setup |= posix_spawnattr_setpgroup(&attributes, 0);
  char option[] = "--json";
  char lang[] = "LANG=C";
  char path[] = "PATH=/usr/bin:/bin";
  char* argv[] = {const_cast<char*>(executable.c_str()), option,
                  const_cast<char*>(request.original.c_str()), nullptr};
  char* env[] = {lang, path, nullptr};
  pid_t pid = -1;
  int rc = setup ? EINVAL
                 : posix_spawn(&pid, executable.c_str(), &actions, &attributes,
                               argv, env);
  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);
  for (auto& fd : writers) fd.Close();
  if (rc != 0)
    return Failure(request, rc == E2BIG ? "argv limit" : "exec failed");
  // Keep the leader unreaped until group cleanup so its ID cannot be reused.
  struct Child {
    pid_t pid;
    ~Child() {
      if (pid > 0) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
        }
      }
    }
  } child{pid};
  std::array<std::string, 2> output;
  size_t total = 0;
  const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
  const char* failure = nullptr;
  bool exited = false;
  while (!exited || readers[0].get() >= 0 || readers[1].get() >= 0) {
    if (cancelled.load()) {
      failure = "cancelled";
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      failure = "timeout";
      break;
    }
    std::array<pollfd, 2> polls{};
    for (int i = 0; i < 2; ++i) polls[i] = {readers[i].get(), POLLIN, 0};
    int polled = poll(polls.data(), polls.size(), 10);
    if (polled < 0 && errno != EINTR) {
      failure = "poll failed";
      break;
    }
    for (int i = 0; i < 2 && !failure; ++i) {
      if (readers[i].get() < 0) continue;
      // One bounded read per stream per iteration preserves fairness/cancel latency.
      std::array<char, 8192> buffer{};
      ssize_t size = read(readers[i].get(), buffer.data(), buffer.size());
      if (size == 0)
        readers[i].Close();
      else if (size > 0) {
        if (static_cast<size_t>(size) > limits.output_bytes - total) {
          failure = "output limit";
          break;
        }
        output[i].append(buffer.data(), static_cast<size_t>(size));
        total += static_cast<size_t>(size);
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        failure = "read failed";
        break;
      }
    }
    siginfo_t status{};
    if (waitid(P_PID, static_cast<id_t>(pid), &status,
               WEXITED | WNOHANG | WNOWAIT) < 0 &&
        errno != EINTR) {
      failure = "wait failed";
      break;
    }
    exited = status.si_pid == pid;
  }
  // Also terminate remaining descendants after a leader exits with closed pipes.
  kill(-pid, SIGKILL);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      failure = "wait failed";
      break;
    }
  }
  child.pid = -1;
  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  int signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
  if (failure) return Failure(request, failure, exit_code, signal);
  Json parsed[2];
  bool present[2] = {false, false};
  for (int i = 0; i < 2; ++i) {
    if (output[i].find_first_not_of(" \t\r\n") == std::string::npos) continue;
    present[i] = true;
    parsed[i] = Json::parse(output[i], nullptr, false);
    if (parsed[i].is_discarded() || !ValidResponse(parsed[i], request.id))
      return Failure(request, "invalid response", exit_code, signal);
  }
  if (!present[0] && !present[1])
    return Failure(request, "no response", exit_code, signal);
  if (present[0] && present[1] && parsed[0] != parsed[1])
    return Failure(request, "conflicting responses", exit_code, signal);
  return {output[present[0] ? 0 : 1], exit_code, signal, true};
}
}
