// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_bootstrap.hh"
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/magic.h>
#include <signal.h>
#include <stdexcept>
#include <system_error>
#include <string_view>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <unistd.h>
namespace capmgr {
namespace {
void Check(bool ok, const char* why) {
  if (!ok) {
    int error = errno;
    throw std::system_error(error ? error : EINVAL, std::generic_category(),
                            why);
  }
}
struct Fd {
  int value;
  ~Fd() {
    if (value >= 0) close(value);
  }
};
std::string ReadAt(int directory, const char* name, size_t bound) {
  Fd fd{openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
  Check(fd.value >= 0, "bootstrap proc field open");
  std::string data(bound, '\0');
  ssize_t size;
  do {
    size = read(fd.value, data.data(), data.size());
  } while (size < 0 && errno == EINTR);
  Check(size > 0 && static_cast<size_t>(size) < bound,
        "bootstrap proc field bound");
  data.resize(static_cast<size_t>(size));
  return data;
}
uint64_t Number(std::string_view value) {
  uint64_t n = 0;
  auto result = std::from_chars(value.data(), value.data() + value.size(), n);
  Check(result.ec == std::errc{} && result.ptr == value.data() + value.size(),
        "bootstrap proc number");
  return n;
}
struct Process {
  uint64_t pid, start;
};
Process Stat(int directory) {
  auto text = ReadAt(directory, "stat", 4096);
  size_t first = text.find(' '), end = text.rfind(')');
  Check(first != std::string::npos && end != std::string::npos &&
            end + 4 < text.size() && text[first + 1] == '(' &&
            text[end + 1] == ' ' && text[end + 3] == ' ',
        "bootstrap proc stat shape");
  char state = text[end + 2];
  Check(std::string_view("RSDTtIWP").find(state) != std::string_view::npos,
        "bootstrap creator dead/state");
  std::string_view rest(text.data() + end + 4, text.size() - end - 4);
  // Remaining fields start with ppid (field4); starttime is field22.
  for (int field = 4; field < 22; ++field) {
    size_t space = rest.find(' ');
    Check(space != std::string_view::npos, "bootstrap short proc stat");
    rest.remove_prefix(space + 1);
  }
  size_t space = rest.find(' ');
  Check(space != std::string_view::npos, "bootstrap missing starttime");
  return {Number(std::string_view(text).substr(0, first)),
          Number(rest.substr(0, space))};
}
void SameNamespace(int parent, int self, const char* name) {
  Fd a{openat(parent, name, O_RDONLY | O_CLOEXEC)},
      b{openat(self, name, O_RDONLY | O_CLOEXEC)};
  struct stat x{}, y{};
  struct statfs fs{};
  Check(a.value >= 0 && b.value >= 0 && !fstat(a.value, &x) &&
            !fstat(b.value, &y) && !fstatfs(a.value, &fs) &&
            fs.f_type == NSFS_MAGIC && x.st_dev == y.st_dev &&
            x.st_ino == y.st_ino,
        "bootstrap namespace mismatch");
}
void Parent() {
  pid_t parent = getppid();
  Check(parent > 1, "bootstrap creator reparented");
  Fd self{open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  struct stat supplied{}, own{};
  struct statfs fs{}, self_fs{};
  Check(self.value >= 0 && !fstat(6, &supplied) && S_ISDIR(supplied.st_mode) &&
            !fstat(self.value, &own) && !fstatfs(6, &fs) &&
            !fstatfs(self.value, &self_fs) && fs.f_type == PROC_SUPER_MAGIC &&
            self_fs.f_type == PROC_SUPER_MAGIC && supplied.st_dev == own.st_dev,
        "bootstrap creator procfs");
  Process before = Stat(6), ours = Stat(self.value);
  Check(before.pid == static_cast<uint64_t>(parent) &&
            ours.pid == static_cast<uint64_t>(getpid()),
        "bootstrap creator PID mismatch");
  // Proves a task directory rather than procfs root/task grouping directories.
  Fd leader{openat(6, "task", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
  Check(leader.value >= 0, "bootstrap creator task directory");
  for (const char* name : {"ns/pid", "ns/mnt"})
    SameNamespace(6, self.value, name);
  Process after = Stat(6);
  Check(after.pid == before.pid && after.start == before.start &&
            getppid() == parent,
        "bootstrap creator changed");
}
void Context(const WorkerBootstrapPolicy& policy) {
  uid_t r, e, s;
  gid_t gr, ge, gs;
  Check(!getresuid(&r, &e, &s) && !getresgid(&gr, &ge, &gs) && !r && !e && !s &&
            !gr && !ge && !gs,
        "bootstrap root IDs");
  Check(policy.supplementary_groups.size() <= 64 &&
            !policy.smack_label.empty() && policy.smack_label.size() < 256 &&
            policy.smack_label.find('\0') == std::string::npos,
        "bootstrap image policy");
  int count = getgroups(0, nullptr);
  Check(count >= 0 &&
            static_cast<size_t>(count) == policy.supplementary_groups.size(),
        "bootstrap group count");
  std::vector<gid_t> actual(static_cast<size_t>(count)),
      expected = policy.supplementary_groups;
  Check(!count || getgroups(count, actual.data()) == count, "bootstrap groups");
  std::sort(actual.begin(), actual.end());
  std::sort(expected.begin(), expected.end());
  Check(actual == expected, "bootstrap group mismatch");
  auto label = ReadAt(AT_FDCWD, "/proc/self/attr/current", 256);
  while (!label.empty() && (label.back() == '\n' || label.back() == '\0'))
    label.pop_back();
  Check(label == policy.smack_label, "bootstrap SMACK label");
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  __user_cap_data_struct caps[2]{};
  Check(!syscall(SYS_capget, &header, caps), "bootstrap capabilities");
  uint64_t permitted = caps[0].permitted | (uint64_t(caps[1].permitted) << 32),
           effective = caps[0].effective | (uint64_t(caps[1].effective) << 32);
  Check(permitted == policy.capabilities && effective == policy.capabilities &&
            !caps[0].inheritable && !caps[1].inheritable,
        "bootstrap capability set mismatch");
  uint64_t bounding = 0;
  for (int cap = 0; cap < 64; ++cap) {
    int bit = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (bit < 0 && errno == EINVAL) break;
    Check(bit >= 0 &&
              prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0) == 0,
          "bootstrap ambient/bounding read");
    if (bit) bounding |= uint64_t(1) << cap;
  }
  Check(bounding == policy.capabilities &&
            prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1 &&
            prctl(PR_GET_SECUREBITS, 0, 0, 0, 0) == 0,
        "bootstrap capability policy");
  struct sigaction action{};
  Check(!sigaction(SIGCHLD, nullptr, &action) && action.sa_handler == SIG_DFL &&
            !(action.sa_flags & SA_NOCLDWAIT),
        "bootstrap reaper disposition");
  sigset_t mask;
  Check(!sigprocmask(SIG_SETMASK, nullptr, &mask), "bootstrap signal mask");
  for (int sig = 1; sig < NSIG; ++sig)
    Check(sigismember(&mask, sig) != 1, "bootstrap blocked signal");
}
void OneTask() {
  DIR* dir = opendir("/proc/self/task");
  Check(dir, "bootstrap task directory");
  size_t count = 0;
  errno = 0;
  while (auto* entry = readdir(dir)) {
    if (entry->d_name[0] != '.') ++count;
  }
  int error = errno;
  closedir(dir);
  Check(!error && count == 1, "bootstrap requires one task");
}
void CheckDescriptors(bool initial, std::span<const int> aliases = {},
                      std::span<const int> namespaces = {}) {
  Check(aliases.empty() || aliases.size() == 6,
        "bootstrap owned FD witness shape");
  Check(namespaces.empty() || namespaces.size() == 2,
        "bootstrap namespace FD witness shape");
  struct stat null{};
  Check(!stat("/dev/null", &null), "bootstrap null device");
  std::array<struct stat, 4> pipes{};
  size_t count = 0;
  for (int fd = 0; fd <= 8; ++fd) {
    struct stat st{};
    int flags = fcntl(fd, F_GETFL);
    Check(!fstat(fd, &st) && flags >= 0, "bootstrap missing FD");
    if (fd < 3)
      Check(S_ISCHR(st.st_mode) && st.st_rdev == null.st_rdev &&
                (flags & O_ACCMODE) == (fd ? O_WRONLY : O_RDONLY),
            "bootstrap stdio");
    else if (fd <= 5 || fd == 8) {
      Check(
          S_ISFIFO(st.st_mode) &&
              (flags & O_ACCMODE) == (fd == 5 || fd == 8 ? O_WRONLY : O_RDONLY),
          "bootstrap pipe direction");
      for (size_t i = 0; i < count; ++i)
        Check(st.st_dev != pipes[i].st_dev || st.st_ino != pipes[i].st_ino,
              "bootstrap aliased pipes");
      pipes[count++] = st;
    } else
      Check(S_ISDIR(st.st_mode) && (flags & O_ACCMODE) == O_RDONLY &&
                !(flags & O_PATH),
            "bootstrap directory FD");
    if (fd >= 3) {
      if (initial)
        Check(!fcntl(fd, F_SETFD, FD_CLOEXEC), "bootstrap CLOEXEC");
      else
        Check(fcntl(fd, F_GETFD) == FD_CLOEXEC, "bootstrap lost CLOEXEC");
    }
  }
  for (size_t i = 0; i < aliases.size(); ++i) {
    int fd = aliases[i];
    Check(std::find(namespaces.begin(), namespaces.end(), fd) ==
                  namespaces.end() &&
              fd >= 9 && fcntl(fd, F_GETFD) == FD_CLOEXEC,
          "bootstrap owned CLOEXEC FD");
    for (size_t j = 0; j < i; ++j)
      Check(fd != aliases[j], "bootstrap duplicate owned FD");
    struct stat actual{}, expected{};
    Check(!fstat(fd, &actual), "bootstrap owned FD stat");
    int result =
        i < 4 ? fstat(static_cast<int>(3 + i), &expected)
              : stat(i == 4 ? "/proc/self" : "/proc/self/ns/mnt", &expected);
    int flags = fcntl(fd, F_GETFL);
    Check(!result && actual.st_dev == expected.st_dev &&
              actual.st_ino == expected.st_ino &&
              actual.st_mode == expected.st_mode && flags >= 0 &&
              !(flags & O_PATH) &&
              (flags & O_ACCMODE) == (i == 2 ? O_WRONLY : O_RDONLY),
          "bootstrap owned FD identity");
  }
  DIR* dir = opendir("/proc/self/fd");
  Check(dir, "bootstrap FD directory");
  int own = dirfd(dir);
  bool extra = false;
  errno = 0;
  while (auto* entry = readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    int fd = -1;
    auto* end = entry->d_name + std::char_traits<char>::length(entry->d_name);
    auto result = std::from_chars(entry->d_name, end, fd);
    if (result.ec != std::errc{} || result.ptr != end ||
        (fd > 8 && fd != own &&
         std::find(aliases.begin(), aliases.end(), fd) == aliases.end() &&
         std::find(namespaces.begin(), namespaces.end(), fd) ==
             namespaces.end()))
      extra = true;
  }
  int error = errno;
  closedir(dir);
  Check(!error && !extra, "bootstrap unexpected FD");
}
}
WorkerInitialNamespaces::WorkerInitialNamespaces() {
  try {
    Check(getuid() == 0 && geteuid() == 0, "namespace capture root startup");
    CheckDescriptors(true);
    OneTask();
    Fd initial{
        open("/proc/1", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    struct statfs proc{};
    Check(initial.value >= 0 && !fstatfs(initial.value, &proc) &&
              proc.f_type == PROC_SUPER_MAGIC && Stat(initial.value).pid == 1,
          "namespace capture PID1 proc object");
    const char* paths[] = {"ns/pid", "ns/mnt"};
    for (size_t i = 0; i < 2; ++i) {
      fds_[i] = openat(initial.value, paths[i], O_RDONLY | O_CLOEXEC);
      struct stat st{};
      struct statfs fs{};
      Check(fds_[i] >= 9 && !fstat(fds_[i], &st) && !fstatfs(fds_[i], &fs) &&
                fs.f_type == NSFS_MAGIC,
            "namespace capture PID1 handle");
      devices_[i] = st.st_dev;
      inodes_[i] = st.st_ino;
    }
    ValidateCurrent();
  } catch (...) {
    for (int fd : fds_)
      if (fd >= 0) close(fd);
    throw;
  }
}
WorkerInitialNamespaces::~WorkerInitialNamespaces() { Close(); }
bool WorkerInitialNamespaces::Close() noexcept {
  bool ok = true;
  for (int& fd : fds_)
    if (fd >= 0) {
      if (close(fd)) ok = false;
      fd = -1;
    }
  return ok;
}
void WorkerInitialNamespaces::ValidateCurrent() const {
  const char* paths[] = {"/proc/self/ns/pid", "/proc/self/ns/mnt"};
  Check(fds_[0] != fds_[1], "namespace witnesses must be distinct descriptors");
  for (size_t i = 0; i < 2; ++i) {
    struct stat held{}, self{};
    struct statfs fs{};
    int flags = fcntl(fds_[i], F_GETFL);
    Check(fds_[i] >= 9 && fcntl(fds_[i], F_GETFD) == FD_CLOEXEC && flags >= 0 &&
              !(flags & O_PATH) && (flags & O_ACCMODE) == O_RDONLY &&
              !fstat(fds_[i], &held) && !fstatfs(fds_[i], &fs) &&
              fs.f_type == NSFS_MAGIC && !stat(paths[i], &self) &&
              held.st_dev == devices_[i] && held.st_ino == inodes_[i] &&
              self.st_dev == held.st_dev && self.st_ino == held.st_ino,
          "captured namespace identity/context changed");
  }
}
void ValidateWorkerBootstrap(const WorkerBootstrapPolicy& policy) {
  policy.namespaces.ValidateCurrent();
  CheckDescriptors(false, {}, policy.namespaces.Descriptors());
  Parent();
  Context(policy);
  OneTask();
  struct sigaction action{};
  action.sa_handler = SIG_IGN;
  Check(!sigaction(SIGPIPE, &action, nullptr), "bootstrap SIGPIPE");
}
void FinishWorkerBootstrap(const WorkerBootstrapPolicy& policy,
                           std::span<const int> loop_fds) {
  policy.namespaces.ValidateCurrent();
  CheckDescriptors(false, loop_fds, policy.namespaces.Descriptors());
  Parent();
  Context(policy);
  OneTask();
  Check(policy.namespaces.Close(), "bootstrap namespace witness close");
  CheckDescriptors(false, loop_fds);
  Parent();
  Context(policy);
  OneTask();
}
}
