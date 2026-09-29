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

#include <amd_mod_common.h>
#include <tzplatform_config.h>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>

#include "amd-module/module_thread.hh"
#include "amd-module/module_config.hh"
#include "common/logging.hh"

namespace {

constexpr const char kConfigPath[] = "/etc/capmgr/amd.json";
constexpr const char kCatalogDirectory[] = "/opt/usr/capmgr/catalog";
constexpr const char kGenerationLock[] = "/opt/usr/capmgr/generation.lock";

struct File {
  int fd = -1;
  ~File() {
    if (fd >= 0) close(fd);
  }
};

void Check(bool okay, const char* message) {
  if (!okay) throw capmgr::Error(capmgr::ErrorCode::kPermission, message);
}

void NoAttributes(int fd, bool directory) {
  for (const char* name : {"system.posix_acl_access", "security.capability",
                           "system.posix_acl_default"}) {
    if (!directory && std::string_view(name) == "system.posix_acl_default")
      continue;
    errno = 0;
    auto size = fgetxattr(fd, name, nullptr, 0);
    Check(
        size < 0 && (errno == ENODATA || errno == ENOTSUP),
        "AMD trusted configuration has extra attributes or unreadable metadata");
  }
}

void TrustedDirectory(const char* path) {
  File held{open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
  struct stat info{};
  Check(held.fd >= 0 && !fstat(held.fd, &info) && S_ISDIR(info.st_mode) &&
            info.st_uid == 0 && info.st_gid == 0 && !(info.st_mode & 0022),
        "AMD trusted directory prerequisite failed");
  NoAttributes(held.fd, true);
}

capmgr::AmdModuleConfig ReadConfig() {
  for (const char* path : {"/", "/etc", "/etc/capmgr"}) TrustedDirectory(path);
  File file{open(kConfigPath, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
  struct stat before{}, after{}, named{};
  Check(file.fd >= 0 && !fstat(file.fd, &before) && S_ISREG(before.st_mode) &&
            before.st_nlink == 1 && before.st_uid == 0 && before.st_gid == 0 &&
            (before.st_mode & 07777) == 0644 && before.st_size > 0 &&
            before.st_size <= 8192,
        "AMD config must be protected root:root0644, regular and bounded");
  NoAttributes(file.fd, false);
  std::string bytes;
  char buffer[1024];
  for (;;) {
    auto count = read(file.fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0)
      throw std::system_error(errno, std::generic_category(),
                              "read AMD config");
    if (!count) break;
    bytes.append(buffer, static_cast<size_t>(count));
    Check(bytes.size() <= 8192, "AMD config exceeds maximum size");
  }
  Check(!fstat(file.fd, &after) && !lstat(kConfigPath, &named) &&
            before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
            after.st_dev == named.st_dev && after.st_ino == named.st_ino &&
            before.st_mode == after.st_mode && before.st_uid == after.st_uid &&
            before.st_gid == after.st_gid && after.st_nlink == 1 &&
            before.st_size == after.st_size &&
            bytes.size() == static_cast<size_t>(after.st_size) &&
            before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
            before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
            before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
            before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
        "AMD config changed while reading");
  const int fd = std::exchange(file.fd, -1);
  if (close(fd))
    throw std::system_error(errno, std::generic_category(), "close AMD config");
  return capmgr::ParseAmdModuleConfig(capmgr::Json::parse(bytes));
}

capmgr::ReadLeasePolicy Policy(const capmgr::AmdModuleConfig& config) {
  struct passwd account{};
  struct passwd* result = nullptr;
  char storage[8192];
  const int error =
      getpwnam_r("app_fw", &account, storage, sizeof(storage), &result);
  Check(error == 0 && result != nullptr, "AMD app_fw account unavailable");
  const auto uid = account.pw_uid;
  const auto gid = account.pw_gid;
  Check(getuid() == uid && geteuid() == uid && getgid() == gid &&
            getegid() == gid,
        "AMD module must run as app_fw");
  for (const char* path : {"/", "/opt", "/opt/usr", "/opt/usr/capmgr"})
    TrustedDirectory(path);
  return {kCatalogDirectory,
          kGenerationLock,
          uid,
          uid,
          gid,
          gid,
          0700,
          0600,
          0600,
          config.directory_label,
          config.file_label,
          config.lock_label};
}

std::mutex module_mutex;
std::unique_ptr<capmgr::AmdModuleThread> module;

}  // namespace

extern "C" EXPORT int AMD_MOD_INIT() {
  try {
    std::lock_guard lock(module_mutex);
    if (module) return 0;
    const auto config = ReadConfig();
    if (!config.enabled) {
      LOG(INFO)
          << "AMD catalog disabled: provisioning and explicit enable required";
      return 0;
    }
    auto policy = Policy(config);
    const char* path = tzplatform_mkpath(TZ_SYS_DB, ".tizen_action.db");
    Check(path != nullptr && path[0] == '/',
          "Action DB platform path unavailable");
    std::string source(path);  // Copy platform scratch storage immediately.
    auto candidate = std::make_unique<capmgr::AmdModuleThread>(
        std::move(policy), std::move(source));
    module = std::move(candidate);
    // Retain ownership while source/configured catalog readiness is retried.
    LOG(INFO)
        << "AMD optional catalog worker started; import readiness is separate";
  } catch (const std::exception& error) {
    capmgr::logging::Failure("AMD optional module initialization failed",
                             error.what());
  } catch (...) {
    capmgr::logging::Failure("AMD optional module initialization failed",
                             "unknown exception");
  }
  return 0;
}

extern "C" EXPORT void AMD_MOD_FINI() {
  std::lock_guard lock(module_mutex);
  module.reset();  // stop + join, SQLite/context owner gone BEFORE dlclose.
}
