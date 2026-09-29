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
// SPDX-License-Identifier: Apache-2.0

#include "launcher/namespace_init.hh"

#include <cerrno>
#include <cstddef>

#include <fcntl.h>
#include <linux/capability.h>
#include <linux/magic.h>
#include <sys/vfs.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace capmgr {

namespace {

// ARMv7 exposes legacy 16-bit setxid numbers alongside the 32-bit variants.
#ifdef SYS_setresuid32
constexpr long kSetUid = SYS_setresuid32, kSetGid = SYS_setresgid32,
               kSetGroups = SYS_setgroups32;
#else
constexpr long kSetUid = SYS_setresuid, kSetGid = SYS_setresgid,
               kSetGroups = SYS_setgroups;
#endif
bool WriteAll(int fd, const void* bytes, size_t size) {
  const auto* data = static_cast<const char*>(bytes);
  while (size) {
    ssize_t n = write(fd, data, size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    data += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

bool Report(const NamespaceInitConfig& c, InitMessageKind kind, InitStage stage,
            int error = 0, int code = 0, int signal = 0) {
  InitMessage message{kind, stage, error, code, signal};
  return WriteAll(c.status_write, &message, sizeof(message));
}

int Fail(const NamespaceInitConfig& c, InitStage stage) {
  int error = errno ? errno : EIO;
  Report(c, InitMessageKind::Failed, stage, error);
  return 125;
}

size_t Length(const char* s, size_t maximum) {
  size_t size = 0;
  if (!s) return maximum;
  while (size < maximum && s[size]) ++size;
  return size;
}

bool LabelEquals(const char* expected) {
  int fd = open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  char value[256];
  ssize_t size = read(fd, value, sizeof(value));
  int saved = errno;
  close(fd);
  errno = saved;
  if (size <= 0 || size == static_cast<ssize_t>(sizeof(value))) return false;
  while (size > 0 && (value[size - 1] == '\n' || value[size - 1] == '\0'))
    --size;
  size_t wanted = Length(expected, sizeof(value));
  if (wanted != static_cast<size_t>(size)) return false;
  for (size_t i = 0; i < wanted; ++i)
    if (value[i] != expected[i]) return false;
  return true;
}

bool SetLabel(const char* expected) {
  if (LabelEquals(expected)) return true;
  size_t length = Length(expected, 256);
  if (!length || length >= 256) {
    errno = EINVAL;
    return false;
  }

  int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;
  bool ok = WriteAll(fd, expected, length);
  int saved = errno;
  close(fd);
  errno = saved;
  return ok && LabelEquals(expected);
}

struct Dirent64 {
  uint64_t ino;
  int64_t offset;
  unsigned short length;
  unsigned char type;
  char name[1];
};

bool CloseInherited(int control, int status, int parent_process) {
  int directory = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) return false;
  alignas(8) char data[4096];
  for (;;) {
    long size = syscall(SYS_getdents64, directory, data, sizeof(data));
    if (size < 0 && errno == EINTR) continue;
    if (size < 0) {
      close(directory);
      return false;
    }
    if (!size) break;
    for (long offset = 0; offset < size;) {
      auto* entry = reinterpret_cast<Dirent64*>(data + offset);
      if (entry->length < offsetof(Dirent64, name) + 2 ||
          entry->length > size - offset) {
        close(directory);
        errno = EIO;
        return false;
      }
      int fd = 0;
      bool numeric = true;
      size_t available = entry->length - offsetof(Dirent64, name), i = 0;
      for (; i < available && entry->name[i]; ++i) {
        char digit = entry->name[i];
        if (digit < '0' || digit > '9' || fd > 214748364 ||
            (fd == 214748364 && digit > '7')) {
          numeric = false;
          break;
        }
        fd = fd * 10 + (digit - '0');
      }
      if (i == available) numeric = false;
      if (numeric && i && fd > 2 && fd != directory && fd != control &&
          fd != status && fd != parent_process)
        close(fd);
      offset += entry->length;
    }
  }

  close(directory);
  return true;
}

bool ProcessAlive(int process) {
  struct statfs fs{};
  struct stat info{};
  if (fstatfs(process, &fs) < 0 || fs.f_type != PROC_SUPER_MAGIC ||
      fstat(process, &info) < 0 || !S_ISDIR(info.st_mode)) {
    errno = EINVAL;
    return false;
  }

  int fd = openat(process, "stat", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  char data[4096];
  ssize_t size;
  do {
    size = read(fd, data, sizeof(data));
  } while (size < 0 && errno == EINTR);
  int saved = errno;
  close(fd);
  errno = saved;
  if (size <= 0 || size == static_cast<ssize_t>(sizeof(data))) {
    errno = ESRCH;
    return false;
  }
  // comm may contain ')': the final ')' precedes the state field.
  ssize_t end = size - 1;
  while (end >= 0 && data[end] != ')') --end;
  if (end < 0 || end + 3 >= size || data[end + 1] != ' ' ||
      data[end + 3] != ' ') {
    errno = EINVAL;
    return false;
  }
  char state = data[end + 2];
  if (state == 'Z' || state == 'X' || state == 'x') {
    errno = ESRCH;
    return false;
  }

  if (state != 'R' && state != 'S' && state != 'D' && state != 'T' &&
      state != 't' && state != 'I' && state != 'W' && state != 'P') {
    errno = EINVAL;
    return false;
  }
  return true;
}

bool MountIsolated(int parent) {
  struct statfs fs{};
  struct stat before{}, now{};
  if (fstatfs(parent, &fs) < 0 || fs.f_type != NSFS_MAGIC ||
      fstat(parent, &before) < 0)
    return false;
  // 4.4 has no NS_GET_NSTYPE ioctl. Verify the kernel's namespace FD link type.
  char path[64] = "/proc/self/fd/";
  size_t used = 14;
  char digits[16];
  size_t count = 0;
  unsigned value = static_cast<unsigned>(parent);
  do {
    digits[count++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value);
  while (count) path[used++] = digits[--count];
  path[used] = '\0';
  char link[64];
  ssize_t size = readlink(path, link, sizeof(link));
  if (size < 6 || link[0] != 'm' || link[1] != 'n' || link[2] != 't' ||
      link[3] != ':' || link[4] != '[') {
    errno = EINVAL;
    return false;
  }

  int current = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
  if (current < 0) return false;
  bool ok = fstat(current, &now) == 0;
  int saved = errno;
  close(current);
  errno = saved;
  if (!ok) return false;
  if (before.st_dev == now.st_dev && before.st_ino == now.st_ino) {
    errno = EPERM;
    return false;
  }
  return true;
}

bool ParentAlive(const NamespaceInitConfig& c) {
  if (!ProcessAlive(c.parent_process)) return false;
  pollfd poller{c.control_read, POLLIN, 0};
  int rc;
  do {
    rc = poll(&poller, 1, 0);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0) return false;
  if (poller.revents & (POLLHUP | POLLERR | POLLNVAL)) {
    errno = EPIPE;
    return false;
  }
  return true;
}

bool AwaitGo(const NamespaceInitConfig& c) {
  timespec start{};
  if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) return false;
  for (;;) {
    if (!ParentAlive(c)) return false;
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return false;
    if (now.tv_sec - start.tv_sec >= 5) {
      errno = ETIMEDOUT;
      return false;
    }
    pollfd item{c.control_read, POLLIN, 0};
    int ready = poll(&item, 1, 50);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) return false;
    if (!ready) continue;
    char go = 0;
    ssize_t size = read(c.control_read, &go, 1);
    if (size < 0 && errno == EINTR) continue;
    if (size != 1 || go != 'G') {
      errno = EPIPE;
      return false;
    }
    return ParentAlive(c);
  }
}

bool ClearAndVerifyCapabilities() {
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  __user_cap_data_struct data[2]{};
  if (syscall(SYS_capset, &header, data) < 0 ||
      prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0 ||
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
    return false;
  if (syscall(SYS_capget, &header, data) < 0 ||
      prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1)
    return false;
  for (auto& item : data)
    if (item.effective || item.permitted || item.inheritable) {
      errno = EPERM;
      return false;
    }
  for (int cap = 0; cap < 64; ++cap) {
    int result = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (result < 0 && errno == EINVAL) break;
    if (result != 0) {
      errno = EPERM;
      return false;
    }
    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0) != 0) {
      errno = EPERM;
      return false;
    }
  }
  return true;
}
}  // namespace

int NamespaceInit(void* configuration) noexcept {
  const auto& c = *static_cast<const NamespaceInitConfig*>(configuration);
  if (getpid() != 1 || !c.uid || !c.gid || c.control_read < 3 ||
      c.status_write < 3 || c.stdout_write < 3 || c.stderr_write < 3 ||
      c.parent_process < 3 || c.parent_mount_namespace < 3 ||
      Length(c.executable, 4096) >= 4096 || !c.executable ||
      c.executable[0] != '/' || Length(c.request, 65537) > 65536) {
    errno = EINVAL;
    return Fail(c, InitStage::Context);
  }
  int descriptors[] = {c.control_read,   c.status_write,
                       c.stdout_write,   c.stderr_write,
                       c.parent_process, c.parent_mount_namespace};
  for (size_t i = 0; i < 6; ++i) {
    int flags = fcntl(descriptors[i], F_GETFD);
    if (flags < 0 || !(flags & FD_CLOEXEC)) {
      errno = EINVAL;
      return Fail(c, InitStage::Context);
    }
    for (size_t j = 0; j < i; ++j)
      if (descriptors[i] == descriptors[j]) {
        errno = EINVAL;
        return Fail(c, InitStage::Context);
      }
  }

  if (!ProcessAlive(c.parent_process)) return Fail(c, InitStage::Parent);
  if (!MountIsolated(c.parent_mount_namespace))
    return Fail(c, InitStage::Mount);
  close(c.parent_mount_namespace);
  // Early guard; credential transitions may clear it, so set it again below.
  if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) return Fail(c, InitStage::Parent);
  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  for (int sig = 1; sig < NSIG; ++sig) {
    if (sig == SIGKILL || sig == SIGSTOP) continue;
    if (sigaction(sig, &action, nullptr) < 0 && errno != EINVAL)
      return Fail(c, InitStage::Signals);
  }
  sigset_t mask;
  sigemptyset(&mask);
  if (sigprocmask(SIG_SETMASK, &mask, nullptr) < 0)
    return Fail(c, InitStage::Signals);
  // Status-pipe failure is handled, not an asynchronous SIGPIPE exit.
  action.sa_handler = SIG_IGN;
  if (sigaction(SIGPIPE, &action, nullptr) < 0)
    return Fail(c, InitStage::Signals);
  if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) < 0 ||
      mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC,
            nullptr) < 0)
    return Fail(c, InitStage::Mount);
  int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (input < 0) return Fail(c, InitStage::Descriptors);
  if (dup2(input, 0) < 0 || fcntl(0, F_SETFD, 0) < 0 ||
      dup2(c.stdout_write, 1) < 0 || dup2(c.stderr_write, 2) < 0 ||
      chdir("/") < 0 ||
      !CloseInherited(c.control_read, c.status_write, c.parent_process))
    return Fail(c, InitStage::Descriptors);
  if (!SetLabel(c.smack_label)) return Fail(c, InitStage::Label);
  // PR_CAPBSET_DROP requires CAP_SETPCAP: do this BEFORE setresuid.
  bool found = false;
  for (int cap = 0; cap < 64; ++cap) {
    int present = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (present < 0 && errno == EINVAL) break;
    if (present < 0 || prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) < 0)
      return Fail(c, InitStage::Bounding);
    found = true;
  }

  if (!found) {
    errno = ENOTSUP;
    return Fail(c, InitStage::Bounding);
  }
  // Raw setxid syscalls avoid libc's multithread setxid rendezvous after clone.
  if (syscall(kSetGroups, 0, nullptr) < 0 ||
      syscall(kSetGid, c.gid, c.gid, c.gid) < 0 ||
      syscall(kSetUid, c.uid, c.uid, c.uid) < 0)
    return Fail(c, InitStage::Credentials);
  if (!ClearAndVerifyCapabilities()) return Fail(c, InitStage::Capabilities);
  uid_t ur, ue, us;
  gid_t gr, ge, gs;
  if (getresuid(&ur, &ue, &us) < 0 || getresgid(&gr, &ge, &gs) < 0 ||
      ur != c.uid || ue != c.uid || us != c.uid || gr != c.gid || ge != c.gid ||
      gs != c.gid || getgroups(0, nullptr) != 0 ||
      !LabelEquals(c.smack_label)) {
    errno = EPERM;
    return Fail(c, InitStage::Credentials);
  }

  if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || !ParentAlive(c))
    return Fail(c, InitStage::Parent);
  if (!Report(c, InitMessageKind::Ready, InitStage::Go)) return 125;
  if (!AwaitGo(c)) return Fail(c, InitStage::Go);
  int error_pipe[2];
  if (pipe2(error_pipe, O_CLOEXEC) < 0) return Fail(c, InitStage::Fork);
  pid_t child = static_cast<pid_t>(syscall(SYS_fork));
  if (child < 0) return Fail(c, InitStage::Fork);
  if (!child) {
    close(error_pipe[0]);
    close(c.control_read);
    close(c.status_write);
    close(c.parent_process);
    action.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &action, nullptr);
    char* argv[] = {const_cast<char*>(c.executable),
                    const_cast<char*>("--json"), const_cast<char*>(c.request),
                    nullptr};
    char* env[] = {const_cast<char*>("PATH=/usr/bin:/bin"),
                   const_cast<char*>("LANG=C.UTF-8"), nullptr};
    execve(c.executable, argv, env);
    int error = errno;
    WriteAll(error_pipe[1], &error, sizeof(error));
    _exit(127);
  }

  close(error_pipe[1]);
  int exec_error = 0;
  size_t received = 0;
  while (received < sizeof(exec_error)) {
    ssize_t count =
        read(error_pipe[0], reinterpret_cast<char*>(&exec_error) + received,
             sizeof(exec_error) - received);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      close(error_pipe[0]);
      return Fail(c, InitStage::Exec);
    }
    if (!count) break;
    received += static_cast<size_t>(count);
  }

  close(error_pipe[0]);
  if (received) {
    errno = received == sizeof(exec_error) ? exec_error : EIO;
    return Fail(c, InitStage::Exec);
  }
  siginfo_t observed{};
  if (waitid(P_PID, static_cast<id_t>(child), &observed,
             WEXITED | WNOHANG | WNOWAIT) < 0)
    return Fail(c, InitStage::Exec);
  if (observed.si_pid && observed.si_code != CLD_EXITED) {
    errno = EIO;
    return Fail(c, InitStage::Exec);
  }

  if (!Report(c, InitMessageKind::Started, InitStage::Exec)) return 125;
  for (;;) {
    int status = 0;
    pid_t reaped = waitpid(-1, &status, 0);
    if (reaped < 0 && errno == EINTR) continue;
    if (reaped < 0) return Fail(c, InitStage::Wait);
    if (reaped == child) {
      if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
        errno = EIO;
        return Fail(c, InitStage::Wait);
      }
      Report(c, InitMessageKind::Exited, InitStage::Wait, 0,
             WIFEXITED(status) ? WEXITSTATUS(status) : -1,
             WIFSIGNALED(status) ? WTERMSIG(status) : 0);
      return 0;
    }
  }
}
}  // namespace capmgr
