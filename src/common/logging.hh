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

#ifndef CAPABILITY_MANAGER_COMMON_LOGGING_HH_
#define CAPABILITY_MANAGER_COMMON_LOGGING_HH_

#include <sstream>
#include <utility>

#ifndef LOG_TAG
#define LOG_TAG "CAPMGR"
#endif

namespace capmgr {

namespace logging {

enum class Level { ERROR, WARNING, INFO, DEBUG };

// Safe C ABI/worker exception boundary; logging failure never replaces cause.
void Failure(const char* operation, const char* cause) noexcept;

class Message {
 public:
  Message(Level level, const char* file, const char* function, int line);
  ~Message() noexcept;
  template <typename T>
  Message& operator<<(const T& value) {
    stream_ << value;
    return *this;
  }

 private:
  Level level_;
  int saved_errno_;
  std::ostringstream stream_;
};

}  // namespace logging

}  // namespace capmgr

#define LOG(LEVEL)                                                      \
  ::capmgr::logging::Message(::capmgr::logging::Level::LEVEL, __FILE__, \
                             __FUNCTION__, __LINE__)

#endif  // CAPABILITY_MANAGER_COMMON_LOGGING_HH_
