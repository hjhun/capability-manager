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

#ifndef CAPABILITY_MANAGER_CATALOG_READ_LEASE_HH_
#define CAPABILITY_MANAGER_CATALOG_READ_LEASE_HH_

#include "catalog/read_access.hh"
#include <memory>
#include <sys/types.h>
namespace capmgr {
// Trusted configuration, never an IPC/application argument. Image provisioning
// must protect normalized path ancestors/mounts from rename and place lock_path
// OUTSIDE the writer-controlled catalog directory. A shared generation lease
// excludes only COOPERATING inode/name/policy changes; ordinary WAL commits and
// checkpoints continue using SQLite locks. CoordinatedCatalogWriter adopts this lock; raw legacy/external
// writers remain outside the cooperative contract.
struct ReadLeasePolicy {
  std::string directory, lock_path;
  uid_t writer, maintainer;
  gid_t group, lock_group;
  mode_t directory_mode, file_mode, lock_mode;
  std::string directory_label, file_label, lock_label;
};
// Private test seam; production default reads security.SMACK64 from each FD.
// A test substitution does not prove Smack or pathname/ancestor provisioning.
struct ReadLeaseOperations {
  virtual ~ReadLeaseOperations() = default;
  virtual std::string Label(int fd);
};
class CatalogReadLease final : public ReadAccess {
 public:
  // Independently opens O_RDONLY lease description, whole-file OFD RDLCK.
  // Never duplicate/pass a service's lock FD. Busy/unsupported locking denies.
  // DB/WAL/SHM are O_PATH metadata pins, not permission/SQLite IO proof.
  // Owned self-FD metadata assumes trusted procfs; peer credentials do not use it.
  // All DB/WAL/SHM files must already exist and match the exact supplied policy.
  explicit CatalogReadLease(ReadLeasePolicy,
                            ReadLeaseOperations* operations = nullptr);
  ~CatalogReadLease() override;
  CatalogReadLease(const CatalogReadLease&) = delete;
  CatalogReadLease& operator=(const CatalogReadLease&) = delete;
  const std::string& Path() const noexcept override;
  void Check()
      override;  // caller-serialized; failure permanently poisons admission
  // Fixed CMR1: + five ordered dev/ino pairs (16 lowercase hex digits each).
  // Descriptor is identity only, not authority or a substitute for live handoff.
  std::string Descriptor();
  void MatchDescriptor(std::string_view);
  void Opened(
      Database&) override;  // RO/WAL and resolved-file check before publish
 private:
  friend class WorkerCatalogReader;
  // Startup owner only; borrowed readable directory remains stable throughout.
  void MatchDirectory(int);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}

#endif  // CAPABILITY_MANAGER_CATALOG_READ_LEASE_HH_
