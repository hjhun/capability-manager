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

#include "catalog/file_metadata.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <string>

#include "common/error.hh"

namespace capmgr {

namespace {

void Require(bool okay) {
  if (!okay) throw Error(ErrorCode::kPermission, "Unsafe metadata descriptor");
}

void Flags(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  const int descriptor = fcntl(fd, F_GETFD);
  Require(flags >= 0 && (flags & O_PATH) && (flags & O_NOFOLLOW) &&
          descriptor >= 0 && (descriptor & FD_CLOEXEC));
}
}  // namespace
void RequireDataPinSupport(int directory) {
  const int probe = openat(directory, ".", O_PATH | O_NOFOLLOW | O_CLOEXEC);
  Require(probe >= 0);
  try {
    Flags(probe);
    struct stat info{};
    Require(!fstat(probe, &info) && S_ISDIR(info.st_mode));
  } catch (...) {
    close(probe);  // Directory only: never a SQLite POSIX-lock inode.
    throw;
  }

  close(probe);
}

void ValidateDataPin(int fd) {
  Flags(fd);
  struct stat info{};
  Require(!fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_nlink == 1);
}

ssize_t MetadataAttribute(int fd, const char* name, void* value, size_t size) {
  const int flags = fcntl(fd, F_GETFL);
  Require(flags >= 0);
  if (!(flags & O_PATH)) return fgetxattr(fd, name, value, size);
  ValidateDataPin(fd);
  // fd is a live, privately owned pin held across this syscall. Trusted procfs
  // self-FD resolution is an image prerequisite; no arbitrary PID/path accepted.
  const auto path = "/proc/self/fd/" + std::to_string(fd);
  return getxattr(path.c_str(), name, value, size);
}
}  // namespace capmgr
