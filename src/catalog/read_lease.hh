// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "catalog/read_access.hh"
#include <memory>
#include <sys/types.h>
namespace capmgr {
// Trusted configuration, never an IPC/application argument. Image provisioning
// must protect normalized path ancestors/mounts from rename and place lock_path
// OUTSIDE the writer-controlled catalog directory. A shared generation lease
// excludes only COOPERATING inode/name/policy changes; ordinary WAL commits and
// checkpoints continue using SQLite locks. Existing writers are not wired here.
struct ReadLeasePolicy {
  std::string directory,lock_path;
  uid_t writer,maintainer;
  gid_t group,lock_group;
  mode_t directory_mode,file_mode,lock_mode;
  std::string directory_label,file_label,lock_label;
};
// Private test seam; production default reads security.SMACK64 from each FD.
// A test substitution does not prove Smack or pathname/ancestor provisioning.
struct ReadLeaseOperations {
  virtual ~ReadLeaseOperations()=default;
  virtual std::string Label(int fd);
};
class CatalogReadLease final:public ReadAccess {
 public:
  // Independently opens O_RDONLY lease description, whole-file OFD RDLCK.
  // Never duplicate/pass a service's lock FD. Busy/unsupported locking denies.
  // All DB/WAL/SHM files must already exist and match the exact supplied policy.
  explicit CatalogReadLease(ReadLeasePolicy,ReadLeaseOperations* operations=nullptr);
  ~CatalogReadLease() override;
  CatalogReadLease(const CatalogReadLease&)=delete;
  CatalogReadLease& operator=(const CatalogReadLease&)=delete;
  const std::string& Path() const noexcept override;
  void Check() override; // caller-serialized; failure permanently poisons admission
  // Fixed CMR1: + five ordered dev/ino pairs (16 lowercase hex digits each).
  // Descriptor is identity only, not authority or a substitute for live handoff.
  std::string Descriptor();
  void MatchDescriptor(std::string_view);
  void Opened(Database&) override; // RO/WAL and resolved-file check before publish
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
