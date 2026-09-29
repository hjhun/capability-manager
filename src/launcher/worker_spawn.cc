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

#include "launcher/worker_spawn.hh"
#include "common/error.hh"

#include <array>
#include <charconv>
#include <cerrno>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CAPMGR_WORKER_IMAGE
#define CAPMGR_WORKER_IMAGE "/usr/libexec/capmgr/capmgr-spawn-worker"
#endif

namespace capmgr {

namespace {

constexpr const char* kImage = CAPMGR_WORKER_IMAGE;
[[noreturn]] void Fail(const char* message) {
  throw Error(ErrorCode::kIo, message);
}

void Check(bool condition, const char* message) {
  if (!condition) Fail(message);
}

struct Sources {
  std::array<int, 6> fd{-1, -1, -1, -1, -1, -1};
  ~Sources() {
    for (int n : fd)
      if (n >= 0) close(n);
  }
};

struct Actions {
  posix_spawn_file_actions_t files{};
  posix_spawnattr_t attributes{};
  bool files_ok = false, attributes_ok = false;
  Actions() {
    files_ok = posix_spawn_file_actions_init(&files) == 0;
    if (files_ok) attributes_ok = posix_spawnattr_init(&attributes) == 0;
    if (!files_ok || !attributes_ok) {
      if (files_ok) posix_spawn_file_actions_destroy(&files);
      Fail("Worker spawn setup");
    }
  }

  ~Actions() {
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&files);
  }
};

void Image() {
  int fd = open(kImage, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) Fail("Fixed worker image unavailable");
  struct stat info{};
  int result = fstat(fd, &info);
  close(fd);
#ifdef CAPMGR_TEST_WORKER_IMAGE
  uid_t owner = geteuid();  // compiled only in the independent test executable
#else
  uid_t owner = 0;
#endif
  Check(result == 0 && S_ISREG(info.st_mode) && info.st_uid == owner &&
            !(info.st_mode & 07022) && (info.st_mode & 0111),
        "Unsafe fixed worker image");
  // No path handoff from clients exists. This file check is not a replacement for
  // image-owned ancestor/mount provenance or a deployment update lock.
}
}  // namespace

uint64_t SpawnFixedWorker(OwnedChildren& children, uint64_t generation,
                          const WorkerInheritedFds& input) {
  Check(generation != 0, "Zero worker generation");
  Image();
  struct sigaction child{};
  Check(!sigaction(SIGCHLD, nullptr, &child) && child.sa_handler == SIG_DFL &&
            !(child.sa_flags & SA_NOCLDWAIT),
        "Worker requires exclusive child reaping");
  std::array<int, 6> original{input.command_read,      input.cancel_read,
                              input.reply_write,       input.parent_process,
                              input.catalog_directory, input.ready_write};
  Sources sources;
  std::array<struct stat, 4> pipes{};
  size_t pipe_count = 0;
  for (size_t i = 0; i < original.size(); ++i) {
    sources.fd[i] = fcntl(original[i], F_DUPFD_CLOEXEC, 9);
    Check(sources.fd[i] >= 9, "Worker descriptor duplication");
    struct stat st{};
    int flags = fcntl(sources.fd[i], F_GETFL);
    Check(!fstat(sources.fd[i], &st) && flags >= 0, "Worker descriptor stat");
    if (i < 3 || i == 5) {
      Check(S_ISFIFO(st.st_mode) &&
                (flags & O_ACCMODE) == (i == 2 || i == 5 ? O_WRONLY : O_RDONLY),
            "Worker pipe direction/type");
      for (size_t before = 0; before < pipe_count; ++before)
        Check(st.st_dev != pipes[before].st_dev ||
                  st.st_ino != pipes[before].st_ino,
              "Worker pipes must be independent");
      pipes[pipe_count++] = st;
    } else
      Check(S_ISDIR(st.st_mode) && (flags & O_ACCMODE) == O_RDONLY,
            "Worker directory descriptor");
  }
  Actions actions;
  for (int i = 0; i < 3; ++i)
    Check(!posix_spawn_file_actions_addopen(&actions.files, i, "/dev/null",
                                            i == 0 ? O_RDONLY : O_WRONLY, 0),
          "Worker null stdio");
  for (size_t i = 0; i < sources.fd.size(); ++i)
    Check(!posix_spawn_file_actions_adddup2(&actions.files, sources.fd[i],
                                            static_cast<int>(3 + i)),
          "Worker descriptor mapping");
  Check(!posix_spawn_file_actions_addclosefrom_np(&actions.files, 9),
        "Worker descriptor closefrom");
  sigset_t empty, defaults;
  sigemptyset(&empty);
  sigemptyset(&defaults);
  for (int signal : {SIGCHLD, SIGPIPE, SIGTERM, SIGINT, SIGHUP})
    sigaddset(&defaults, signal);
  Check(!posix_spawnattr_setsigmask(&actions.attributes, &empty) &&
            !posix_spawnattr_setsigdefault(&actions.attributes, &defaults) &&
            !posix_spawnattr_setpgroup(&actions.attributes, 0) &&
            !posix_spawnattr_setflags(&actions.attributes,
                                      POSIX_SPAWN_SETSIGMASK |
                                          POSIX_SPAWN_SETSIGDEF |
                                          POSIX_SPAWN_SETPGROUP),
        "Worker signal context");
  std::array<char, 21> number{};
  auto converted = std::to_chars(number.data(),
                                 number.data() + number.size() - 1, generation);
  Check(converted.ec == std::errc{}, "Worker generation encoding");
  char option[] = "--generation", lang[] = "LANG=C",
       path[] = "PATH=/usr/bin:/bin";
  char* argv[] = {const_cast<char*>(kImage), option, number.data(), nullptr};
  char* env[] = {lang, path, nullptr};
  auto owned = children.Reserve();
  pid_t pid = -1;
  int result =
      posix_spawn(&pid, kImage, &actions.files, &actions.attributes, argv, env);
  if (result == 0)
    children.AttachReserved(owned, pid);
  else
    children.AbandonUnspawned(owned);
  if (result != 0) {
    errno = result;
    Fail("Fixed worker exec failed");
  }
  return owned;
}
}  // namespace capmgr
