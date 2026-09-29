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

#ifndef CAPABILITY_MANAGER_LAUNCHER_WORKER_CATALOG_LEASE_HH_
#define CAPABILITY_MANAGER_LAUNCHER_WORKER_CATALOG_LEASE_HH_

#include "catalog/read_lease.hh"
#include "launcher/worker_catalog.hh"

namespace capmgr {

// Startup snapshot with the SAME independent generation lease as its physically
// closed RO connection. Move-only; no assignment may retire a live worker lease.
// This freezes generation maintenance, NOT WAL contents or executable authority.
// A worker owner must keep this value through closed admission, output drain and
// owned-child cleanup. Bootstrap/worker lifetime wiring is a separate increment.
// An inherited closed snapshot may close its own references only, never F_UNLCK;
// another child's retained reference can keep EX maintenance busy until exit.
class LeasedWorkerCatalogSnapshot final {
 public:
  ~LeasedWorkerCatalogSnapshot();
  LeasedWorkerCatalogSnapshot(LeasedWorkerCatalogSnapshot&&) noexcept;
  LeasedWorkerCatalogSnapshot& operator=(LeasedWorkerCatalogSnapshot&&) =
      delete;
  LeasedWorkerCatalogSnapshot(const LeasedWorkerCatalogSnapshot&) = delete;
  LeasedWorkerCatalogSnapshot& operator=(const LeasedWorkerCatalogSnapshot&) =
      delete;
  uint64_t Revision() const noexcept { return snapshot_.revision; }
  const WorkerRegistry& Registry() const noexcept { return snapshot_.registry; }

 private:
  friend class WorkerCatalogReader;
  friend class LeasedWorkerLoop;
  LeasedWorkerCatalogSnapshot(WorkerCatalogSnapshot&&,
                              std::unique_ptr<CatalogReadLease>&&) noexcept;
  WorkerCatalogSnapshot snapshot_;
  std::unique_ptr<CatalogReadLease> lease_;
};

// Caller-serialized private startup facade, never available to a plugin/IPC
// request. Borrowed directory is readable/non-O_PATH and remains exclusively
// stable through construction; the concrete lease independently opens its own
// directory/lock/data pins. Trusted normalized path ancestry, procfs and image
// provenance remain prerequisites. No hostile rename/FD-table ABA protection.
// No connection, statement, blob, backup or general ReadAccess escapes. READONLY
// without CREATE; no migration, sidecar recreation, immutable or live job lookup.
// Finish reads schema/revision/published <=256 CLI entries in one RO transaction,
// materializes the owning registry and physically closes SQLite before returning.
// SQLITE_BUSY/error retains connection AND lease for Close/Finish retry. Close
// can abandon a failed load; it never releases the lease ahead of physical close.
// Creator TGID precedes every SQLite use and exception cleanup. Forked operations
// reject; inherited reader destruction fail-stops BEFORE SQLite (child exec/_exit
// required). Trusted no-escape close invariant failure at destruction also
// fail-stops; ordinary metadata/schema/content/acquisition failures do not.
class WorkerCatalogReader final {
 public:
  WorkerCatalogReader(int trusted_directory, ReadLeasePolicy,
                      ReadLeaseOperations* operations = nullptr);
  ~WorkerCatalogReader();
  WorkerCatalogReader(const WorkerCatalogReader&) = delete;
  WorkerCatalogReader& operator=(const WorkerCatalogReader&) = delete;
  WorkerCatalogReader(WorkerCatalogReader&&) = delete;
  WorkerCatalogReader& operator=(WorkerCatalogReader&&) = delete;
  LeasedWorkerCatalogSnapshot Finish();
  void Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_WORKER_CATALOG_LEASE_HH_
