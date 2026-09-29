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

#include "launcher/worker_catalog.hh"
#include "catalog/catalog.hh"
#include "catalog/file_metadata.hh"

#include <array>
#include <cerrno>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace capmgr {

namespace {

[[noreturn]] void Deny() {
  throw Error(ErrorCode::kPermission, "Untrusted worker catalog files");
}

struct Fd {
  int value = -1;
  ~Fd() {
    if (value >= 0) close(value);
  }
};

bool NoAcl(int fd, const char* key) {
  if (MetadataAttribute(fd, key, nullptr, 0) >= 0) return false;
  return errno == ENODATA || errno == ENOTSUP;
}

bool Same(const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

void CheckFile(const struct stat& st, const WorkerCatalogFilePolicy& policy) {
  mode_t mode = st.st_mode & 07777;
  if (!S_ISREG(st.st_mode) || st.st_uid != policy.writer ||
      st.st_gid != policy.group || st.st_nlink != 1 || mode != policy.file_mode)
    Deny();
}
}  // namespace

WorkerCatalogSnapshot LoadWorkerCatalog(int source,
                                        const WorkerCatalogFilePolicy& policy) {
  if ((policy.directory_mode != 0700 && policy.directory_mode != 0750 &&
       policy.directory_mode != 02750) ||
      (policy.file_mode != 0600 && policy.file_mode != 0640))
    Deny();
  // Reject a non-directory BEFORE duplicating it: closing a duplicate of a
  // SQLite data FD could drop all same-process POSIX locks on that inode.
  struct stat supplied{};
  const int supplied_flags = fcntl(source, F_GETFL);
  if (fstat(source, &supplied) || !S_ISDIR(supplied.st_mode) ||
      supplied_flags < 0 || (supplied_flags & O_ACCMODE) != O_RDONLY ||
      (supplied_flags & O_PATH))
    Deny();
  Fd directory{fcntl(source, F_DUPFD_CLOEXEC, 3)};
  struct stat dir{};
  if (directory.value < 0 || fstat(directory.value, &dir) ||
      !S_ISDIR(dir.st_mode) || !Same(supplied, dir) ||
      dir.st_uid != policy.writer || dir.st_gid != policy.group)
    Deny();
  mode_t mode = dir.st_mode & 07777;
  if ((mode != policy.directory_mode) ||
      !NoAcl(directory.value, "system.posix_acl_access") ||
      !NoAcl(directory.value, "system.posix_acl_default"))
    Deny();
  // Require a readable directory anchor, never an application-provided O_PATH or
  // writable-open description. Its parent/namespace provenance is caller-owned.
  int flags = fcntl(directory.value, F_GETFL);
  if (flags < 0 || flags != supplied_flags || (flags & O_ACCMODE) != O_RDONLY ||
      (flags & O_PATH))
    Deny();
  constexpr std::array<const char*, 3> names{"catalog.db", "catalog.db-wal",
                                             "catalog.db-shm"};
  std::array<Fd, 3> files;
  std::array<struct stat, 3> before{};
  RequireDataPinSupport(directory.value);
  for (size_t i = 0; i < names.size(); ++i) {
    files[i].value =
        openat(directory.value, names[i], O_PATH | O_NOFOLLOW | O_CLOEXEC);
    ValidateDataPin(files[i].value);
    if (files[i].value < 0 || fstat(files[i].value, &before[i])) Deny();
    CheckFile(before[i], policy);
    if (!NoAcl(files[i].value, "system.posix_acl_access")) Deny();
  }
  // Directory alias gives SQLite valid sibling WAL/SHM names. Modern SQLite
  // canonicalizes it; this is not an FD-only VFS. Trusted stable path ancestry
  // and writer policy, plus the identity checks below, are required.
  Catalog catalog(
      "/proc/self/fd/" + std::to_string(directory.value) + "/catalog.db",
      Database::Access::kReadOnly);
  const char* filename =
      sqlite3_db_filename(catalog.database().handle(), "main");
  if (!filename || filename[0] != '/') Deny();
  const std::string resolved = filename;
  auto check_resolved = [&] {
    constexpr std::array<const char*, 3> suffixes{"", "-wal", "-shm"};
    for (size_t i = 0; i < suffixes.size(); ++i) {
      struct stat actual{};
      if (lstat((resolved + suffixes[i]).c_str(), &actual) ||
          !Same(before[i], actual))
        Deny();
      CheckFile(actual, policy);
    }
  };
  check_resolved();
  Transaction read(catalog.database(), false);
  const auto revision = catalog.Revision();
  std::vector<RegisteredCli> entries;
  catalog.Foreach(Kind::kCli, [&](const Json& detail) {
    if (entries.size() == 256)
      throw Error(ErrorCode::kLimit,
                  "Worker catalog snapshot exceeds 256 entries");
    auto entry = catalog.GetPrivate(detail.at("id").get<std::string>());
    if (entry.kind != Kind::kCli || entry.owner.empty() ||
        CanonicalId(Kind::kCli, entry.key) != entry.id)
      throw Error(ErrorCode::kDatabase, "Worker catalog identity mismatch");
    entries.push_back({entry.id, entry.executable});
    return true;
  });
  WorkerRegistry registry(std::move(entries));
  read.Commit();
  // Legitimate writer-side checkpoint/replacement can also invalidate startup.
  // Fail closed, do not silently bind a new generation to unchecked sidecars.
  for (size_t i = 0; i < names.size(); ++i) {
    struct stat after{};
    if (fstatat(directory.value, names[i], &after, AT_SYMLINK_NOFOLLOW) ||
        !Same(before[i], after))
      Deny();
    CheckFile(after, policy);
    ValidateDataPin(files[i].value);
    if (!NoAcl(files[i].value, "system.posix_acl_access")) Deny();
  }

  check_resolved();
  struct stat after{};
  if (fstat(directory.value, &after) || !Same(dir, after) ||
      (after.st_mode & 07777) != mode || after.st_uid != policy.writer ||
      after.st_gid != policy.group ||
      !NoAcl(directory.value, "system.posix_acl_access") ||
      !NoAcl(directory.value, "system.posix_acl_default"))
    Deny();
  return {revision, std::move(registry)};
}
}  // namespace capmgr
