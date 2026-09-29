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

#include "../fixtures/read_policy_recovery_reference.hh"

#include <gtest/gtest.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <string_view>

namespace {

using capmgr::fixture::realpolicy::MatchesRecoveryReference;
using capmgr::fixture::realpolicy::RecoveryReference;
struct stat Identity() {
  struct stat info{};
  info.st_dev = 13;
  info.st_ino = 37;
  info.st_uid = info.st_gid = 0;
  info.st_nlink = 1;
  info.st_mode = S_IFREG | 0600;
  return info;
}

TEST(ReadPolicyRecoveryReference, ExactIdentityAndOnlyInheritedThenCloexec) {
  const auto info = Identity();
  EXPECT_TRUE(MatchesRecoveryReference(info, info, O_RDWR, 0, false));
  EXPECT_TRUE(MatchesRecoveryReference(info, info, O_RDWR, FD_CLOEXEC, true));
  EXPECT_FALSE(MatchesRecoveryReference(info, info, O_RDWR, 0, true));
  EXPECT_FALSE(MatchesRecoveryReference(info, info, O_RDWR, FD_CLOEXEC, false));
}

TEST(ReadPolicyRecoveryReference, RejectsMissingWrongAccessAndPathDescriptors) {
  const auto info = Identity();
  for (int flags : {-1, O_RDONLY, O_WRONLY, O_PATH})
    EXPECT_FALSE(MatchesRecoveryReference(info, info, flags, FD_CLOEXEC, true));
  EXPECT_FALSE(MatchesRecoveryReference(info, info, O_RDWR, -1, true));
}

TEST(ReadPolicyRecoveryReference, RejectsSubstitutionOrChangedMetadata) {
  const auto info = Identity();
  for (int field = 0; field != 7; ++field) {
    auto changed = info;
    if (field == 0) ++changed.st_dev;
    if (field == 1) ++changed.st_ino;
    if (field == 2) ++changed.st_uid;
    if (field == 3) ++changed.st_gid;
    if (field == 4) ++changed.st_nlink;
    if (field == 5) changed.st_mode |= 0040;
    if (field == 6) changed.st_nlink = 0;
    EXPECT_FALSE(MatchesRecoveryReference(changed, info, O_RDWR, 0, false));
  }
}

TEST(ReadPolicyRecoveryReference, TrustedExpectedMetadataMustAlsoBeExact) {
  for (int field = 0; field != 6; ++field) {
    auto bad = Identity();
    if (field == 0) bad.st_uid = 301;
    if (field == 1) bad.st_gid = 10212;
    if (field == 2) bad.st_nlink = 2;
    if (field == 3) bad.st_mode = S_IFDIR | 0600;
    if (field == 4) bad.st_mode = S_IFLNK | 0600;
    if (field == 5) bad.st_mode |= S_ISUID;
    EXPECT_FALSE(MatchesRecoveryReference(bad, bad, O_RDWR, 0, false));
  }
}
// Actual root-file tests are opt-in in a standalone process. No role/drop/module,
// SQLite, policy, actual recovery CLI or production path is reached.
void Isolated(bool corrupt_identity) {
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    char path[] = "/tmp/capmgr-recovery-reference-XXXXXX";
    int original = mkstemp(path);
    if (original >= 0) fprintf(stderr, "OWNED_REFERENCE_FILE=%s\n", path);
    if (original < 0 || fchmod(original, 0600) || fchown(original, 0, 0))
      _exit(81);
    struct stat expected{};
    if (fstat(original, &expected) || flock(original, LOCK_SH | LOCK_NB))
      _exit(82);
    const int stable = fcntl(original, F_DUPFD_CLOEXEC, 5);
    if (stable < 5 || close(original) || dup2(stable, 4) != 4 || close(stable))
      _exit(83);
    const int independent = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (independent < 0) _exit(84);
    struct stat actual{};
    if (fstat(4, &actual) ||
        !MatchesRecoveryReference(actual, expected, fcntl(4, F_GETFL),
                                  fcntl(4, F_GETFD), false)) {
      fprintf(stderr, "RETAINED_REFERENCE_FILE=%s\n", path);
      _exit(86);
    }
    bool okay = true;
    if (corrupt_identity) {
      ++expected.st_ino;
      try {
        RecoveryReference rejected(expected);
        okay = false;
      } catch (const std::runtime_error&) {
      }
    } else {
      try {
        {
          RecoveryReference retained(expected);
          retained.MarkCloexec();
          retained.Validate();
        }  // Destruction must not close or unlock the inherited description.
        okay = fcntl(4, F_GETFD) == FD_CLOEXEC;
      } catch (...) {
        okay = false;
      }
    }
    errno = 0;
    okay = okay && fcntl(4, F_GETFD) >= 0 &&
           flock(independent, LOCK_EX | LOCK_NB) == -1 &&
           (errno == EWOULDBLOCK || errno == EAGAIN);
    // Only test cleanup closes4. Real fixed roles retain4 until kernel exit.
    okay = close(4) == 0 && okay;
    okay = flock(independent, LOCK_EX | LOCK_NB) == 0 && okay;
    struct stat named{};
    const bool same_path = !lstat(path, &named) && S_ISREG(named.st_mode) &&
                           named.st_dev == actual.st_dev &&
                           named.st_ino == actual.st_ino && named.st_uid == 0 &&
                           named.st_nlink == 1 &&
                           (named.st_mode & 07777) == 0600;
    const bool removed = same_path && unlink(path) == 0;
    fprintf(stderr, "%s_REFERENCE_FILE=%s\n", removed ? "REMOVED" : "RETAINED",
            path);
    okay = removed && okay;
    okay = close(independent) == 0 && okay;
    _exit(okay ? 0 : 85);
  }
  int status = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  pid_t observed;
  do {
    observed = waitpid(child, &status, WNOHANG);
    if (observed == 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (observed == 0 && std::chrono::steady_clock::now() < end);
  if (observed == 0) {
    // Exclusive unreaped direct child. Kill is not the asserted successful result.
    kill(child, SIGKILL);
    ASSERT_EQ(waitpid(child, &status, 0), child);
    FAIL() << "reference fixture timed out";
    return;
  }

  ASSERT_EQ(observed, child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ReadPolicyRecoveryReference,
     RootReferenceDestructionDoesNotCloseOrUnlock) {
  if (geteuid() != 0 || !getenv("CAPMGR_RECOVERY_REFERENCE_TESTS") ||
      std::string_view(getenv("CAPMGR_RECOVERY_REFERENCE_TESTS")) != "1")
    GTEST_SKIP() << "Explicit standalone root reference test required";
  Isolated(false);
}

TEST(ReadPolicyRecoveryReference, RootFailedValidationLeavesReferenceOpen) {
  if (geteuid() != 0 || !getenv("CAPMGR_RECOVERY_REFERENCE_TESTS") ||
      std::string_view(getenv("CAPMGR_RECOVERY_REFERENCE_TESTS")) != "1")
    GTEST_SKIP() << "Explicit standalone root reference test required";
  Isolated(true);
}
}  // namespace
