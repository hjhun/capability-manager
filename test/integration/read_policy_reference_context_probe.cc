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

// Build-only fixed-reference own-context diagnostic; no module or policy calls.
#include "../fixtures/read_policy_context.hh"
#include "trusted_fixture.hh"

#include <charconv>
#include <limits>

using namespace capmgr::fixture::realpolicy;

namespace {

const char* stage = "fixed-reference-startup";
uint64_t Number(const char* text) {
  const std::string_view value(text);
  uint64_t result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  Check(!value.empty() && parsed.ec == std::errc{} &&
            parsed.ptr == value.data() + value.size(),
        "fixed identity number");
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    // These identity values are trusted private coordinator setup, never public
    // API/IPC inputs. The fixed borrowed descriptor is never opened from argv.
    Check(argc == 5 && std::string_view(argv[1]) == "--reference-context",
          "fixed diagnostic arguments");
    const std::string_view role = argv[2];
    Check(role == "system301-platform" || role == "root-user-shell",
          "fixed reference role");
    uid_t real, effective, saved;
    gid_t rgid, egid, sgid;
    Check(!getresuid(&real, &effective, &saved) && !real && !effective &&
              !saved && !getresgid(&rgid, &egid, &sgid) && !rgid && !egid &&
              !sgid,
          "reference fixture startup root IDs");
    capmgr::fixture::TrustedPath(Self(), true);
    const auto dev = Number(argv[3]), ino = Number(argv[4]);
    Check(dev <= std::numeric_limits<dev_t>::max() &&
              ino <= std::numeric_limits<ino_t>::max(),
          "fixed identity range");
    struct stat expected{};
    expected.st_dev = static_cast<dev_t>(dev);
    expected.st_ino = static_cast<ino_t>(ino);
    expected.st_uid = expected.st_gid = 0;
    expected.st_nlink = 1;
    expected.st_mode = S_IFREG | 0600;
    RecoveryReference reference(expected);
    reference.MarkCloexec();
    OwnInitialTable(stage, -1, &reference);
    if (role == "system301-platform") {
      stage = "reference-own-drop";
      Drop("System", 301, true);
      VerifyContext("System", 301, true);
    } else {
      Check(TaskLabel() == "User::Shell", "fixed never-drop server label");
    }
    stage = "reference-after-context";
    // Permission failure is a SETUP FAIL, never a reason for policy/ACL repair.
    reference.Validate();
    OwnInitialTable(stage, -1, &reference);
    std::cout << "REFERENCE_CONTEXT_VALIDATED role=" << role
              << " retained_fd4=1 module_opened=0" << std::endl;
    return 0;  // FD4 stays open through destruction/TLS to kernel process exit.
  } catch (const std::exception& error) {
    std::cerr << "REFERENCE_CONTEXT_FAIL stage=" << stage << " " << error.what()
              << std::endl;
    return 1;  // Reference also stays open through this failure return.
  }
}
