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

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>

#ifndef CAPMGR_LOADER_MISSING_ENTRY
extern "C" __attribute__((visibility("default"))) int CapmgrRealPolicyFixture(
    const char*, const char*, const char*, const char* descriptor) noexcept {
  char* end = nullptr;
  long fd = strtol(descriptor, &end, 10);
  errno = 0;
  return end && !*end && fd >= 3 && fcntl(fd, F_GETFD) == -1 && errno == EBADF
             ? 27
             : 91;
}
#else
extern "C" __attribute__((visibility("default"))) int UnrelatedFixedEntry() {
  return 99;
}
#endif
