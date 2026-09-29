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

// Fixed build-only module; original entry/modes remain unchanged.
#include "../fixtures/read_policy_native_support.hh"

extern "C" __attribute__((visibility("default"))) int CapmgrRealPolicyFixture(
    const char* kind_arg, const char* root_arg, const char* endpoint_arg,
    const char* role_arg) noexcept {
  try {
    Check(kind_arg && root_arg && endpoint_arg && role_arg, "entry arguments");
    std::string kind(kind_arg), root(root_arg), endpoint(endpoint_arg);
    const bool platform_group =
        kind == "platform-server" || kind == "platform-client";
    const auto role = Role(role_arg, platform_group);
    Check(root.starts_with("/opt/usr/capmgr-real-policy-") &&
              endpoint == "d::org.capmgr.realpolicy." +
                              std::to_string(getppid()) + "." + role.name,
          "fixed native arguments");
    if (kind == "server" || kind == "platform-server") {
      uid_t real, effective, saved;
      gid_t rgroup, egroup, sgroup;
      Check(!getresuid(&real, &effective, &saved) && !real && !effective &&
                !saved && !getresgid(&rgroup, &egroup, &sgroup) && !rgroup &&
                !egroup && !sgroup,
            "never-drop root server IDs");
      return Server(root, endpoint, role);
    }
    Check(kind == "client" || kind == "platform-client", "fixed native kind");
    return Client(endpoint, role, false);
  } catch (const std::exception& error) {
    std::cerr << "REAL_GATE_NATIVE_FAIL stage=" << stage << " " << error.what()
              << std::endl;
  } catch (...) {
    std::cerr << "REAL_GATE_NATIVE_FAIL stage=" << stage << " native exception"
              << std::endl;
  }
  return 1;
}
