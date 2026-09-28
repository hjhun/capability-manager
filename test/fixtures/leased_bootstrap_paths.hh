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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_LEASED_BOOTSTRAP_PATHS_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_LEASED_BOOTSTRAP_PATHS_HH_

#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace capmgr::fixture::leasedbootstrap {
struct Paths {
  std::string scope;
  std::string catalog;
  std::string lock_parent;
  std::string lock;
};
inline void Require(bool okay, const char* why) {
  if (!okay) throw std::runtime_error(why);
}
// Private fixture topology, never a production policy or application path.
inline Paths ParseCatalogLink(std::string_view path) {
  constexpr std::string_view prefix =
      "/opt/usr/capmgr-leased-bootstrap-fixture-";
  constexpr std::string_view suffix = "/catalog";
  Require(path.size() == prefix.size() + 6 + suffix.size() &&
              path.starts_with(prefix) && path.ends_with(suffix),
          "exact leased bootstrap catalog topology");
  for (char c : path.substr(prefix.size(), 6))
    Require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9'),
            "exact leased bootstrap scope component");
  Paths result;
  result.scope = std::string(path.substr(0, prefix.size() + 6));
  result.catalog = std::string(path);
  result.lock_parent = result.scope + "/lock-parent";
  result.lock = result.lock_parent + "/generation.lock";
  return result;
}
namespace detail {
class Directory {
 public:
  explicit Directory(const std::string& path)
      : fd_(open(path.c_str(),
                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) {
    Require(fd_ >= 0, "leased bootstrap ancestry open");
  }
  ~Directory() {
    if (fd_ >= 0) close(fd_);
  }
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  int Get() const { return fd_; }
  void Close() {
    int owned = fd_;
    fd_ = -1;
    Require(!close(owned), "leased bootstrap ancestry close");
  }

 private:
  int fd_;
};
// Ancestry/backing only. The caller's captured WorkerInitialNamespaces supplies
// the initial-namespace premise; never reopen /proc/1 after capability reduction.
inline struct stat InspectDirectory(const std::string& path, mode_t mode) {
  Directory directory(path);
  struct stat held{}, named{};
  struct statfs backing{};
  Require(!fstat(directory.Get(), &held) && !lstat(path.c_str(), &named) &&
              S_ISDIR(held.st_mode) && S_ISDIR(named.st_mode) &&
              held.st_dev == named.st_dev && held.st_ino == named.st_ino &&
              held.st_uid == 0 && held.st_gid == 0 && named.st_uid == 0 &&
              named.st_gid == 0 && held.st_mode == named.st_mode &&
              !(held.st_mode & 07022) &&
              (!mode || (held.st_mode & 07777) == mode) &&
              !fstatfs(directory.Get(), &backing) &&
              backing.f_type == EXT4_SUPER_MAGIC,
          "leased bootstrap protected directory");
  for (const char* name :
       {"system.posix_acl_access", "system.posix_acl_default"}) {
    errno = 0;
    Require(fgetxattr(directory.Get(), name, nullptr, 0) < 0 &&
                (errno == ENODATA || errno == ENOTSUP),
            "leased bootstrap directory ACL");
  }
  directory.Close();
  return held;
}
}  // namespace detail

// Borrowed readable directory must remain exclusively stable through return.
// Reject wrong types/flags BEFORE opening anything; never dup/close the caller FD.
// This is own-FD path metadata, not peer credential authority or generic policy.
inline Paths ResolveCatalog(int borrowed_fd) {
  struct stat held{};
  const int flags = fcntl(borrowed_fd, F_GETFL);
  Require(flags >= 0 && !(flags & O_PATH) && (flags & O_ACCMODE) == O_RDONLY &&
              fcntl(borrowed_fd, F_GETFD) == FD_CLOEXEC &&
              !fstat(borrowed_fd, &held) && S_ISDIR(held.st_mode),
          "leased bootstrap borrowed directory");
  std::array<char, 4097> link{};
  const auto name = "/proc/self/fd/" + std::to_string(borrowed_fd);
  const ssize_t count = readlink(name.c_str(), link.data(), link.size());
  Require(count > 0 && static_cast<size_t>(count) < link.size(),
          "leased bootstrap complete own FD link");
  const auto paths = ParseCatalogLink(
      std::string_view(link.data(), static_cast<size_t>(count)));
  for (const char* parent : {"/", "/opt", "/opt/usr"})
    detail::InspectDirectory(parent, 0);
  detail::InspectDirectory(paths.scope, 0700);
  const auto named = detail::InspectDirectory(paths.catalog, 0700);
  detail::InspectDirectory(paths.lock_parent, 0700);
  struct stat after{};
  Require(!fstat(borrowed_fd, &after) && fcntl(borrowed_fd, F_GETFL) == flags &&
              fcntl(borrowed_fd, F_GETFD) == FD_CLOEXEC &&
              S_ISDIR(after.st_mode) && held.st_dev == after.st_dev &&
              held.st_ino == after.st_ino && named.st_dev == after.st_dev &&
              named.st_ino == after.st_ino && named.st_mode == after.st_mode &&
              named.st_uid == after.st_uid && named.st_gid == after.st_gid,
          "leased bootstrap held/named directory changed");
  return paths;
}
}  // namespace capmgr::fixture::leasedbootstrap

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_LEASED_BOOTSTRAP_PATHS_HH_
