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

#ifndef CAPABILITY_MANAGER_COMMON_ERROR_HH_
#define CAPABILITY_MANAGER_COMMON_ERROR_HH_

#include <stdexcept>
#include <string>

namespace capmgr {

enum class ErrorCode : int {
  kInvalid = -1,
  kPermission = -2,
  kDatabase = -3,
  kNotFound = -4,
  kConflict = -5,
  kUnsupported = -6,
  kBusy = -7,
  kLimit = -8,
  kIo = -9,
  kNoMemory = -10
};

class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}
  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_COMMON_ERROR_HH_
