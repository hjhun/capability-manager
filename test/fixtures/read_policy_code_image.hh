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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_CODE_IMAGE_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_CODE_IMAGE_HH_

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdexcept>
#include <string>
#include <cstdint>

#include "read_policy_recovery_reference.hh"

namespace capmgr::fixture::realpolicy {

// Takes ownership of an already trusted, pinned code descriptor. The caller
// validates root ownership, exact mode, ACL/caps and fixed path BEFORE transfer.
// This does not establish trust in the dependency closure or loader settings.
class CodeImage {
 public:
  CodeImage(int owned_fd, const struct stat& identity)
      : fd_(owned_fd), identity_(identity) {
    try {
      Validate();
    } catch (...) {
      if (fd_ >= 0) close(fd_);
      fd_ = -1;
      throw;
    }
  }
  ~CodeImage() {
    if (fd_ >= 0) close(fd_);
    // Deliberately no dlclose, including missing-entry/failure paths.
    // Native callbacks, TLS and dependencies may survive until process exit.
  }
  CodeImage(const CodeImage&) = delete;
  CodeImage& operator=(const CodeImage&) = delete;
  void Validate() const {
    struct stat held{};
    const int flags = fcntl(fd_, F_GETFL);
    const bool valid =
        fd_ >= 3 && !fstat(fd_, &held) && S_ISREG(held.st_mode) &&
        held.st_nlink == 1 && held.st_dev == identity_.st_dev &&
        held.st_ino == identity_.st_ino && held.st_uid == identity_.st_uid &&
        held.st_gid == identity_.st_gid && held.st_mode == identity_.st_mode &&
        flags >= 0 && (flags & O_ACCMODE) == O_RDONLY && !(flags & O_PATH) &&
        fcntl(fd_, F_GETFD) == FD_CLOEXEC;
    if (!valid) throw std::runtime_error("code image identity/flags changed");
  }
  template <typename BeforeLoad>
  int Invoke(BeforeLoad&& before_load, const char* kind, const char* root,
             const char* endpoint, const char* role) {
    Validate();
    // Includes complete FD/task/context validation BEFORE any constructors.
    before_load(fd_);
    Validate();
    const auto link = std::string("/proc/self/fd/") + std::to_string(fd_);
    void* module = dlopen(link.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) throw std::runtime_error("fixed native module unavailable");
    // Never release the mapping, even if symbol/close/entry subsequently fails.
    dlerror();
    auto symbol = dlsym(module, "CapmgrRealPolicyFixture");
    const char* error = dlerror();
    if (!symbol || error)
      throw std::runtime_error("fixed native entry missing");
    int owned = fd_;
    fd_ = -1;
    if (close(owned))
      throw std::runtime_error("owned code image close before entry");
    using Entry =
        int (*)(const char*, const char*, const char*, const char*) noexcept;
    return reinterpret_cast<Entry>(symbol)(kind, root, endpoint, role);
  }
  template <typename BeforeLoad>
  int InvokeReference(const RecoveryReference& reference,
                      BeforeLoad&& before_load, const char* kind,
                      const char* root, const char* endpoint) {
    if (fd_ != 3) throw std::runtime_error("fixed reference code slot");
    Validate();
    reference.Validate();
    before_load(fd_);
    Validate();
    reference.Validate();
    void* module = dlopen("/proc/self/fd/3", RTLD_NOW | RTLD_LOCAL);
    if (!module) throw std::runtime_error("fixed reference module unavailable");
    // Keep successful mappings through every later error and kernel exit.
    dlerror();
    auto symbol = dlsym(module, "CapmgrReferenceModuleFixture");
    const char* error = dlerror();
    if (!symbol || error)
      throw std::runtime_error("fixed reference entry missing");
    reference.Validate();
    const int owned = fd_;
    fd_ = -1;
    if (close(owned))
      throw std::runtime_error("reference code close before entry");
    using Entry = int (*)(const char*, const char*, const char*, uint64_t,
                          uint64_t) noexcept;
    const auto& identity = reference.Identity();
    return reinterpret_cast<Entry>(symbol)(kind, root, endpoint,
                                           identity.st_dev, identity.st_ino);
  }

 private:
  int fd_ = -1;
  struct stat identity_{};
};
}  // namespace capmgr::fixture::realpolicy

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_CODE_IMAGE_HH_
