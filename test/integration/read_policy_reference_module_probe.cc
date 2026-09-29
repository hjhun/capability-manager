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

// Minimal fixed-role launcher; no platform constructor before Drop proof.
#include "trusted_fixture.hh"
#include "../fixtures/read_policy_context.hh"
#include "../fixtures/read_policy_code_image.hh"
#include "../fixtures/read_policy_survivor_hold.hh"
#include "../fixtures/read_policy_socket_creation.hh"

#include <charconv>
#include <limits>

#ifndef CAPMGR_REFERENCE_NATIVE_IMAGE
#error "Fixed reference module path required"
#endif
using namespace capmgr::fixture::realpolicy;
namespace {

const char* stage = "reference-module-startup";

uint64_t Number(const char* text) {
  const std::string_view value(text);
  uint64_t result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  Check(!value.empty() && parsed.ec == std::errc{} &&
            parsed.ptr == value.data() + value.size(),
        "fixed reference number");
  return result;
}
CodeImage OpenCodeImage() {
  capmgr::fixture::TrustedPath(CAPMGR_REFERENCE_NATIVE_IMAGE, true);
  int fd =
      open(CAPMGR_REFERENCE_NATIVE_IMAGE, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  try {
    struct stat info{};
    Check(fd == 3 && !fstat(fd, &info) && S_ISREG(info.st_mode) &&
              info.st_uid == 0 && info.st_gid == 0 && info.st_nlink == 1 &&
              (info.st_mode & 07777) == 0755,
          "unsafe fixed code image");
    for (const char* name :
         {"system.posix_acl_access", "system.posix_acl_default",
          "security.capability"}) {
      errno = 0;
      auto size = fgetxattr(fd, name, nullptr, 0);
      Check(size < 0 && (errno == ENODATA || errno == EOPNOTSUPP),
            "code image ACL/cap metadata");
    }
    int transferred = fd;
    fd = -1;
    return CodeImage(transferred, info);
  } catch (...) {
    if (fd >= 0) close(fd);
    throw;
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Check(argc == 7 && std::string_view(argv[1]) == "--reference-module",
          "fixed reference module arguments");
    const std::string kind(argv[2]), root(argv[3]), endpoint(argv[4]);
    Check(kind == "server-hold" || kind == "reader-server" ||
              kind == "reader-hold",
          "fixed reference module mode");
    Check(ReferenceModuleTopology(root, endpoint, getppid()),
          "fixed reference topology");
    uid_t r, e, s;
    gid_t rg, eg, sg;
    Check(!getresuid(&r, &e, &s) && !r && !e && !s &&
              !getresgid(&rg, &eg, &sg) && !rg && !eg && !sg,
          "fixed reference startup root IDs");
    capmgr::fixture::TrustedPath(Self(), true);
    capmgr::fixture::TrustedPath(root);
    struct stat directory{};
    Check(!lstat(root.c_str(), &directory) && S_ISDIR(directory.st_mode) &&
              directory.st_uid == 0 && directory.st_gid == 0 &&
              (directory.st_mode & 07777) == 0700,
          "fixed reference scope");
    const auto dev = Number(argv[5]), ino = Number(argv[6]);
    Check(dev <= std::numeric_limits<dev_t>::max() &&
              ino <= std::numeric_limits<ino_t>::max(),
          "fixed reference identity range");
    struct stat expected{};
    expected.st_dev = dev;
    expected.st_ino = ino;
    expected.st_uid = expected.st_gid = 0;
    expected.st_mode = S_IFREG | 0600;
    expected.st_nlink = 1;
    RecoveryReference reference(expected);
    reference.MarkCloexec();
    OwnInitialTable(stage, -1, &reference);  // exactly stdio+4;3 MUST be absent
    if (kind == "reader-hold") {
      PlatformGroupPreflight();
      OwnInitialTable(stage, -1,
                      &reference);  // NSS must not retain endpoints/tasks
    } else
      Check(TaskLabel() == "User::Shell", "fixed root server subject");
    auto code =
        OpenCodeImage();  // owned3; opened while privileged, before Drop
    if (kind == "reader-hold") {
      stage = "reference-client-drop";
      Drop("System", 301, true);
      VerifyContext("System", 301, true);
    }
    return code.InvokeReference(
        reference,
        [&](int fd) {
          if (kind == "reader-hold")
            VerifyContext("System", 301, true);
          else {
            Check(!getresuid(&r, &e, &s) && !r && !e && !s &&
                      !getresgid(&rg, &eg, &sg) && !rg && !eg && !sg &&
                      TaskLabel() == "User::Shell",
                  "never-drop server identity");
          }
          OwnInitialTable(stage, fd,
                          &reference);  // exact0..4 before constructors
          if (kind != "reader-hold") {
            stage = "reference-server-creation-mask";
            EstablishReferenceServerCreationMask();
          }
        },
        kind.c_str(), root.c_str(), endpoint.c_str());
  } catch (const std::exception& error) {
    std::cerr << "REFERENCE_MODULE_FAIL stage=" << stage << " " << error.what()
              << std::endl;
    return 1;  // Borrowed4 remains through destruction/TLS/kernel exit.
  }
}
