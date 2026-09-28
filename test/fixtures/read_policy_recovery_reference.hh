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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_RECOVERY_REFERENCE_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_RECOVERY_REFERENCE_HH_

#include <cerrno>
#include <fcntl.h>
#include <initializer_list>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <stdexcept>

namespace capmgr::fixture::realpolicy {
// Private fixed-image fixture only. Expected identity comes from the trusted
// never-drop coordinator's validated durable journal, never a client/PID/path.
// Metadata is provenance, NOT evidence that flock SH is held or who holds it.
inline bool MatchesRecoveryReference(const struct stat& held,
                                     const struct stat& expected, int flags,
                                     int descriptor_flags, bool cloexec) {
  return S_ISREG(expected.st_mode) && expected.st_uid == 0 &&
         expected.st_gid == 0 && expected.st_nlink == 1 &&
         (expected.st_mode & 07777) == 0600 && held.st_dev == expected.st_dev &&
         held.st_ino == expected.st_ino && held.st_uid == expected.st_uid &&
         held.st_gid == expected.st_gid && held.st_mode == expected.st_mode &&
         held.st_nlink == 1 && flags >= 0 && !(flags & O_PATH) &&
         (flags & O_ACCMODE) == O_RDWR &&
         descriptor_flags == (cloexec ? FD_CLOEXEC : 0);
}
// Borrows fixed FD4 without dup/close/unlock, including destruction/failure.
// Role code must retain it through library/TLS cleanup to kernel process exit.
// Failed marking may leave CLOEXEC set; startup must fail, never retry/rollback.
// Initial mapping is inherited non-CLOEXEC; before loading, mark CLOEXEC then
// revalidate. No post-load exec/fork/credential transition is permitted.
// No journal contents/directory/plan or catalog resource is exposed by this type.
class RecoveryReference final {
 public:
  static constexpr int kDescriptor = 4;
  explicit RecoveryReference(const struct stat& expected)
      : expected_(expected) {
    Validate(false);
  }
  ~RecoveryReference() = default;  // Deliberately does not close FD4.
  RecoveryReference(const RecoveryReference&) = delete;
  RecoveryReference& operator=(const RecoveryReference&) = delete;
  RecoveryReference(RecoveryReference&&) = delete;
  RecoveryReference& operator=(RecoveryReference&&) = delete;
  void MarkCloexec() const {
    Validate(false);
    if (fcntl(kDescriptor, F_SETFD, FD_CLOEXEC))
      throw std::runtime_error("recovery reference CLOEXEC failed");
    Validate(true);
  }
  void Validate(bool cloexec = true) const {
    struct stat held{};
    const int flags = fcntl(kDescriptor, F_GETFL);
    const int descriptor_flags = fcntl(kDescriptor, F_GETFD);
    if (fstat(kDescriptor, &held) ||
        !MatchesRecoveryReference(held, expected_, flags, descriptor_flags,
                                  cloexec))
      throw std::runtime_error("fixed recovery reference identity/flags");
    for (const char* name :
         {"system.posix_acl_access", "system.posix_acl_default"}) {
      errno = 0;
      if (fgetxattr(kDescriptor, name, nullptr, 0) >= 0 ||
          (errno != ENODATA && errno != ENOTSUP))
        throw std::runtime_error("fixed recovery reference ACL unavailable");
    }
  }

 private:
  const struct stat expected_;
};
}

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_RECOVERY_REFERENCE_HH_
