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

// Explicit root fixture. No service, policy, mounted host tree or operational DB
// is changed. Runs NamespaceInit with fixed app_fw/System fixture context.
#include "launcher/worker_loop.hh"
#include "catalog/catalog.hh"

#include <array>

#include <fcntl.h>

#include <filesystem>
#include <iostream>

#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace capmgr;

namespace {

void Require(bool yes, const char* message) {
  if (!yes) throw std::runtime_error(message);
}

class OwnedScope {
 public:
  OwnedScope() {
    Require(mkdtemp(path_) != nullptr, "owned scope");
    std::cout << "OWNED_SCOPE=" << path_ << "\n" << std::flush;
  }

  ~OwnedScope() {
    if (!attempted_) Cleanup();
  }

  bool Cleanup(bool inject_failure = false) noexcept {
    attempted_ = true;
    std::error_code error;
    try {
      if (inject_failure)
        error = std::make_error_code(std::errc::io_error);
      else
        std::filesystem::remove_all(path_, error);
    } catch (...) {
      error = std::make_error_code(std::errc::not_enough_memory);
    }
    if (error) {
      std::cerr << "RETAINED_SCOPE=" << path_
                << " cleanup_error=" << error.value() << "\n";
      return false;
    }
    std::cout << "REMOVED_SCOPE=" << path_ << "\n";
    return true;
  }

  const char* Path() const { return path_; }

 private:
  char path_[64] = "/opt/usr/capmgr-worker-fixture-XXXXXX";
  bool attempted_ = false;
};

struct Pipe {
  int fd[2]{-1, -1};
  Pipe() { Require(pipe2(fd, O_CLOEXEC | O_NONBLOCK) == 0, "pipe"); }
  ~Pipe() {
    for (int f : fd)
      if (f >= 0) close(f);
  }

  void Send(const std::vector<uint8_t>& b) {
    Require(write(fd[1], b.data(), b.size()) == static_cast<ssize_t>(b.size()),
            "send");
  }
};

WorkerRegistry FixtureCatalog(const std::string& path) {
  // Fixture-only independent catalog read BEFORE any active child. Production
  // snapshot provenance/invalidation is an explicit integration gate.
  Catalog catalog(path, Database::Access::kReadOnly);
  auto e = catalog.GetPrivate("cli:fixture");
  Require(e.kind == Kind::kCli, "CLI registration");
  return WorkerRegistry({{e.id, e.executable}});
}

uint64_t Get(const uint8_t* p, size_t n) {
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

int Workload(const char* value) {
  auto request = Json::parse(value);
  auto mode =
      request.at("params").at("arguments").at("mode").get<std::string>();
  if (getuid() != 301 || geteuid() != 301 || getpid() != 2 || getppid() != 1)
    return 41;
  if (mode == "linger")
    for (;;) pause();
  if (mode == "flood") {
    std::array<char, 4096> bytes{};
    for (int i = 0; i < 200; ++i)
      if (write(1, bytes.data(), bytes.size()) !=
          static_cast<ssize_t>(bytes.size()))
        return 42;
  }

  if (mode == "setsid") {
    pid_t child = fork();
    if (child < 0) return 43;
    if (!child) {
      if (setsid() < 0) _exit(44);
      for (;;) pause();
    }
  }
  std::cout << Json{{"jsonrpc", "2.0"},
                    {"id", request["id"]},
                    {"result", {{"mode", mode}}}}
                   .dump()
            << std::flush;
  std::cerr << "fixture-diagnostic" << std::flush;
  return 0;
}

void Run(const std::string& db, const char* mode, WorkerFailure expected,
         bool cancel = false, bool stall = false) {
  Pipe regular, priority, reply;
  auto registry = FixtureCatalog(db);
  WorkerRuntime runtime;
  int anchor = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  Require(anchor >= 0, "creator anchor");
  WorkerLoop worker({1, regular.fd[0], priority.fd[0], reply.fd[1], anchor, 301,
                     301, "System"},
                    registry, runtime);
  Json request = {
      {"jsonrpc", "2.0"},
      {"id", "native"},
      {"method", "tools/call"},
      {"params", {{"name", "cli:fixture"}, {"arguments", {{"mode", mode}}}}}};
  regular.Send(
      EncodeWorkerCommand({WorkerCommandKind::Start, 1, 1, 1, request.dump()}));
  auto deadline = WorkerLoop::Clock::now() + std::chrono::seconds(10);
  std::vector<uint8_t> bytes;
  std::string out, err;
  bool complete = false, sent = false, accepted = false;
  WorkerFailure failure = WorkerFailure::None;
  try {
    while (!complete && WorkerLoop::Clock::now() < deadline) {
      worker.Step();
      if (cancel && accepted && !sent) {
        priority.Send(
            EncodeWorkerCommand({WorkerCommandKind::Cancel, 1, 1, 1, {}}));
        sent = true;
      }
      if (stall && worker.AdmissionOpen()) {
        usleep(1000);
        continue;
      }
      std::array<uint8_t, 8192> chunk;
      ssize_t count;
      while ((count = read(reply.fd[0], chunk.data(), chunk.size())) > 0)
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
      while (bytes.size() >= 56) {
        Require(std::string(reinterpret_cast<char*>(bytes.data()), 4) == "CWR1",
                "reply magic");
        auto size = Get(bytes.data() + 32, 4);
        Require(size <= 4096, "reply size");
        if (bytes.size() < 56 + size) break;
        auto kind = static_cast<WorkerReplyKind>(Get(bytes.data() + 6, 2));
        if (kind == WorkerReplyKind::Accepted) accepted = true;
        if (kind == WorkerReplyKind::Stdout)
          out.append(reinterpret_cast<char*>(bytes.data() + 56), size);
        if (kind == WorkerReplyKind::Stderr)
          err.append(reinterpret_cast<char*>(bytes.data() + 56), size);
        if (kind == WorkerReplyKind::Complete) {
          complete = true;
          failure = static_cast<WorkerFailure>(Get(bytes.data() + 36, 4));
        }
        bytes.erase(bytes.begin(), bytes.begin() + 56 + size);
      }
      usleep(1000);
    }
    Require(complete, "bounded completion");
    std::cout << "OBSERVED mode=" << mode
              << " cause=" << static_cast<unsigned>(failure)
              << " expected=" << static_cast<unsigned>(expected) << "\n";
    Require(failure == expected, "expected outcome");
    Require(worker.Jobs() == 0, "no owned job");
    if (expected == WorkerFailure::None) {
      Require(Json::parse(out).at("result").at("mode") == mode,
              "native stdout");
      Require(err == "fixture-diagnostic", "separate stderr");
    }
    worker.Shutdown();
    Require(worker.Quiescent(), "reaped namespace init");
    std::cout << "WORKER_PASS mode=" << mode
              << " cause=" << static_cast<unsigned>(failure) << "\n";
  } catch (...) {
    worker.Shutdown();
    auto cleanup = WorkerLoop::Clock::now() + std::chrono::seconds(5);
    while (!worker.Quiescent() && WorkerLoop::Clock::now() < cleanup) {
      worker.Step();
      usleep(1000);
    }
    // Unconfirmed cleanup remains fail-stop and cannot release frontend capacity.
    if (!worker.Quiescent()) {
      std::cerr
          << "RETAINED_SCOPE: child cleanup unconfirmed; no directory removal\n"
          << std::flush;
      std::
          terminate();  // Keep scope and fail closed, never unwind its cleanup guard.
    }
    close(anchor);
    throw;
  }

  close(anchor);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--json") return Workload(argv[2]);
  if (argc != 2 || (std::string(argv[1]) != "--run-root-fixture" &&
                    std::string(argv[1]) != "--fail-after-scope" &&
                    std::string(argv[1]) != "--fail-cleanup"))
    return 2;
  try {
    Require(geteuid() == 0, "root fixture only");
    auto* account = getpwnam("app_fw");
    Require(account && account->pw_uid == 301 && account->pw_gid == 301,
            "fixed app_fw fixture identity");
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    Require(sigaction(SIGPIPE, &action, nullptr) == 0, "SIGPIPE");
    action.sa_handler = SIG_DFL;
    Require(sigaction(SIGCHLD, &action, nullptr) == 0, "SIGCHLD");
    OwnedScope scope;
    if (std::string(argv[1]) == "--fail-after-scope")
      throw std::runtime_error("injected pre-child failure");
    std::string db = std::string(scope.Path()) + "/catalog.db";
    {
      Catalog writer(db, Database::Access::kWriter);
      Entry entry;
      entry.kind = Kind::kCli;
      entry.owner = "fixture";
      entry.key = "fixture";
      entry.name = "Fixture";
      entry.id = CanonicalId(Kind::kCli, entry.key);
      entry.executable = std::filesystem::canonical("/proc/self/exe").string();
      entry.detail = Json::object();
      writer.Stage("fixture-install", "fixture", {entry});
      writer.Finalize("fixture-install", true);
    }
    if (std::string(argv[1]) != "--fail-cleanup") {
      Run(db, "normal", WorkerFailure::None);
      Run(db, "setsid", WorkerFailure::None);
      Run(db, "linger", WorkerFailure::Cancelled, true);
      Run(db, "flood", WorkerFailure::Backpressure, false, true);
    }
    Require(scope.Cleanup(std::string(argv[1]) == "--fail-cleanup"),
            "owned scope cleanup failed");
    std::cout << "WORKER_FIXTURE_PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
