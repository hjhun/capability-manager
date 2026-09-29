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

// Private setup fixture, never installed as a privileged/setuid helper.
#include "launcher/namespace_init.hh"
#include "launcher/owned_children.hh"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <fcntl.h>

#include <fstream>
#include <iostream>

#include <linux/capability.h>
#include <poll.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>

#include <sstream>
#include <stdexcept>

#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <thread>

#include <unistd.h>

using namespace capmgr;
using namespace std::chrono_literals;

namespace {

void Require(bool condition, const char* description) {
  if (!condition)
    throw std::runtime_error(std::string(description) + ": " + strerror(errno));
}

std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), {}};
}

std::string ReadOutput(int fd) {
  std::string output;
  char buffer[1024];
  for (;;) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    output.append(buffer, static_cast<size_t>(n));
    if (output.size() > 16384)
      throw std::runtime_error("fixture output overflow");
  }
  return output;
}

InitMessage Receive(int fd) {
  InitMessage message{};
  size_t used = 0;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (used < sizeof(message) &&
         std::chrono::steady_clock::now() < deadline) {
    pollfd item{fd, POLLIN, 0};
    int ready = poll(&item, 1, 100);
    if (ready < 0 && errno == EINTR) continue;
    Require(ready >= 0, "poll setup");
    if (!ready) continue;
    ssize_t size = read(fd, reinterpret_cast<char*>(&message) + used,
                        sizeof(message) - used);
    Require(size > 0, "read setup message");
    used += static_cast<size_t>(size);
  }

  Require(used == sizeof(message), "setup message deadline");
  return message;
}

int Workload(const std::string& request) {
  auto* account = getpwnam("app_fw");
  if (!account || getuid() != account->pw_uid || getgid() != account->pw_gid ||
      getgroups(0, nullptr) != 0)
    return 31;
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  __user_cap_data_struct caps[2]{};
  if (syscall(SYS_capget, &header, caps) < 0 ||
      prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1)
    return 32;
  for (auto c : caps)
    if (c.effective || c.permitted || c.inheritable) return 33;
  uid_t ur, ue, us;
  gid_t gr, ge, gs;
  if (getresuid(&ur, &ue, &us) < 0 || getresgid(&gr, &ge, &gs) < 0 ||
      ur != account->pw_uid || ue != ur || us != ur || gr != account->pw_gid ||
      ge != gr || gs != gr)
    return 34;
  std::string label = ReadFile("/proc/self/attr/current");
  while (!label.empty() && (label.back() == '\n' || label.back() == '\0'))
    label.pop_back();
  if (label != "System") return 34;
  for (int cap = 0; cap < 64; ++cap) {
    int bounding = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (bounding < 0 && errno == EINVAL) break;
    if (bounding != 0 ||
        prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0) != 0)
      return 33;
  }

  std::istringstream stat(ReadFile("/proc/self/stat"));
  pid_t proc_pid = 0;
  stat >> proc_pid;
  if (proc_pid != getpid() ||
      ReadFile("/proc/1/status").find("Tgid:\t1\n") == std::string::npos)
    return 36;
  if (ReadFile("/proc/self/mountinfo").find(" shared:") != std::string::npos)
    return 35;
  if (getpid() != 2 || getppid() != 1) return 36;
  // Directory iteration owns one additional FD. Probe a high-numbered inherited
  // descriptor explicitly and count all open entries excluding the iterator.
  if (fcntl(100, F_GETFD) >= 0 || getenv("CAPMGR_FIXTURE_SECRET")) return 37;
  struct stat input{}, null_device{};
  if (fstat(0, &input) < 0 || ::stat("/dev/null", &null_device) < 0 ||
      input.st_rdev != null_device.st_rdev || !S_ISCHR(input.st_mode))
    return 39;
  size_t descriptors = 0;
  for (const auto& entry :
       std::filesystem::directory_iterator("/proc/self/fd")) {
    (void)entry;
    ++descriptors;
  }

  if (descriptors != 4) return 38;
  std::cout << "ISOLATION_OK pid=" << getpid() << " uid=" << getuid() << "\n"
            << std::flush;
  if (request == R"({"mode":"setsid"})") {
    int ready[2];
    if (pipe2(ready, O_CLOEXEC) < 0) return 40;
    pid_t child = fork();
    if (child < 0) return 41;
    if (child == 0) {
      close(ready[0]);
      if (setsid() < 0) _exit(42);
      char ok = 'S';
      if (write(ready[1], &ok, 1) != 1) _exit(43);
      close(ready[1]);
      for (;;) pause();
    }
    close(ready[1]);
    char ok = 0;
    if (read(ready[0], &ok, 1) != 1 || ok != 'S') return 44;
    close(ready[0]);
    std::cout << "SETSID_READY\n" << std::flush;
  }

  if (request == R"({"mode":"linger"})")
    for (;;) pause();
  return 0;
}

int ClosedStdinInit(void* config) {
  close(0);
  return NamespaceInit(config);
}

void Run(const std::string& executable, const char* request,
         int expected_exec_error, bool wrong_flags = false,
         bool closed_stdin = false) {
  auto* account = getpwnam("app_fw");
  Require(account && account->pw_uid && account->pw_gid, "app_fw account");
  int control[2], status[2], output[2], error[2];
  Require(pipe2(control, O_CLOEXEC) == 0 && pipe2(status, O_CLOEXEC) == 0 &&
              pipe2(output, O_CLOEXEC) == 0 && pipe2(error, O_CLOEXEC) == 0,
          "pipes");
  int leaked = open("/dev/null", O_RDONLY);
  Require(leaked >= 0, "fixture descriptor");
  Require(dup2(leaked, 100) == 100, "fixture high descriptor");
  close(leaked);
  setenv("CAPMGR_FIXTURE_SECRET", "must-not-be-inherited", 1);
  int parent_process = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  int parent_mount = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
  Require(parent_process >= 3 && parent_mount >= 3, "parent anchors");
  NamespaceInitConfig config{account->pw_uid,    account->pw_gid, "System",
                             executable.c_str(), request,         control[0],
                             status[1],          output[1],       error[1],
                             parent_process,     parent_mount};
  const auto mount_before = ReadFile("/proc/self/mountinfo");
  void* stack = mmap(nullptr, 1024 * 1024, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  Require(stack != MAP_FAILED, "stack allocation");
  OwnedChildren children;
  auto job = children.Reserve();
  pid_t init =
      clone(closed_stdin ? ClosedStdinInit : NamespaceInit,
            static_cast<char*>(stack) + 1024 * 1024,
            CLONE_NEWPID | (wrong_flags ? 0 : CLONE_NEWNS) | SIGCHLD, &config);
  int clone_error = errno;
  if (init > 0)
    children.AttachReserved(job, init);
  else
    children.AbandonUnspawned(job);
  munmap(stack, 1024 * 1024);
  errno = clone_error;
  Require(init > 0, "clone namespace");
  close(control[0]);
  close(status[1]);
  close(output[1]);
  close(error[1]);
  close(100);
  close(parent_process);
  close(parent_mount);
  try {
    auto ready = Receive(status[0]);
    if (wrong_flags)
      Require(ready.kind == InitMessageKind::Failed &&
                  ready.stage == InitStage::Mount && ready.error == EPERM,
              "reject shared mount namespace");
    else {
      if (ready.kind == InitMessageKind::Failed)
        throw std::runtime_error(
            "setup stage=" + std::to_string(static_cast<int>(ready.stage)) +
            " errno=" + std::to_string(ready.error));
      Require(ready.kind == InitMessageKind::Ready, "setup ready");
      auto own_ns = std::filesystem::read_symlink("/proc/self/ns/mnt");
      auto child_ns = std::filesystem::read_symlink(
          "/proc/" + std::to_string(init) + "/ns/mnt");
      Require(own_ns != child_ns, "private mount namespace");
      char go = 'G';
      Require(write(control[1], &go, 1) == 1, "GO");
      auto start = Receive(status[0]);
      if (expected_exec_error)
        Require(start.kind == InitMessageKind::Failed &&
                    start.stage == InitStage::Exec &&
                    start.error == expected_exec_error,
                "expected exec rejection");
      else {
        Require(start.kind == InitMessageKind::Started, "exec started");
        auto done = Receive(status[0]);
        Require(done.kind == InitMessageKind::Exited && done.code == 0 &&
                    !done.signal,
                "workload exit");
      }
    }
    ChildStatus cleanup;
    for (int i = 0; i < 500; ++i) {
      cleanup = children.Inspect(job);
      if (cleanup.state == ChildState::Complete) break;
      std::this_thread::sleep_for(2ms);
    }
    Require(cleanup.state == ChildState::Complete, "namespace cleanup");
    children.Release(job);
    auto stdout_text = ReadOutput(output[0]);
    auto stderr_text = ReadOutput(error[0]);
    Require(stderr_text.empty(), "workload stderr");
    if (wrong_flags) Require(stdout_text.empty(), "wrong flags never execute");
    if (!expected_exec_error && !wrong_flags)
      Require(stdout_text.find("ISOLATION_OK") != std::string::npos,
              "workload isolation");
    if (std::string(request).find("setsid") != std::string::npos)
      Require(stdout_text.find("SETSID_READY") != std::string::npos,
              "setsid descendant created");
    Require(ReadFile("/proc/self/mountinfo") == mount_before,
            "parent mounts unchanged");
    std::cout << "PASS " << request << "\n" << stdout_text;
  } catch (...) {
    auto cleanup = children.StopAndWait(job, 2s);
    if (cleanup.state == ChildState::Complete)
      children.Release(job);
    else {
      std::cerr << "CLEANUP_PENDING init=" << init << "\n";
      _exit(3);
    }
    close(control[1]);
    close(status[0]);
    close(output[0]);
    close(error[0]);
    throw;
  }

  close(control[1]);
  close(status[0]);
  close(output[0]);
  close(error[0]);
}

struct DelayedConfiguration {
  NamespaceInitConfig config;
  int read_gate;
};

int DelayedInit(void* input) {
  auto* delayed = static_cast<DelayedConfiguration*>(input);
  char go;
  ssize_t count;
  do {
    count = read(delayed->read_gate, &go, 1);
  } while (count < 0 && errno == EINTR);
  if (count != 1) return 125;
  close(delayed->read_gate);
  return NamespaceInit(&delayed->config);
}

void ParentDeath(const std::string& executable, int phase,
                 bool retained_writer = false) {
  // Become reaper for the namespace init when its direct creating parent dies.
  Require(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0, "subreaper fixture");
  int relay[2], capture[2];
  Require(pipe2(relay, O_CLOEXEC) == 0 && pipe2(capture, O_CLOEXEC) == 0,
          "relay pipe");
  int held_control[2], delay[2];
  Require(pipe2(held_control, O_CLOEXEC) == 0 && pipe2(delay, O_CLOEXEC) == 0,
          "retained writer fixture");
  pid_t broker = fork();
  Require(broker >= 0, "fixture broker fork");
  if (!broker) {
    close(relay[0]);
    close(capture[0]);
    close(delay[1]);
    try {
      auto* account = getpwnam("app_fw");
      Require(account, "app_fw");
      int control[2], status[2], errors[2];
      if (retained_writer) {
        control[0] = held_control[0];
        control[1] = held_control[1];
      } else {
        close(held_control[0]);
        close(held_control[1]);
        Require(pipe2(control, O_CLOEXEC) == 0, "control pipe");
      }
      Require(pipe2(status, O_CLOEXEC) == 0 && pipe2(errors, O_CLOEXEC) == 0,
              "broker pipes");
      int parent_process =
          open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      int parent_mount = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
      Require(parent_process >= 3 && parent_mount >= 3, "broker anchors");
      NamespaceInitConfig config{account->pw_uid,
                                 account->pw_gid,
                                 "System",
                                 executable.c_str(),
                                 R"({"mode":"linger"})",
                                 control[0],
                                 status[1],
                                 capture[1],
                                 errors[1],
                                 parent_process,
                                 parent_mount};
      void* stack = mmap(nullptr, 1024 * 1024, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
      Require(stack != MAP_FAILED, "broker stack");
      DelayedConfiguration delayed{config, delay[0]};
      // Force creator death BEFORE NamespaceInit/prctl in the retained-writer case.
      bool late_start = retained_writer && phase == 0;
      pid_t init = clone(late_start ? DelayedInit : NamespaceInit,
                         static_cast<char*>(stack) + 1024 * 1024,
                         CLONE_NEWPID | CLONE_NEWNS | SIGCHLD,
                         late_start ? static_cast<void*>(&delayed)
                                    : static_cast<void*>(&config));
      Require(init > 0, "broker clone");
      munmap(stack, 1024 * 1024);
      close(control[0]);
      close(status[1]);
      close(capture[1]);
      close(errors[1]);
      if (phase > 0)
        Require(Receive(status[0]).kind == InitMessageKind::Ready,
                "parent-death ready");
      if (phase > 1) {
        char go = 'G';
        Require(write(control[1], &go, 1) == 1, "parent-death GO");
        Require(Receive(status[0]).kind == InitMessageKind::Started,
                "parent-death started");
      }
      InitMessage message{InitMessageKind::Ready, InitStage::Parent, 0, init,
                          0};
      Require(write(relay[1], &message, sizeof(message)) == sizeof(message),
              "relay init PID");
      for (;;) pause();
    } catch (const std::exception& error) {
      std::cerr << error.what() << "\n";
      _exit(1);
    }
  }

  close(relay[1]);
  close(capture[1]);
  close(held_control[0]);
  close(delay[0]);
  if (!retained_writer) {
    close(held_control[1]);
    held_control[1] = -1;
  }
  OwnedChildren children;
  auto broker_id = children.Adopt(broker);
  uint64_t init_id = 0;
  try {
    auto message = Receive(relay[0]);
    Require(message.code > 0, "relay PID");
    Require(children.StopAndWait(broker_id, 2s).state == ChildState::Complete,
            "broker death");
    children.Release(broker_id);
    if (retained_writer) {
      char go = 'G';
      if (phase == 0)
        Require(write(delay[1], &go, 1) == 1,
                "release delayed init after creator death");
      // Retain GO writer without sending a byte: AwaitGo must detect dead proc
      // even though pipe HUP and retrospective PDEATHSIG cannot be relied upon.
    }
    init_id = children.Adopt(
        message.code);  // Now our adopted direct child, not caller input.
    ChildStatus result;
    for (int i = 0; i < 1000; ++i) {
      result = children.Inspect(init_id);
      if (result.state == ChildState::Complete) break;
      std::this_thread::sleep_for(2ms);
    }
    Require(result.state == ChildState::Complete,
            "PDEATHSIG or pre-GO EOF cleanup");
    children.Release(init_id);
    init_id = 0;
    auto output = ReadOutput(capture[0]);
    if (phase < 2) Require(output.empty(), "no workload before GO");
    std::cout << "PASS parent-death-phase=" << phase
              << " retained-writer=" << retained_writer << "\n";
  } catch (...) {
    if (init_id) {
      auto done = children.StopAndWait(init_id, 2s);
      if (done.state != ChildState::Complete) _exit(3);
      children.Release(init_id);
    }
    if (children.Size()) {
      auto done = children.StopAndWait(broker_id, 2s);
      if (done.state != ChildState::Complete) _exit(3);
      children.Release(broker_id);
    }
    close(relay[0]);
    close(capture[0]);
    close(delay[1]);
    throw;
  }

  close(relay[0]);
  close(capture[0]);
  close(delay[1]);
  if (held_control[1] >= 0) close(held_control[1]);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--json") return Workload(argv[2]);
  try {
    Require(geteuid() == 0, "root setup fixture only");
    signal(SIGPIPE, SIG_IGN);
    auto executable = std::filesystem::canonical("/proc/self/exe").string();
    Run(executable, R"({"mode":"normal"})", false);
    Run(executable, R"({"mode":"closed-stdin"})", 0, false, true);
    Run(executable, R"({"mode":"setsid"})", false);
    Run(executable, R"({"mode":"wrong-mount-flags"})", 0, true);
    Run("/capmgr/nonexistent-executable", R"({"mode":"missing"})", ENOENT);
    std::string denied = executable + ".denied-" + std::to_string(getpid());
    int file =
        open(denied.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    Require(file >= 0, "root-only fixture executable");
    const char script[] = "#!/bin/sh\nexit 0\n";
    Require(write(file, script, sizeof(script) - 1) == sizeof(script) - 1,
            "fixture script");
    close(file);
    try {
      Run(denied, R"({"mode":"denied"})", EACCES);
    } catch (...) {
      unlink(denied.c_str());
      throw;
    }
    Require(unlink(denied.c_str()) == 0, "fixture removal");
    for (int phase = 0; phase < 3; ++phase) ParentDeath(executable, phase);
    for (int phase = 0; phase < 2; ++phase)
      ParentDeath(executable, phase, true);
    std::cout << "NAMESPACE_FIXTURE_PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
