// SPDX-License-Identifier: Apache-2.0
// Fork-only root fixture: no namespace setup, workload, mount or policy writes.
#include "launcher/worker_bootstrap.hh"
#include "launcher/worker_loop.hh"
#include "../fixtures/bootstrap_policy.hh"
#include "trusted_fixture.hh"
#include <atomic>
#include <thread>
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace capmgr;
namespace {
void Check(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}
int Child(const std::string& mode, const std::array<int, 6>& sources) {
  try {
    for (int i = 0; i < 3; ++i) {
      int fd = open("/dev/null", i ? O_WRONLY : O_RDONLY);
      Check(fd >= 0 && dup2(fd, i) >= 0, "null stdio");
      if (fd > 2) close(fd);
    }
    for (size_t i = 0; i < sources.size(); ++i)
      Check(dup2(sources[i], static_cast<int>(3 + i)) >= 0, "fixed mapping");
    closefrom(9);
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    Check(!sigaction(SIGCHLD, &action, nullptr), "child disposition");
    action.sa_handler = SIG_IGN;
    Check(!sigaction(SIGPIPE, &action, nullptr), "safe diagnostic SIGPIPE");
    sigset_t mask;
    sigemptyset(&mask);
    Check(!sigprocmask(SIG_SETMASK, &mask, nullptr), "signal mask");
    WorkerInitialNamespaces namespaces;
    fixture::Reduce(mode != "missing-nnp");
    WorkerBootstrapPolicy policy{fixture::kCaps, {}, "User::Shell", namespaces};
    bool rejected = false;
    std::atomic<bool> stop = false;
    std::thread second;
    try {
      auto witnesses = namespaces.Descriptors();
      if (mode == "stale-witness") close(witnesses[0]);
      if (mode == "wrong-witness") {
        Check(dup2(witnesses[1], witnesses[0]) == witnesses[0] &&
                  !fcntl(witnesses[0], F_SETFD, FD_CLOEXEC),
              "wrong namespace type");
      }
      if (mode == "substituted-witness") {
        int wrong = open("/dev/null", O_RDONLY | O_CLOEXEC);
        Check(wrong >= 0 && dup2(wrong, witnesses[0]) == witnesses[0] &&
                  !fcntl(witnesses[0], F_SETFD, FD_CLOEXEC),
              "substituted namespace handle");
        close(wrong);
      }
      if (mode == "bad-caps") policy.capabilities ^= 1ULL << CAP_KILL;
      if (mode == "bad-label") policy.smack_label = "System";
      if (mode == "bad-groups") policy.supplementary_groups = {0};
      if (mode == "extra-fd")
        Check(open("/dev/null", O_RDONLY | O_CLOEXEC) >= 9, "extra FD");
      if (mode == "missing-fd") close(8);
      if (mode == "aliased-pipe") Check(dup2(5, 8) == 8, "alias");
      if (mode == "wrong-parent") {
        int self = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        Check(self >= 9 && dup2(self, 6) == 6, "wrong parent");
        close(self);
      }
      if (mode == "extra-task")
        second = std::thread([&] {
          while (!stop.load()) usleep(1000);
        });
      ValidateWorkerBootstrap(policy);
      if (mode == "late-witness") {
        Check(dup2(witnesses[1], witnesses[0]) == witnesses[0] &&
                  !fcntl(witnesses[0], F_SETFD, FD_CLOEXEC),
              "late namespace substitution");
      }
      if (mode == "late-task")
        second = std::thread([&] {
          while (!stop.load()) usleep(1000);
        });
      if (mode == "late-fd")
        Check(open("/dev/null", O_RDONLY | O_CLOEXEC) >= 9, "post-load FD");
      if (mode == "lost-cloexec") Check(!fcntl(7, F_SETFD, 0), "clear CLOEXEC");
      if (mode == "owned-loop" || mode == "late-loop-fd") {
        WorkerRuntime runtime;
        WorkerLoop loop({1, 3, 4, 5, 6, 301, 301, "System"}, WorkerRegistry({}),
                        runtime);
        if (mode == "late-loop-fd")
          Check(open("/dev/null", O_RDONLY | O_CLOEXEC) >= 9, "post-loop FD");
        FinishWorkerBootstrap(policy, loop.StartupDescriptors());
        loop.Shutdown();
      } else
        FinishWorkerBootstrap(policy);
      for (int fd : witnesses) {
        errno = 0;
        Check(fcntl(fd, F_GETFD) == -1 && errno == EBADF,
              "witness must close before READY");
      }
    } catch (const std::exception& error) {
      rejected = true;
      auto message = std::string_view(error.what());
      auto ignored =
          write(8, message.data(), std::min(message.size(), size_t(511)));
      (void)ignored;
    } catch (...) {
      rejected = true;
    }
    stop = true;
    if (second.joinable()) second.join();
    bool valid = mode == "valid" || mode == "owned-loop";
    return rejected != valid ? 0 : 61;
  } catch (...) {
    return 62;
  }
}
void Run(const std::string& mode) {
  int pipes[4][2];
  for (auto& p : pipes) Check(!pipe2(p, O_CLOEXEC), "pipe");
  int parent = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC),
      catalog = open("/opt/usr", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  std::array<int, 6> original{pipes[0][0], pipes[1][0], pipes[2][1],
                              parent,      catalog,     pipes[3][1]},
      sources{};
  for (size_t i = 0; i < sources.size(); ++i) {
    sources[i] = fcntl(original[i], F_DUPFD_CLOEXEC, 9);
    Check(sources[i] >= 9, "source duplication");
  }
  OwnedChildren owned(1);
  auto token = owned.Reserve();
  pid_t child = fork();
  if (child > 0)
    owned.AttachReserved(token, child);
  else if (child < 0)
    owned.AbandonUnspawned(token);
  Check(child >= 0, "fork");
  if (!child) _exit(Child(mode, sources));
  for (int fd : sources) close(fd);
  for (auto& p : pipes)
    for (int fd : p)
      if (fd != pipes[3][0]) close(fd);
  close(parent);
  close(catalog);
  ChildStatus status;
  bool exited = false;
  for (int i = 0; i < 5000; ++i) {
    status = owned.Inspect(token);
    if (status.state == ChildState::Complete) {
      exited = true;
      break;
    }
    usleep(1000);
  }
  if (!exited) {
    status = owned.StopAndWait(token, std::chrono::seconds(5));
    if (status.state != ChildState::Complete) {
      std::cerr << "CONTEXT_CHILD_CLEANUP_UNCONFIRMED" << std::endl;
      std::terminate();
    }
    owned.Release(token);
    throw std::runtime_error("context fixture deadline");
  }
  owned.Release(token);
  char diagnostic[512]{};
  int flags = fcntl(pipes[3][0], F_GETFL);
  Check(flags >= 0 && !fcntl(pipes[3][0], F_SETFL, flags | O_NONBLOCK),
        "diagnostic pipe");
  ssize_t size = read(pipes[3][0], diagnostic, sizeof(diagnostic) - 1);
  close(pipes[3][0]);
  if (status.exit_code || status.signal)
    std::cerr << "CONTEXT_CHILD_FAIL mode=" << mode
              << " exit=" << status.exit_code << " signal=" << status.signal
              << " reason=" << (size > 0 ? diagnostic : "") << std::endl;
  Check(status.exit_code == 0 && !status.signal, "context child verdict");
  std::cout << "CONTEXT_CASE_PASS mode=" << mode << std::endl;
}
}
int main(int argc, char** argv) {
  if ((argc != 2 && argc != 3) || std::string(argv[1]) != "--run-root-fixture")
    return 2;
  try {
    Check(getuid() == 0 && geteuid() == 0, "root fixture only");
    struct sigaction disposition{};
    disposition.sa_handler = SIG_DFL;
    Check(!sigaction(SIGCHLD, &disposition, nullptr), "exclusive child reaper");
    fixture::TrustedPath(
        std::filesystem::read_symlink("/proc/self/exe").string(), true);
    fixture::TrustedPath("/opt/usr");
    bool selected = false;
    for (const char* mode :
         {"valid", "bad-caps", "bad-label", "bad-groups", "missing-nnp",
          "extra-fd", "missing-fd", "aliased-pipe", "wrong-parent",
          "extra-task", "late-task", "late-fd", "lost-cloexec", "owned-loop",
          "late-loop-fd", "stale-witness", "wrong-witness",
          "substituted-witness", "late-witness"}) {
      if (argc == 3 && std::string(argv[2]) != mode) continue;
      selected = true;
      Run(mode);
    }
    Check(selected, "unknown context fixture case");
    std::cout << "BOOTSTRAP_CONTEXT_PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "BOOTSTRAP_CONTEXT_FAIL " << e.what() << std::endl;
    return 1;
  }
}
