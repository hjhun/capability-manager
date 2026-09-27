// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <span>
#include <vector>
#include <sys/types.h>
namespace capmgr {
// In-process image policy, NEVER decoded from IPC or client argv. This validator
// does not provision policy or prove the image/ancestors/mount provenance. The
// service must run in initial PID/mount namespaces with platform-owned /proc;
// /proc/1 and same-superblock comparisons rely on that verified image premise.
// Required effective/permitted/bounding capabilities must match exactly; all
// inheritable/ambient capabilities are forbidden, securebits=0 and NNP=1 required.
// The fixed image must preserve one-thread behavior structurally after loading;
// a startup task snapshot alone cannot prevent later library thread creation.
// Fixed-image startup only: validates the initial 0..8 layout, then pins both
// namespace objects read from platform PID1 while startup still has permission
// to inspect that label. No namespace FD/PID is accepted from IPC. Capture checks
// they equal this process's namespaces; later checks use the held objects and
// current self namespaces, without reopening cross-label /proc/1 after cap drop.
class WorkerInitialNamespaces {
 public:
  WorkerInitialNamespaces();
  ~WorkerInitialNamespaces();
  WorkerInitialNamespaces(const WorkerInitialNamespaces&) = delete;
  WorkerInitialNamespaces& operator=(const WorkerInitialNamespaces&) = delete;
  std::array<int, 2> Descriptors() const noexcept { return fds_; }
  void ValidateCurrent() const;
  bool Close() noexcept;

 private:
  std::array<int, 2> fds_{-1, -1};
  std::array<dev_t, 2> devices_{};
  std::array<ino_t, 2> inodes_{};
};
struct WorkerBootstrapPolicy {
  uint64_t capabilities;
  std::vector<gid_t> supplementary_groups;
  std::string smack_label;
  WorkerInitialNamespaces& namespaces;
};
// Before loading: exact inherited 0..8 plus captured witnesses, null stdio, independent
// pipes, creator proc-object identity/liveness, same trusted procfs and namespace
// context. Capture marks 3..8 CLOEXEC; Validate verifies it and sets SIGPIPE
// ignored. No jobs may exist yet.
// After loading: recheck creator and fixed root policy plus exactly one task.
// Catalog SQLite handles must have closed before Finish; path/SMACK provenance,
// registry invalidation and executable authorization remain separate gates.
void ValidateWorkerBootstrap(const WorkerBootstrapPolicy&);
// The two captured namespace descriptors are also checked and allowed.
// Finish consumes/closes these witnesses and checks the final FD table again
// before returning. It is a one-shot gate before READY and all job clones.
// Optional exact six aliases come solely from the just-constructed WorkerLoop's
// StartupDescriptors: command/cancel/reply/parent/self/mount. Their inode/type,
// access direction and CLOEXEC are checked, and every other post-load FD rejects.
void FinishWorkerBootstrap(const WorkerBootstrapPolicy&,
                           std::span<const int> loop_fds = {});
}
