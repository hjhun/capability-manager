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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_ROUTE_OBSERVATION_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_ROUTE_OBSERVATION_HH_

#include <sys/stat.h>
#include <sys/un.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace capmgr::fixture::realpolicy {

struct RouteText {
  std::string bytes;
  bool complete = false;
  nlohmann::json Report() const {
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
      hex += digits[byte >> 4];
      hex += digits[byte & 15];
    }
    return {{"hex", std::move(hex)},
            {"sample_bytes", bytes.size()},
            {"complete", complete}};
  }
};

inline RouteText CaptureRouteText(const char* value) {
  if (!value) return {};
  constexpr size_t limit = 512;
  const auto count = strnlen(value, limit + 1);
  return {std::string(value, std::min(count, limit)), count <= limit};
}

// Only this exact fixed pathname may be inspected. This is not socket access
// authority or the proxy's retained internal path/target observation.
inline bool PermittedRoutePath(const RouteText& path,
                               std::string_view expected) {
  return path.complete && path.bytes == expected && !expected.empty() &&
         expected.front() == '/' &&
         expected.size() < sizeof(sockaddr_un{}.sun_path);
}

inline nlohmann::json RouteMetadata(const RouteText& path,
                                    std::string_view expected,
                                    int (*inspect)(const char*,
                                                   struct stat*) = ::lstat) {
  nlohmann::json result{{"attempted", false}, {"result", nullptr},
                        {"errno", nullptr},   {"dev", nullptr},
                        {"ino", nullptr},     {"uid", nullptr},
                        {"gid", nullptr},     {"mode", nullptr}};
  if (!PermittedRoutePath(path, expected)) return result;
  struct stat info{};
  const int status = inspect(path.bytes.c_str(), &info);
  const int saved_error = status == 0 ? 0 : errno;
  result["attempted"] = true;
  result["result"] = status;
  result["errno"] =
      saved_error;  // This lstat only, never the RPC failure errno.
  if (status == 0) {
    result["dev"] = info.st_dev;
    result["ino"] = info.st_ino;
    result["uid"] = info.st_uid;
    result["gid"] = info.st_gid;
    result["mode"] = info.st_mode;
  }
  return result;
}

// Creation prerequisite only. A named sample is not the stub's retained FD,
// authenticated admission, or authority to modify the sampled socket.
inline bool ReferenceServerSocketReady(const RouteText& path, int api_status,
                                       std::string_view expected,
                                       const nlohmann::json& metadata) {
  return api_status == 0 && PermittedRoutePath(path, expected) &&
         metadata.at("attempted") == true && metadata.at("result") == 0 &&
         metadata.at("errno") == 0 && metadata.at("uid") == 0 &&
         metadata.at("gid") == 0 && metadata.at("mode") == (S_IFSOCK | 0777);
}

}  // namespace capmgr::fixture::realpolicy

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_ROUTE_OBSERVATION_HH_
