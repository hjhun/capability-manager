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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_CONTEXT_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_CONTEXT_HH_

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>

namespace capmgr::fixture::realpolicy {
using Json = nlohmann::json;
inline void Check(bool okay, const char* why) {
  if (!okay) throw std::runtime_error(why);
}
inline std::string Self() {
  return std::filesystem::read_symlink("/proc/self/exe").string();
}
struct FixedRole {
  const char* name;
  uid_t uid;
  const char* label;
  bool platform_group = false;
};
constexpr std::array<FixedRole, 3> kRoles{{{"system301", 301, "System"},
                                           {"shell301", 301, "User::Shell"},
                                           {"shell1", 1, "User::Shell"}}};
constexpr gid_t kPlatformGroup = 10212;
constexpr std::array<FixedRole, 1> kPlatformRoles{
    {{"system301-platform", 301, "System", true}}};
inline const FixedRole& Role(const std::string& name,
                             bool platform_group = false) {
  const std::span<const FixedRole> roles =
      platform_group ? std::span<const FixedRole>(kPlatformRoles)
                     : std::span<const FixedRole>(kRoles);
  for (const auto& role : roles)
    if (name == role.name) return role;
  throw std::runtime_error("unknown fixed role");
}
inline bool ExactGroups(bool platform_group, std::span<const gid_t> groups) {
  return platform_group ? groups.size() == 1 && groups.front() == kPlatformGroup
                        : groups.empty();
}
inline void RequirePlatformGroup(const char* name, gid_t gid) {
  Check(name && std::string_view(name) == "priv_platform" &&
            gid == kPlatformGroup,
        "fixed priv_platform group unavailable/mismatched");
}
inline void PlatformGroupPreflight() {
  // Resolve only in the never-drop trusted coordinator BEFORE scope/spawn.
  // This does not change the image group database or pick a fallback group.
  std::array<char, 16384> storage{};
  struct group value{};
  struct group* found = nullptr;
  Check(getgrnam_r("priv_platform", &value, storage.data(), storage.size(),
                   &found) == 0 &&
            found,
        "fixed priv_platform group lookup");
  RequirePlatformGroup(found->gr_name, found->gr_gid);
}
inline std::string TaskLabel();
inline void VerifyContext(const std::string& label, uid_t uid,
                          bool platform_group = false) {
  uid_t real, effective, saved;
  gid_t rgid, egid, sgid;
  Check(getresuid(&real, &effective, &saved) == 0 && real == uid &&
            effective == uid && saved == uid &&
            getresgid(&rgid, &egid, &sgid) == 0 && rgid == uid && egid == uid &&
            sgid == uid,
        "all role IDs");
  std::array<gid_t, 2> groups{};
  int count = getgroups(static_cast<int>(groups.size()), groups.data());
  Check(count >= 0 && ExactGroups(platform_group,
                                  std::span<const gid_t>(groups.data(), count)),
        "exact role groups");
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  std::array<__user_cap_data_struct, 2> data{};
  Check(syscall(SYS_capget, &header, data.data()) == 0, "capability verify");
  for (const auto& item : data)
    Check(!(item.effective | item.permitted | item.inheritable),
          "retained capability set");
  for (int cap = 0; cap != 64; ++cap) {
    errno = 0;
    int value = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (value < 0 && errno == EINVAL) break;
    Check(value == 0 &&
              prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0) == 0,
          "retained bounding/ambient capability");
  }
  Check(prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1 && TaskLabel() == label,
        "context label/NNP");
}
inline std::string TaskLabel() {
  std::ifstream file("/proc/self/attr/current", std::ios::binary);
  Check(static_cast<bool>(file), "own task label open");
  std::string text((std::istreambuf_iterator<char>(file)), {});
  while (!text.empty() && (text.back() == '\0' || text.back() == '\n'))
    text.pop_back();
  Check(!text.empty() && text.size() <= 255, "own task label bytes");
  return text;
}
inline void Drop(const std::string& label, uid_t uid,
                 bool platform_group = false) {
  // Own-task fixture context only, never peer credential authority.
  int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
  Check(fd >= 0, "own task label writer");
  ssize_t written = write(fd, label.data(), label.size());
  int closed = close(fd);
  Check(written == static_cast<ssize_t>(label.size()) && closed == 0,
        "own task label change");
  for (int cap = 0; cap != 64; ++cap) {
    errno = 0;
    int present = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (present < 0 && errno == EINVAL) break;
    Check(present >= 0 && prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) == 0,
          "bounding capability drop");
  }
  Check(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) == 0,
        "ambient capability clear");
  const gid_t group = kPlatformGroup;
  Check(
      setgroups(platform_group ? 1 : 0, platform_group ? &group : nullptr) == 0,
      "supplementary groups");
  Check(setresgid(uid, uid, uid) == 0 && setresuid(uid, uid, uid) == 0,
        "role IDs drop");
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  std::array<__user_cap_data_struct, 2> data{};
  Check(syscall(SYS_capset, &header, data.data()) == 0,
        "all capability sets clear");
  Check(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0, "no new privileges");
  VerifyContext(label, uid, platform_group);
}
inline void OwnInitialTable(const char*& stage, int code_fd = -1) {
  // Own fixture introspection only; no peer/task authority is derived here.
  for (const char* path : {"/proc/self/task", "/proc/self/fd"}) {
    const bool fd_scan = std::string_view(path) == "/proc/self/fd";
    stage = fd_scan ? "own-fd-scan" : "own-task-scan";
    DIR* scan = opendir(path);
    Check(scan, "own initial scan");
    const int scan_fd = dirfd(scan);
    size_t count = 0, captured = 0;
    std::array<std::array<char, 32>, 16> observed{};
    bool truncated = false;
    bool okay = true;
    int failure = 0;
    for (;;) {
      errno = 0;
      auto* item = readdir(scan);
      if (!item) {
        failure = errno;
        break;
      }
      if (item->d_name[0] == '.') continue;
      if (captured < observed.size()) {
        auto length = strnlen(item->d_name, observed[captured].size());
        if (length == observed[captured].size()) truncated = true;
        memcpy(observed[captured].data(), item->d_name,
               std::min(length, observed[captured].size() - 1));
        ++captured;
      } else {
        truncated = true;
      }
      char* end = nullptr;
      long fd = strtol(item->d_name, &end, 10);
      if (fd_scan) {
        if (fd == scan_fd) continue;
        okay &= end && !*end && fd >= 0 && (fd <= 2 || fd == code_fd);
      }
      ++count;
    }
    errno = 0;
    int closed = closedir(scan);
    int close_errno = closed ? errno : 0;
    const size_t expected = fd_scan ? (code_fd >= 0 ? 4u : 3u) : 1u;
    const bool valid = !failure && !closed && okay && count == expected;
    if (!valid) {
      Json names = Json::array(), details = Json::array();
      for (size_t i = 0; i != captured; ++i) {
        std::string name = observed[i].data();
        names.push_back(name);
        if (!fd_scan || name == std::to_string(scan_fd)) continue;
        // Metadata only; never read, reopen, duplicate or close this own FD.
        int fd = -1;
        auto parsed =
            std::from_chars(name.data(), name.data() + name.size(), fd);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != name.data() + name.size() || fd < 0) {
          details.push_back({{"name", name}, {"numeric_fd", false}});
          continue;
        }
        struct stat info{};
        int stat_result = fstat(fd, &info);
        int stat_errno = stat_result ? errno : 0;
        int descriptor_flags = fcntl(fd, F_GETFD);
        int descriptor_errno = descriptor_flags < 0 ? errno : 0;
        int status_flags = fcntl(fd, F_GETFL);
        int status_errno = status_flags < 0 ? errno : 0;
        std::array<char, 128> target{};
        auto link = std::string("/proc/self/fd/") + name;
        auto size = readlink(link.c_str(), target.data(), target.size());
        int link_errno = size < 0 ? errno : 0;
        details.push_back(
            {{"fd", fd},
             {"stat_errno", stat_errno},
             {"type", stat_result ? 0u : info.st_mode & S_IFMT},
             {"getfd", descriptor_flags},
             {"getfd_errno", descriptor_errno},
             {"getfl", status_flags},
             {"getfl_errno", status_errno},
             {"link_errno", link_errno},
             {"link_truncated", size == static_cast<ssize_t>(target.size())},
             {"target",
              size >= 0 ? std::string(target.data(), size) : "<unavailable>"}});
      }
      std::cerr << "INITIAL_TABLE_REJECT path=" << path
                << " expected=" << expected << " observed_count=" << count
                << " scan_fd=" << scan_fd << " entry_validation=" << okay
                << " readdir_errno=" << failure << " closedir_result=" << closed
                << " closedir_errno=" << close_errno
                << " snapshot_truncated=" << truncated
                << " observed=" << names.dump()
                << " own_fd_metadata=" << details.dump() << std::endl;
    }
    Check(valid, "incomplete/extra initial task or FD table");
  }
  stage = "own-stdio-validation";
  for (int fd = 0; fd != 3; ++fd) {
    struct stat info{};
    Check(!fstat(fd, &info) && !S_ISSOCK(info.st_mode), "stdio absent/socket");
    Check(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "stdio CLOEXEC");
  }
}

}

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_CONTEXT_HH_
