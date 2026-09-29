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

// Minimal build-only launcher: no TIDL/GLib/Cynara/dlog startup dependencies.
#include "trusted_fixture.hh"
#include "../fixtures/read_policy_context.hh"
#include "../fixtures/read_policy_code_image.hh"
#include "launcher/owned_children.hh"

#include <dlfcn.h>
#include <signal.h>
#include <spawn.h>
#include <sys/xattr.h>

#include <thread>

#ifndef CAPMGR_REAL_POLICY_NATIVE_IMAGE
#error "The fixed native fixture image must be selected at build time"
#endif
extern char** environ;
using namespace capmgr;
using namespace capmgr::fixture::realpolicy;
using namespace std::chrono_literals;

namespace {

const char* stage = "startup";
struct Scope {
  char path[64] = "/opt/usr/capmgr-real-policy-XXXXXX";
  int anchor = -1;
  struct stat identity{};
  bool attempted = false;
  bool retained = false;
  Scope() {
    fixture::TrustedPath("/opt/usr");
    Check(mkdtemp(path), "scope");
    std::cout << "OWNED_SCOPE=" << path << std::endl;
    anchor = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (anchor < 0 || fstat(anchor, &identity)) {
      if (anchor >= 0) close(anchor);
      std::cerr << "RETAINED_SCOPE=" << path << std::endl;
      throw std::runtime_error("scope anchor");
    }
  }

  ~Scope() {
    if (retained) std::cerr << "RETAINED_SCOPE=" << path << std::endl;
    if (!attempted && !retained) {
      try {
        Cleanup();
      } catch (...) {
      }
    }
    if (anchor >= 0) close(anchor);
  }

  void Cleanup() {
    Check(!retained, "scope ownership/endpoint uncertainty");
    if (attempted) return;
    attempted = true;
    try {
      fixture::TrustedPath(path);
      struct stat current{}, held{};
      Check(!lstat(path, &current) && !fstat(anchor, &held) &&
                current.st_dev == identity.st_dev &&
                current.st_ino == identity.st_ino &&
                held.st_dev == identity.st_dev &&
                held.st_ino == identity.st_ino &&
                (current.st_mode & 07777) == 0700,
            "scope changed");
      std::error_code error;
      std::filesystem::remove_all(path, error);
      Check(!error, "scope deletion failed");
      std::cout << "REMOVED_SCOPE=" << path << std::endl;
    } catch (...) {
      std::cerr << "RETAINED_SCOPE=" << path << std::endl;
      throw;
    }
  }
};

struct Child {
  OwnedChildren owned{1};
  uint64_t token = 0;
  ~Child() {
    if (token) {
      auto status = owned.StopAndWait(token, 2s);
      if (status.state != ChildState::Complete) {
        std::cerr << "RETAINED_SCOPE child cleanup unconfirmed\n" << std::flush;
        std::terminate();
      }
      owned.Release(token);
    }
  }
  pid_t pid = -1;
  ChildStatus result;
  void Start(std::vector<std::string> args) {
    struct StdioCopies {
      std::array<int, 3> fds{-1, -1, -1};
      ~StdioCopies() {
        for (int fd : fds)
          if (fd >= 0) close(fd);
      }
    } sources;
    for (int i = 0; i != 3; ++i) {
      sources.fds[i] = fcntl(i, F_DUPFD_CLOEXEC, 3);
      Check(sources.fds[i] >= 3, "private stdio copy");
    }
    std::vector<char*> pointers;
    for (auto& arg : args) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    Check(!posix_spawn_file_actions_init(&actions), "spawn init");
    for (int i = 0; i != 3; ++i) {
      if (posix_spawn_file_actions_adddup2(&actions, sources.fds[i], i)) {
        posix_spawn_file_actions_destroy(&actions);
        throw std::runtime_error("stdio mapping");
      }
    }
    if (posix_spawn_file_actions_addclosefrom_np(&actions, 3)) {
      posix_spawn_file_actions_destroy(&actions);
      throw std::runtime_error("closefrom");
    }
    auto id = owned.Reserve();
    pid_t child = -1;
    int result = posix_spawn(&child, args[0].c_str(), &actions, nullptr,
                             pointers.data(), environ);
    if (result == 0)
      owned.AttachReserved(id, child);
    else
      owned.AbandonUnspawned(id);
    posix_spawn_file_actions_destroy(&actions);
    Check(result == 0, "spawn");
    token = id;
    pid = child;
  }

  void Wait(std::chrono::seconds budget = 20s) {
    auto end = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < end) {
      auto status = owned.Inspect(token);
      if (status.state == ChildState::Complete) {
        owned.Release(token);
        token = 0;
        result = status;
        Check(status.signal == 0, "child signal");
        return;
      }
      std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("child deadline");
  }
};

CodeImage OpenCodeImage() {
  fixture::TrustedPath(CAPMGR_REAL_POLICY_NATIVE_IMAGE, true);
  int fd =
      open(CAPMGR_REAL_POLICY_NATIVE_IMAGE, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  try {
    struct stat info{};
    Check(fd >= 3 && !fstat(fd, &info) && S_ISREG(info.st_mode) &&
              info.st_uid == 0 && info.st_gid == 0 && info.st_nlink == 1 &&
              (info.st_mode & 07777) == 0755,
          "unsafe fixed code image");
    for (const char* name :
         {"system.posix_acl_access", "system.posix_acl_default",
          "security.capability"}) {
      errno = 0;
      auto size = fgetxattr(fd, name, nullptr, 0);
      Check(size < 0 && (errno == ENODATA || errno == EOPNOTSUPP),
            "code image ACL/cap metadata");
    }
    int transferred = fd;
    fd = -1;
    return CodeImage(transferred, info);
  } catch (...) {
    if (fd >= 0) close(fd);
    throw;
  }
}

void WriteRecord(const std::string& name, const Json& value) {
  auto bytes = value.dump() + "\n";
  Check(bytes.size() <= 4096, "fixture record limit");
  int fd = open(name.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  Check(fd >= 0, "fixture record creation");
  size_t offset = 0;
  while (offset != bytes.size()) {
    ssize_t n = write(fd, bytes.data() + offset, bytes.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      close(fd);
      throw std::runtime_error("fixture record write");
    }
    offset += static_cast<size_t>(n);
  }

  int mode = fchmod(fd, 0600);
  int closed = close(fd);
  Check(!mode && !closed, "fixture record mode/close");
}

void ReportContext(FixedRole role) {
  std::cout << "REAL_GATE_VERIFIED_CONTEXT role=" << role.name
            << " uid=" << role.uid << " gid=" << role.uid
            << " groups=" << (role.platform_group ? "[10212]" : "[]")
            << " caps=all-zero bounding=0 ambient=0 NNP=1 label=" << TaskLabel()
            << std::endl;
}

void Run(bool context_only, bool platform_group = false) {
  if (platform_group) {
    stage = "fixed-platform-group-prerequisite";
    PlatformGroupPreflight();
    // NSS/image startup is a premise, not permission to inherit its endpoints.
    // Refuse any changed task/FD table before creating a scope or spawning.
    OwnInitialTable(stage);
  }
  stage = "fixture-scope";
  Scope scope;
  unsigned positives = 0;
  const std::span<const FixedRole> roles =
      platform_group ? std::span<const FixedRole>(kPlatformRoles)
                     : std::span<const FixedRole>(kRoles);
  for (const auto& role : roles) {
    auto endpoint = "d::org.capmgr.realpolicy." + std::to_string(getpid()) +
                    "." + role.name;
    const auto endpoint_path =
        "/run/aul/rpcport/." + endpoint + "::CapabilityManager";
    Child server, client;
    if (!context_only) {
      server.Start({Self(), platform_group ? "platform-server" : "server",
                    scope.path, endpoint, role.name});
      // Failures before the explicit drain/endpoint check cannot permit scope
      // deletion merely because a destructor subsequently killed the server.
      scope.retained = true;
      auto end = std::chrono::steady_clock::now() + 5s;
      auto ready = std::string(scope.path) + "/ready-" + role.name;
      while (!std::filesystem::exists(ready) &&
             std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(10ms);
      Check(std::filesystem::exists(ready), "server readiness deadline");
    }
    auto stop = [&] {
      if (context_only) return;
      WriteRecord(std::string(scope.path) + "/stop-" + role.name,
                  {{"stage", "stop"}});
      try {
        server.Wait(5s);
      } catch (...) {
        scope.retained = true;
        std::cerr << "RETAINED_SCOPE=" << scope.path
                  << " server drain unproved\n";
        throw;
      }
      if (std::filesystem::exists(endpoint_path)) {
        scope.retained = true;
        std::cerr << "RETAINED_ENDPOINT=" << endpoint_path << std::endl;
        throw std::runtime_error("server endpoint retained");
      }
      Check(server.result.exit_code == 0, "server exit failure");
      scope.retained = false;
    };
    try {
      const char* kind = platform_group ? (context_only ? "platform-context"
                                                        : "platform-client")
                                        : (context_only ? "context" : "client");
      client.Start({Self(), kind, scope.path, endpoint, role.name});
      client.Wait(15s);
      stop();
    } catch (...) {
      auto failure = std::current_exception();
      if (server.token) {
        try {
          stop();
        } catch (...) {
          scope.retained = true;
        }
      }
      std::rethrow_exception(failure);
    }
    if (context_only) {
      Check(client.result.exit_code == 0, "context child failure");
      continue;
    }
    Check(client.result.exit_code == 10 || client.result.exit_code == 20,
          "client setup/teardown failure");
    auto record = std::string(scope.path) + "/body-" + role.name;
    if (client.result.exit_code == 10) {
      std::ifstream input(record);
      Json body = Json::parse(input);
      Check(body == Json{{"stage", "server-cancel-body"},
                         {"role", role.name},
                         {"pid", client.pid},
                         {"uid", role.uid},
                         {"gid", role.uid},
                         {"socket_label", role.label},
                         {"token", std::string(role.name) + ":" +
                                       std::to_string(client.pid)},
                         {"cancel_calls", 1},
                         {"other_calls", 0}},
            "server/client correlation");
      if (role.uid == 301) ++positives;
      std::cout << "REAL_GATE_CORRELATED_POSITIVE role=" << role.name
                << " owned_client_pid=" << client.pid << std::endl;
    } else {
      Check(!std::filesystem::exists(record),
            "method body without confirmed reply");
      std::cout << "REAL_GATE_AVAILABILITY_UNPROVED role=" << role.name
                << std::endl;
    }
  }

  scope.Cleanup();
  if (context_only) {
    std::cout << (platform_group
                      ? "REAL_GATE_PLATFORM_GROUP_CONTEXT_ONLY_PASS\n"
                      : "REAL_GATE_CONTEXT_ONLY_PASS\n");
    return;
  }

  if (platform_group) {
    std::cout << "REAL_GATE_PLATFORM_GROUP_DIAGNOSTIC_COMPLETE positives="
              << positives << " operational_policy_changes=0\n";
    std::cout << "REAL_GATE_ORIGINAL_DIFFERENT_SUBJECT_MATRIX_BLOCKED\n";
    if (positives == 1)
      std::cout
          << "REAL_GATE_PLATFORM_GROUP_TUPLE_AVAILABLE_FOR_SEPARATE_OBJECT_"
             "REVIEW\n";
    else
      std::cout << "REAL_GATE_PLATFORM_GROUP_NEXT_OBJECT_MATRIX_BLOCKED\n";
    return;
  }
  std::cout << "REAL_GATE_DIAGNOSTIC_COMPLETE candidate_positives=" << positives
            << " operational_policy_changes=0\n";
  if (positives < 2)
    std::cout
        << "REAL_GATE_NEXT_MATRIX_BLOCKED image/availability prerequisite\n";
  else
    std::cout
        << "REAL_GATE_TWO_CONTEXTS_AVAILABLE_FOR_SEPARATE_MATRIX_REVIEW\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    uid_t real, effective, saved;
    gid_t rgroup, egroup, sgroup;
    Check(!getresuid(&real, &effective, &saved) && !real && !effective &&
              !saved && !getresgid(&rgroup, &egroup, &sgroup) && !rgroup &&
              !egroup && !sgroup,
          "root development fixture IDs");
    fixture::TrustedPath(Self(), true);
    OwnInitialTable(stage);
    struct sigaction action{};
    Check(!sigaction(SIGCHLD, nullptr, &action) &&
              action.sa_handler == SIG_DFL && !(action.sa_flags & SA_NOCLDWAIT),
          "exclusive default SIGCHLD");
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--contexts-only")) {
      stage = "fixture-scope";
      Run(argc == 2);
      return 0;
    }
    if (argc == 2 &&
        (std::string(argv[1]) == "--platform-group-contexts-only" ||
         std::string(argv[1]) == "--platform-group-rpc")) {
      Run(std::string(argv[1]) == "--platform-group-contexts-only", true);
      return 0;
    }
    stage = "child-arguments";
    Check(argc == 5, "fixed fixture arguments");
    std::string kind = argv[1], root = argv[2], endpoint = argv[3];
    const bool platform_group = kind == "platform-server" ||
                                kind == "platform-client" ||
                                kind == "platform-context";
    const auto role = Role(argv[4], platform_group);
    fixture::TrustedPath(root);
    Check(root.starts_with("/opt/usr/capmgr-real-policy-") &&
              endpoint == "d::org.capmgr.realpolicy." +
                              std::to_string(getppid()) + "." + role.name,
          "fixed parent fixture arguments");
    Check(kind == "server" || kind == "client" || kind == "context" ||
              platform_group,
          "fixed child kind");
    if (kind == "context" || kind == "platform-context") {
      stage = "client-own-context";
      Drop(role.label, role.uid, role.platform_group);
      ReportContext(role);
      return 0;
    }
    auto code = OpenCodeImage();
    if (kind == "client" || kind == "platform-client") {
      stage = "client-own-context";
      Drop(role.label, role.uid, role.platform_group);
      ReportContext(role);
    }
    return code.Invoke(
        [&](int fd) {
          OwnInitialTable(stage, fd);
          if (kind == "client" || kind == "platform-client")
            VerifyContext(role.label, role.uid, role.platform_group);
          stage = "fixed-module-load";
        },
        kind.c_str(), root.c_str(), endpoint.c_str(), role.name);
  } catch (const std::exception& error) {
    std::cerr << "REAL_GATE_FIXTURE_FAIL stage=" << stage << " " << error.what()
              << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "REAL_GATE_FIXTURE_FAIL stage=" << stage
              << " native exception\n";
    return 1;
  }
}
