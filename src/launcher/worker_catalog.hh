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
// SPDX-License-Identifier: Apache-2.0

#ifndef CAPABILITY_MANAGER_LAUNCHER_WORKER_CATALOG_HH_
#define CAPABILITY_MANAGER_LAUNCHER_WORKER_CATALOG_HH_

#include "launcher/worker_loop.hh"

namespace capmgr {

struct WorkerCatalogFilePolicy {
  uid_t writer;
  gid_t group;
  mode_t directory_mode;  // exact0700/0750/2750
  mode_t file_mode;       // exact0600/0640 for DB/WAL/SHM
};

struct WorkerCatalogSnapshot {
  uint64_t revision;
  WorkerRegistry registry;
};

// Startup-only private snapshot loader, NEVER call with a live worker job.
// Directory FD is borrowed from the trusted fixed frontend, not an IPC client.
// Caller keeps that FD exclusively stable/open through validation and dup;
// matching the duplicate is not protection against malicious FD-table reuse.
// Requires an image-proven procfs /proc/self/fd and prevalidated directory/mount
// provenance AND stable trusted pathname ancestors during startup. Pins the
// directory and three existing files (O_PATH metadata pins); checks DAC/ACL,
// identity and schema. Metadata pins do not prove read permission; the actual
// SQLite RO open/queries are required. SQLite
// may canonicalize procfd input into a normal path: this is NOT an FD-only VFS or
// proof against hostile rename/ABA. Its reported DB/WAL/SHM paths are checked
// against the pinned files; protection from concurrent hostile path substitution
// remains a caller/image prerequisite. No immutable or inherited SQLite handle.
// Caller supplies the provisioned writer/group/modes (the live writer may be
// app_fw, not root). Directory and all sidecars must match that exact identity;
// only owner write is allowed. This performs DAC/ACL checks only. Distinct job
// SMACK policy denying writer access, ancestor/mount trust, source registration,
// <=256 subset selection and revision invalidation still gate production reuse.
// Policy is trusted internal configuration, never supplied by an IPC request.
WorkerCatalogSnapshot LoadWorkerCatalog(int trusted_directory,
                                        const WorkerCatalogFilePolicy&);
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_WORKER_CATALOG_HH_
