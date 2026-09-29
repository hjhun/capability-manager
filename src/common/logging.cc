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

#include "common/logging.hh"

#ifdef CAPMGR_HAVE_DLOG
#include <dlog.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace capmgr {

namespace logging {

Message::Message(Level level, const char* file, const char* function, int line)
    : level_(level), saved_errno_(errno) {
  const char* slash = std::strrchr(file, '/');
  stream_ << (slash ? slash + 1 : file) << ": " << function << "(" << line
          << "): ";
}

Message::~Message() noexcept {
  try {
#ifdef CAPMGR_HAVE_DLOG
    log_priority priority = DLOG_ERROR;
    switch (level_) {
      case Level::ERROR:
        priority = DLOG_ERROR;
        break;
      case Level::WARNING:
        priority = DLOG_WARN;
        break;
      case Level::INFO:
        priority = DLOG_INFO;
        break;
      case Level::DEBUG:
        priority = DLOG_DEBUG;
        break;
    }
    // Fixed format preserves percent characters without treating them as formats.
    dlog_print(priority, LOG_TAG, "%s", stream_.str().c_str());
#else
    // Ordinary host builds have no platform dlog; native AMD builds require it.
    std::fprintf(stderr, "%s: %s\n", LOG_TAG, stream_.str().c_str());
#endif
  } catch (...) {
    // Diagnostics never throw during stack unwinding.
  }
  errno = saved_errno_;
}

void Failure(const char* operation, const char* cause) noexcept {
  const int saved = errno;
  try {
    LOG(ERROR) << operation << ": " << cause;
  } catch (...) {
  }
  errno = saved;
}

}  // namespace logging

}  // namespace capmgr
