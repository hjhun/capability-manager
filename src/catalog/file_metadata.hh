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

#ifndef CAPABILITY_MANAGER_CATALOG_FILE_METADATA_HH_
#define CAPABILITY_MANAGER_CATALOG_FILE_METADATA_HH_

#include <cstddef>
#include <sys/types.h>

namespace capmgr {
// Metadata only. Probe on a DIRECTORY before ever opening a SQLite data inode;
// kernels silently ignoring O_PATH must fail before an ordinary data FD exists.
void RequireDataPinSupport(int directory);
void ValidateDataPin(int fd);
// For owned O_PATH regular single-link CLOEXEC pins, uses getxattr through our
// own trusted /proc/self/fd magic link. Never opens an ordinary data descriptor.
// This is metadata object access, never peer/task credential extraction.
ssize_t MetadataAttribute(int fd, const char* name, void* value, size_t size);
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_CATALOG_FILE_METADATA_HH_
