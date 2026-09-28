// SPDX-License-Identifier: Apache-2.0
// Explicit no-START root development fixture; not installed or run by CTest.
#include "catalog/coordinated_writer.hh"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "../fixtures/leased_bootstrap_paths.hh"
#include "../fixtures/leased_bootstrap_children.hh"
#include "launcher/worker_spawn.hh"
#include "launcher/worker_supervisor.hh"
#include "trusted_fixture.hh"

namespace {
using namespace capmgr;
using namespace std::chrono_literals;
using Clock = WorkerSupervisor::Clock;

void Check(bool okay, const char* why) {
  if (!okay) throw std::runtime_error(why);
}

struct Fd {
  int value = -1;
  explicit Fd(int fd) : value(fd) { Check(fd >= 0, "fixture open"); }
  ~Fd() {
    if (value >= 0) close(value);
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  void Close() {
    const int owned = value;
    value = -1;
    Check(owned >= 0 && !close(owned), "fixture close");
  }
};

struct Pipe {
  int fd[2]{-1, -1};
  Pipe() { Check(!pipe2(fd, O_CLOEXEC), "fixture pipe"); }
  ~Pipe() {
    for (int value : fd)
      if (value >= 0) close(value);
  }
  void Close(int side) {
    const int owned = fd[side];
    fd[side] = -1;
    Check(owned >= 0 && !close(owned), "fixture pipe close");
  }
};

// This single-threaded, no-handler-spawn fixture creates only ordinary SIGCHLD
// children and has one exclusive waiter. No event/None is NOT an empty table.
void EmptyChildren() {
  siginfo_t result{};
  errno = 0;
  const int rc = waitid(P_ALL, 0, &result, WEXITED | WNOHANG | WNOWAIT);
  Check(rc == -1 && errno == ECHILD, "expected ECHILD boundary");
}

std::set<std::string> Entries(int directory) {
  Fd scan_fd(
      openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  DIR* scan = fdopendir(scan_fd.value);
  Check(scan, "fixture directory scan");
  scan_fd.value = -1;
  std::set<std::string> result;
  int error = 0;
  try {
    for (;;) {
      errno = 0;
      const auto* entry = readdir(scan);
      if (!entry) {
        error = errno;
        break;
      }
      if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, ".."))
        result.insert(entry->d_name);
      Check(result.size() <= 4, "fixture unexpected directory entry");
    }
  } catch (...) {
    closedir(scan);
    throw;
  }
  const int closed = closedir(scan);
  Check(!error && !closed, "fixture complete directory scan");
  return result;
}

struct Scope {
  std::string path;
  fixture::leasedbootstrap::Paths paths;
  int anchor = -1;
  struct stat identity{};
  bool done = false;
  Scope() {
    fixture::TrustedPath("/opt/usr");
    char name[] = "/opt/usr/capmgr-leased-bootstrap-fixture-XXXXXX";
    Check(mkdtemp(name), "fixture scope create");
    path = name;
    std::cout << "OWNED_LEASED_BOOTSTRAP_SCOPE=" << path << std::endl;
    paths = fixture::leasedbootstrap::ParseCatalogLink(path + "/catalog");
    anchor =
        open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (anchor < 0 || fstat(anchor, &identity)) {
      if (anchor >= 0) close(anchor);
      anchor = -1;
      std::cerr << "RETAINED_LEASED_BOOTSTRAP_SCOPE=" << path << std::endl;
      throw std::runtime_error("fixture scope pin");
    }
  }
  ~Scope() {
    // Never infer child absence or delete from destructor/exception unwinding.
    if (!done)
      std::cerr << "RETAINED_LEASED_BOOTSTRAP_SCOPE=" << path << std::endl;
    if (anchor >= 0) close(anchor);
  }
  void Cleanup() {
    fixture::TrustedPath(path);
    struct stat held{}, named{};
    Check(!fstat(anchor, &held) && !lstat(path.c_str(), &named) &&
              held.st_dev == identity.st_dev &&
              held.st_ino == identity.st_ino &&
              named.st_dev == identity.st_dev &&
              named.st_ino == identity.st_ino && named.st_uid == 0 &&
              named.st_gid == 0 && (named.st_mode & 07777) == 0700,
          "fixture scope changed before cleanup");
    Check(Entries(anchor) ==
              std::set<std::string>{"catalog", "journal", "lock-parent"},
          "fixture root inventory changed");
    // Validate the COMPLETE fixed inventory before deleting anything. Trusted
    // root/exclusive unchanged paths are premises, not hostile FD-table ABA proof.
    struct Group {
      const char* directory;
      std::set<std::string> permitted;
      int fd = -1;
      std::set<std::string> observed;
    };
    std::array<Group, 3> groups{{
        {"catalog", {"catalog.db", "catalog.db-wal", "catalog.db-shm"}, -1, {}},
        {"journal", {"lock", "state.json", "state.next"}, -1, {}},
        {"lock-parent", {"generation.lock"}, -1, {}},
    }};
    try {
      for (auto& group : groups) {
        fixture::leasedbootstrap::detail::InspectDirectory(
            path + "/" + group.directory, 0700);
        group.fd = openat(anchor, group.directory,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        Check(group.fd >= 0, "fixture cleanup directory pin");
        group.observed = Entries(group.fd);
        for (const auto& file : group.observed) {
          struct stat info{};
          Check(group.permitted.contains(file) &&
                    !fstatat(group.fd, file.c_str(), &info,
                             AT_SYMLINK_NOFOLLOW) &&
                    S_ISREG(info.st_mode) && info.st_nlink == 1 &&
                    info.st_uid == 0 && info.st_gid == 0 &&
                    (info.st_mode & 07777) == 0600,
                "fixture cleanup file identity/type/mode");
          fixture::NoAcl(path + "/" + group.directory + "/" + file);
        }
      }
      for (auto& group : groups) {
        for (const auto& file : group.observed)
          Check(!unlinkat(group.fd, file.c_str(), 0),
                "fixture owned file remove");
        const int owned = group.fd;
        group.fd = -1;
        Check(!close(owned), "fixture cleanup directory close");
        Check(!unlinkat(anchor, group.directory, AT_REMOVEDIR),
              "fixture owned directory remove");
      }
    } catch (...) {
      for (auto& group : groups)
        if (group.fd >= 0) close(group.fd);
      throw;
    }
    Check(!rmdir(path.c_str()), "fixture owned scope remove");
    done = true;
    std::cout << "REMOVED_LEASED_BOOTSTRAP_SCOPE=" << path << std::endl;
  }
};

using Children = fixture::leasedbootstrap::Children;

ReadLeasePolicy Policy(const Scope& scope) {
  return {scope.paths.catalog,
          scope.paths.lock,
          0,
          0,
          0,
          0,
          0700,
          0600,
          0600,
          "User::Shell",
          "User::Shell",
          "User::Shell"};
}

void CreateFile(const std::string& path, std::string_view bytes = {}) {
  Fd file(open(path.c_str(),
               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count =
        write(file.value, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    Check(count > 0, "fixture seed write");
    offset += static_cast<size_t>(count);
  }
  Check(!fsync(file.value), "fixture seed sync");
  file.Close();
}

void Provision(Scope& scope, bool missing_shm) {
  for (const auto& path :
       {scope.paths.catalog, scope.paths.lock_parent, scope.path + "/journal"})
    Check(!mkdir(path.c_str(), 0700), "fixture private directory create");
  CreateFile(scope.paths.lock);
  {
    CoordinatedCatalogWriter writer(Policy(scope),
                                    CatalogGenerationLease::Mode::kMaintenance);
    Entry entry;
    entry.kind = Kind::kCli;
    entry.owner = "fixture";
    entry.key = "fixture";
    entry.name = "Fixture";
    entry.id = "cli:fixture";
    entry.executable = "/usr/bin/true";
    entry.detail = Json::object();
    writer.Stage("fixture", "fixture", {entry});
    writer.Finalize("fixture", true);
    Check(writer.Revision() == 1, "fixture published revision");
    writer.Close();
  }
  // Actual default ACL/SMACK/pin validation; this temporary parent's SH is
  // destroyed BEFORE any worker/probe can supply the measured exclusion.
  {
    CatalogReadLease lease(Policy(scope));
    lease.Check();
    std::cout << "PROVISIONED_REAL_METADATA=" << lease.Descriptor()
              << std::endl;
  }
  if (missing_shm) {
    auto maintenance = CatalogGenerationLease::Acquire(
        Policy(scope), CatalogGenerationLease::Mode::kMaintenance);
    const auto path = scope.paths.catalog + "/catalog.db-shm";
    struct stat before{}, after{};
    Check(!lstat(path.c_str(), &before) && S_ISREG(before.st_mode) &&
              before.st_nlink == 1 && before.st_uid == 0 &&
              before.st_gid == 0 && (before.st_mode & 07777) == 0600 &&
              !lstat(path.c_str(), &after) && before.st_dev == after.st_dev &&
              before.st_ino == after.st_ino && !unlink(path.c_str()),
          "fixture EX missing-SHM perturbation");
  }
  CreateFile(
      scope.path + "/journal/state.json",
      R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})");
  Fd journal(open((scope.path + "/journal").c_str(),
                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  Check(!fsync(journal.value), "fixture seed directory sync");
}

void Probe(Scope& scope, Children& children, size_t slot,
           const char* expected) {
  fixture::TrustedPath(CAPMGR_LEASED_BOOTSTRAP_LOCK_PROBE, true);
  posix_spawn_file_actions_t actions{};
  Check(!posix_spawn_file_actions_init(&actions), "fixture probe actions");
  int setup =
      posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  if (!setup) setup = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
  if (setup) {
    posix_spawn_file_actions_destroy(&actions);
    throw std::runtime_error("fixture probe fixed table");
  }
  const auto database = scope.paths.catalog + "/catalog.db";
  char lang[] = "LANG=C", path[] = "PATH=/usr/bin:/bin", mode[] = "generation";
  char* env[] = {lang, path, nullptr};
  char* argv[] = {const_cast<char*>(CAPMGR_LEASED_BOOTSTRAP_LOCK_PROBE),
                  const_cast<char*>(database.c_str()),
                  const_cast<char*>(scope.paths.lock.c_str()),
                  mode,
                  const_cast<char*>(expected),
                  nullptr};
  Check(!children.records.at(slot), "fixture probe slot already owned");
  children.records[slot] = children.owned.Reserve();
  pid_t pid = -1;
  const int result = posix_spawn(&pid, CAPMGR_LEASED_BOOTSTRAP_LOCK_PROBE,
                                 &actions, nullptr, argv, env);
  if (!result)
    children.owned.AttachReserved(children.records[slot], pid);
  else {
    children.owned.AbandonUnspawned(children.records[slot]);
    children.records[slot] = 0;
  }
  posix_spawn_file_actions_destroy(&actions);
  Check(!result, "fixture probe spawn");
  const auto status = children.Wait(slot, 5s);
  Check(status.exit_code == 0 && !status.signal,
        "fixture independent EX result");
  std::cout << "OWNED_GENERATION_PROBE_REAP=0 expected=" << expected
            << std::endl;
}

void Run(bool missing_shm) {
  EmptyChildren();  // BEFORE scope, SQLite or children exist.
  Scope scope;
  Children children;
  try {
    Provision(scope, missing_shm);
    {
      Fd tasks(open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
      Check(Entries(tasks.value) ==
                std::set<std::string>{std::to_string(getpid())},
            "fixture one spawning thread");
    }
    {
      Fd journal_fd(open((scope.path + "/journal").c_str(),
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      Fd catalog_fd(open(scope.paths.catalog.c_str(),
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      Fd parent(open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
      BrokerJournal journal(journal_fd.value);
      Pipe command, cancel, reply, ready;
      auto session = std::make_unique<WorkerSession>(journal, command.fd[1],
                                                     cancel.fd[1], reply.fd[0]);
      command.Close(1);
      cancel.Close(1);
      reply.Close(0);
      children.records[0] =
          SpawnFixedWorker(children.owned, journal.Generation(),
                           {command.fd[0], cancel.fd[0], reply.fd[1],
                            parent.value, catalog_fd.value, ready.fd[1]});
      command.Close(0);
      cancel.Close(0);
      reply.Close(1);
      ready.Close(1);
      WorkerSupervisor supervisor(std::move(session), children.owned,
                                  children.records[0], ready.fd[0]);
      ready.Close(0);
      bool started = false, rejected = false;
      const auto deadline = Clock::now() + 7s;
      try {
        while (!started) {
          children.WithinDeadline(
              deadline, "fixture startup deadline",
              [&] {
                children.Observe(0);
                started = supervisor.PollStartup();
              },
              [] { return Clock::now(); });
          if (!started) usleep(1000);
        }
      } catch (const std::exception& error) {
        children.DeadlineFailure(error.what());
        rejected = true;
        std::cout << "LEASED_STARTUP_REJECTED=" << error.what() << std::endl;
      }
#ifdef CAPMGR_LEASED_BOOTSTRAP_FAULT_PROBE
      const bool negative = true;
      const int rejected_exit = 126;
#else
      const bool negative = missing_shm;
      const int rejected_exit = 125;
#endif
      if (negative) {
        Check(!children.uncertain && rejected && !started &&
                  supervisor.Failed() && journal.Blocked() &&
                  journal.Reservations().empty(),
              "fixture deliberate startup rejection/session state");
        const auto status = children.Wait(0, 5s);
        Check(status.exit_code == rejected_exit && !status.signal,
              "fixture exact rejected worker reap");
        std::cout << "OWNED_REJECTED_WORKER_REAP=" << rejected_exit
                  << std::endl;
#ifdef CAPMGR_LEASED_BOOTSTRAP_FAULT_PROBE
        std::cout
            << "POST_CLOSE_FAULT_FIRED_AND_TABLE_REJECTED=source-enforced-exit126"
            << std::endl;
#else
        struct stat absent{};
        errno = 0;
        Check(lstat((scope.paths.catalog + "/catalog.db-shm").c_str(),
                    &absent) < 0 &&
                  errno == ENOENT,
              "fixture shared startup recreated missing SHM");
        std::cout << "MISSING_SHM_STILL_ABSENT" << std::endl;
#endif
      } else {
        Check(started && !rejected && supervisor.CatalogRevision() == 1,
              "fixture typed READY/revision");
        std::cout << "LEASED_TYPED_READY_REVISION=1 NO_START" << std::endl;
        Probe(scope, children, 1, "BUSY");
        bool clean = false;
        const auto stop_deadline = Clock::now() + 7s;
        children.WithinDeadline(
            stop_deadline, "fixture drain deadline",
            [&] { supervisor.PrepareStop(); }, [] { return Clock::now(); });
        while (!clean) {
          children.WithinDeadline(
              stop_deadline, "fixture drain deadline",
              [&] {
                children.Observe(0);
                supervisor.Step();
                clean = supervisor.ConfirmNormalExit();
                if (clean) children.records[0] = 0;  // Reaped/released.
              },
              [] { return Clock::now(); });
          if (!clean) usleep(1000);
        }
        Check(clean && !journal.Blocked() && journal.Reservations().empty(),
              "fixture clean stop/owned reap/durable empty reservations");
        std::cout
            << "OWNED_NORMAL_WORKER_REAP=0 CONFIRM_NORMAL_EXIT NO_RESERVATIONS"
            << std::endl;
      }
    }
    Probe(scope, children, 2, "OK");
    Check(fixture::leasedbootstrap::CleanupScope(children, EmptyChildren,
                                                 [&] { scope.Cleanup(); }),
          "fixture retained child records");
    std::cout << "LEASED_BOOTSTRAP_CASE_PASS="
#ifdef CAPMGR_LEASED_BOOTSTRAP_FAULT_PROBE
              << "post-close-table-fault"
#else
              << (missing_shm ? "missing-shm" : "normal-no-start")
#endif
              << std::endl;
  } catch (...) {
    const auto failure = std::current_exception();
    children.CleanupKnown();
    try {
      fixture::leasedbootstrap::CleanupScope(children, EmptyChildren,
                                             [&] { scope.Cleanup(); });
    } catch (...) {
      // Scope destructor reports retention. Original functional error stands.
    }
    std::rethrow_exception(failure);
  }
}
}  // namespace

int main(int argc, char**) {
  try {
    Check(argc == 1 && getuid() == 0 && geteuid() == 0 && getgid() == 0 &&
              getegid() == 0,
          "root fixed no-argument fixture only");
    umask(0077);
    struct sigaction child{};
    child.sa_handler = SIG_DFL;
    sigemptyset(&child.sa_mask);
    Check(!sigaction(SIGCHLD, &child, nullptr), "fixture SIGCHLD reset");
    struct sigaction pipe{};
    pipe.sa_handler =
        SIG_IGN;  // Required by the existing WorkerSession contract.
    sigemptyset(&pipe.sa_mask);
    Check(!sigaction(SIGPIPE, &pipe, nullptr), "fixture session SIGPIPE setup");
    fixture::TrustedPath(CAPMGR_WORKER_IMAGE, true);
#ifdef CAPMGR_LEASED_BOOTSTRAP_FAULT_PROBE
    Run(false);
#else
    Run(false);
    Run(true);
#endif
    std::cout << "LEASED_BOOTSTRAP_NO_START_FIXTURE_PASS" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "LEASED_BOOTSTRAP_FIXTURE_FAIL=" << error.what() << std::endl;
    return 1;
  }
}
