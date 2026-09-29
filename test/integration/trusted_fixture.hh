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

#ifndef CAPABILITY_MANAGER_TEST_INTEGRATION_TRUSTED_FIXTURE_HH_
#define CAPABILITY_MANAGER_TEST_INTEGRATION_TRUSTED_FIXTURE_HH_

// Root development-fixture checks, not production mount/SMACK provisioning.
// Trusts platform PID1/procfs and excludes a hostile root mount administrator.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <sys/stat.h>
#include <sys/xattr.h>

#include <cerrno>

namespace capmgr::fixture {

inline void Require(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}

inline void PlatformNamespaces() {
  for (const char* kind : {"mnt", "pid"}) {
    struct stat self{}, initial{};
    Require(!stat((std::string("/proc/self/ns/") + kind).c_str(), &self) &&
                !stat((std::string("/proc/1/ns/") + kind).c_str(), &initial) &&
                self.st_dev == initial.st_dev && self.st_ino == initial.st_ino,
            "fixture requires platform PID1 namespaces");
  }
}

inline void NoAcl(const std::string& path) {
  for (const char* name :
       {"system.posix_acl_access", "system.posix_acl_default"}) {
    errno = 0;
    auto size = lgetxattr(path.c_str(), name, nullptr, 0);
    Require(size < 0 && (errno == ENODATA || errno == EOPNOTSUPP),
            "fixture ACL unsupported or check failed");
  }
}

inline void TrustedPath(const std::string& path, bool executable = false) {
  PlatformNamespaces();
  std::filesystem::path name(path);
  Require(name.is_absolute() && name.lexically_normal() == name,
          "fixture absolute normalized path");
  std::filesystem::path current = "/";
  std::vector<std::string> paths{"/"};
  for (const auto& component : name.relative_path()) {
    current /= component;
    paths.push_back(current.string());
  }

  for (size_t i = 0; i < paths.size(); ++i) {
    struct stat info{};
    bool leaf = executable && i + 1 == paths.size();
    Require(!lstat(paths[i].c_str(), &info) && info.st_uid == 0 &&
                !(info.st_mode & 07022) &&
                (leaf ? (S_ISREG(info.st_mode) && info.st_nlink == 1 &&
                         (info.st_mode & 0111))
                      : S_ISDIR(info.st_mode)),
            "unsafe fixture path ownership/type/mode");
    NoAcl(paths[i]);
  }

  std::ifstream mountinfo("/proc/self/mountinfo");
  Require(mountinfo.good(), "fixture mountinfo");
  size_t longest = 0;
  std::string filesystem, source, line;
  while (std::getline(mountinfo, line)) {
    auto split = line.find(" - ");
    Require(split != std::string::npos, "fixture mount record");
    std::istringstream before(line.substr(0, split)),
        after(line.substr(split + 3));
    std::string ignored, point, type, device;
    for (int i = 0; i < 4; ++i) before >> ignored;
    before >> point;
    after >> type >> device;
    if (point == "/" || path == point || path.starts_with(point + "/")) {
      Require(point.find('\\') == std::string::npos,
              "escaped fixture mount path");
      if (point.size() >= longest) {
        longest = point.size();
        filesystem = type;
        source = device;
      }
    }
  }

  Require(!mountinfo.bad() && longest && filesystem == "ext4" &&
              source.starts_with("/dev/"),
          "fixture requires platform local ext4 mount");
}
}  // namespace capmgr::fixture

#endif  // CAPABILITY_MANAGER_TEST_INTEGRATION_TRUSTED_FIXTURE_HH_
