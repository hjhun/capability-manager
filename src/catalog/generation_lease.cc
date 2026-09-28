// SPDX-License-Identifier: Apache-2.0
#include "catalog/generation_lease.hh"
#include "catalog/file_metadata.hh"

#include <array>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <thread>
#include <unistd.h>

namespace capmgr {
namespace {
using Clock = std::chrono::steady_clock;
void Require(bool value) {
  if (!value)
    throw Error(ErrorCode::kPermission, "Catalog generation policy rejected");
}
struct Fd {
  int value = -1;
  ~Fd() {
    // Never F_UNLCK: an inherited/duplicated description also belongs to parent.
    if (value >= 0) close(value);
  }
};
bool Normal(const std::string& path) {
  return !path.empty() && path[0] == '/' && path.back() != '/' &&
         path.find('\0') == std::string::npos &&
         std::filesystem::path(path).lexically_normal() == path;
}
void NoAcl(int fd, bool directory) {
  for (const char* name :
       {"system.posix_acl_access", "system.posix_acl_default"}) {
    if (!directory && std::string_view(name) == "system.posix_acl_default")
      continue;
    errno = 0;
    Require(MetadataAttribute(fd, name, nullptr, 0) < 0 &&
            (errno == ENODATA || errno == ENOTSUP));
  }
}
GenerationLeaseOperations real_operations;
}
int GenerationLeaseOperations::Lock(int fd, short type) {
  struct flock lock{};
  lock.l_type = type;
  lock.l_whence = SEEK_SET;
  return fcntl(fd, F_OFD_SETLK, &lock);
}
struct CatalogGenerationLease::Impl {
  ReadLeasePolicy policy;
  Mode mode;
  GenerationLeaseOperations& operations;
  std::string path;
  pid_t creator = getpid();
  bool poisoned = false, sealed = false;
  std::unique_ptr<CatalogReadLease> shared;
  Fd lock, directory;
  struct stat lock_identity{}, directory_identity{};
  std::array<Fd, 3> files;
  std::array<struct stat, 3> identities{};
  static constexpr std::array<const char*, 3> names{
      "catalog.db", "catalog.db-wal", "catalog.db-shm"};

  Impl(ReadLeasePolicy p, Mode m, std::chrono::milliseconds budget,
       GenerationLeaseOperations* o)
      : policy(std::move(p)),
        mode(m),
        operations(o ? *o : real_operations),
        path(policy.directory + "/catalog.db") {
    if (mode != Mode::kExisting && mode != Mode::kMaintenance)
      throw Error(ErrorCode::kInvalid, "Invalid generation mode");
    if (budget.count() < 0 || budget > std::chrono::seconds(1))
      throw Error(ErrorCode::kInvalid,
                  "Invalid maintenance acquisition budget");
    if (mode == Mode::kExisting) {
      // Includes independent O_RDONLY whole-file OFD plus all data-file pins.
      shared = std::make_unique<CatalogReadLease>(policy, &operations);
      sealed = true;
      return;
    }
    Require(Normal(policy.directory) && Normal(policy.lock_path) &&
            policy.lock_path != policy.directory &&
            !policy.lock_path.starts_with(policy.directory + "/") &&
            (policy.directory_mode == 0700 || policy.directory_mode == 0750 ||
             policy.directory_mode == 02750) &&
            (policy.file_mode == 0600 || policy.file_mode == 0640) &&
            (policy.lock_mode == 0600 || policy.lock_mode == 0640) &&
            !policy.directory_label.empty() && !policy.file_label.empty() &&
            !policy.lock_label.empty());
    lock.value = open(policy.lock_path.c_str(),
                      O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    Require(lock.value >= 0 && !fstat(lock.value, &lock_identity));
    Verify(lock.value, lock_identity, policy.maintainer, policy.lock_group,
           policy.lock_mode, policy.lock_label, false);
    const auto deadline = Clock::now() + budget;
    for (;;) {
      CheckOne(lock.value, lock_identity, policy.lock_path, policy.maintainer,
               policy.lock_group, policy.lock_mode, policy.lock_label, false);
      if (operations.Lock(lock.value, F_WRLCK) == 0) break;
      const int error = errno;
      if (error == EINVAL || error == ENOSYS || error == EOPNOTSUPP)
        throw Error(ErrorCode::kUnsupported, "OFD maintenance unavailable");
      if (error != EINTR && error != EAGAIN && error != EACCES)
        throw Error(ErrorCode::kIo, "OFD maintenance acquisition failed");
      if (Clock::now() >= deadline)
        throw Error(ErrorCode::kBusy, "Catalog generation is in use");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // The deadline bounds retries, not arbitrary filesystem/label operation IO.
    CheckOne(lock.value, lock_identity, policy.lock_path, policy.maintainer,
             policy.lock_group, policy.lock_mode, policy.lock_label, false);
    directory.value = open(policy.directory.c_str(),
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    Require(directory.value >= 0 &&
            !fstat(directory.value, &directory_identity));
    Verify(directory.value, directory_identity, policy.writer, policy.group,
           policy.directory_mode, policy.directory_label, true);
    RequireDataPinSupport(directory.value);
    Check();
  }
  void Verify(int fd, const struct stat& info, uid_t uid, gid_t gid,
              mode_t mode_value, const std::string& label, bool dir) {
    Require((dir ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) &&
            (dir || info.st_nlink == 1) && info.st_uid == uid &&
            info.st_gid == gid && (info.st_mode & 07777) == mode_value);
    NoAcl(fd, dir);
    Require(operations.Label(fd) == label);
  }
  void CheckOne(int fd, const struct stat& identity, const std::string& name,
                uid_t uid, gid_t gid, mode_t mode_value,
                const std::string& label, bool dir) {
    struct stat held{}, named{};
    Require(!fstat(fd, &held) && !lstat(name.c_str(), &named) &&
            identity.st_dev == held.st_dev && identity.st_ino == held.st_ino &&
            identity.st_dev == named.st_dev && identity.st_ino == named.st_ino);
    Verify(fd, held, uid, gid, mode_value, label, dir);
    Require((named.st_mode & 07777) == mode_value && named.st_uid == uid &&
            named.st_gid == gid);
  }
  void Check() {
    Require(creator == getpid() && !poisoned);
    try {
      if (shared) {
        shared->Check();
        return;
      }
      CheckOne(lock.value, lock_identity, policy.lock_path, policy.maintainer,
               policy.lock_group, policy.lock_mode, policy.lock_label, false);
      CheckOne(directory.value, directory_identity, policy.directory,
               policy.writer, policy.group, policy.directory_mode,
               policy.directory_label, true);
      if (sealed)
        for (size_t i = 0; i < names.size(); ++i) {
          ValidateDataPin(files[i].value);
          CheckOne(files[i].value, identities[i],
                   policy.directory + "/" + names[i], policy.writer,
                   policy.group, policy.file_mode, policy.file_label, false);
        }
    } catch (...) {
      poisoned = true;
      throw;
    }
  }
  void Prepare() {
    Check();
    if (shared) return;
    // Exclusive bootstrap precreates only the fixed main filename with the exact
    // mode; SQLite sidecars inherit its mode. No arbitrary repair/relabel occurs.
    Fd main;
    main.value =
        openat(directory.value, names[0], O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (main.value >= 0) ValidateDataPin(main.value);
    if (main.value < 0 && errno == ENOENT)
      main.value = openat(directory.value, names[0],
                          O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                          policy.file_mode);
    struct stat info{};
    Require(main.value >= 0 && !fstat(main.value, &info));
    Verify(main.value, info, policy.writer, policy.group, policy.file_mode,
           policy.file_label, false);
    // Existing sidecars must already be safe. Only absence is permitted under EX.
    for (size_t i = 1; i < names.size(); ++i) {
      Fd file;
      file.value =
          openat(directory.value, names[i], O_PATH | O_NOFOLLOW | O_CLOEXEC);
      if (file.value < 0 && errno == ENOENT) continue;
      ValidateDataPin(file.value);
      Require(file.value >= 0 && !fstat(file.value, &info));
      Verify(file.value, info, policy.writer, policy.group, policy.file_mode,
             policy.file_label, false);
    }
    Check();
  }
  void Seal() {
    Check();
    if (shared) return;
    for (size_t i = 0; i < names.size(); ++i) {
      files[i].value =
          openat(directory.value, names[i], O_PATH | O_NOFOLLOW | O_CLOEXEC);
      ValidateDataPin(files[i].value);
      Require(files[i].value >= 0 && !fstat(files[i].value, &identities[i]));
      Verify(files[i].value, identities[i], policy.writer, policy.group,
             policy.file_mode, policy.file_label, false);
    }
    sealed = true;
    Check();
  }
};
std::unique_ptr<CatalogGenerationLease> CatalogGenerationLease::Acquire(
    ReadLeasePolicy policy, Mode mode, std::chrono::milliseconds budget,
    GenerationLeaseOperations* operations) {
  return std::unique_ptr<CatalogGenerationLease>(new CatalogGenerationLease(
      std::make_unique<Impl>(std::move(policy), mode, budget, operations)));
}
CatalogGenerationLease::CatalogGenerationLease(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
CatalogGenerationLease::~CatalogGenerationLease() = default;
const std::string& CatalogGenerationLease::Path() const noexcept {
  return impl_->path;
}
bool CatalogGenerationLease::Maintenance() const noexcept {
  return impl_->mode == Mode::kMaintenance;
}
bool CatalogGenerationLease::InCreator() const noexcept {
  return impl_->creator == getpid();
}
void CatalogGenerationLease::Check() { impl_->Check(); }
void CatalogGenerationLease::Prepare() { impl_->Prepare(); }
void CatalogGenerationLease::Seal() { impl_->Seal(); }
void CatalogGenerationLease::Opened(Database& database) {
  try {
    Check();
    const char* path = sqlite3_db_filename(database.handle(), "main");
    Require(sqlite3_db_readonly(database.handle(), "main") == 0 && path &&
            Path() == path);
    Statement mode(database.handle(), "PRAGMA journal_mode");
    Require(mode.Step() && mode.Text(0) == "wal");
    Check();
  } catch (...) {
    impl_->poisoned = true;
    throw;
  }
}
}
