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

#include "amd-module/module_config.hh"

#include <set>

namespace capmgr {

AmdModuleConfig ParseAmdModuleConfig(const Json& value) {
  if (!value.is_object() || !value.contains("enabled") ||
      !value["enabled"].is_boolean())
    throw Error(ErrorCode::kInvalid,
                "AMD config requires a boolean enabled field");
  AmdModuleConfig result;
  result.enabled = value["enabled"].get<bool>();
  std::set<std::string> keys;
  for (auto it = value.begin(); it != value.end(); ++it) keys.insert(it.key());
  const std::set<std::string> expected =
      result.enabled ? std::set<std::string>{"enabled", "directoryLabel",
                                             "fileLabel", "lockLabel"}
                     : std::set<std::string>{"enabled"};
  if (keys != expected)
    throw Error(ErrorCode::kInvalid, "Unexpected AMD configuration fields");
  if (!result.enabled) return result;
  auto label = [&](const char* field) {
    if (!value[field].is_string())
      throw Error(ErrorCode::kInvalid, "AMD labels must be strings");
    auto text = value[field].get<std::string>();
    if (text.empty() || text.size() > 255)
      throw Error(ErrorCode::kInvalid, "AMD label length invalid");
    for (unsigned char byte : text)
      if (byte <= 32 || byte >= 127 || byte == '/' || byte == 92 ||
          byte == 34 || byte == 39)
        throw Error(ErrorCode::kInvalid,
                    "AMD label contains forbidden characters");
    return text;
  };
  result.directory_label = label("directoryLabel");
  result.file_label = label("fileLabel");
  result.lock_label = label("lockLabel");
  return result;
}

}  // namespace capmgr
