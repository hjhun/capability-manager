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

#ifndef CAPABILITY_MANAGER_LAUNCHER_WORKER_SUPERVISOR_HH_
#define CAPABILITY_MANAGER_LAUNCHER_WORKER_SUPERVISOR_HH_

#include "launcher/worker_session.hh"

namespace capmgr {

// Fixed 32-byte bootstrap record: CWB1, LE version1(u16), length32(u16),
// generation(u64), observed catalog revision(u64 <=INT64_MAX), reserved zero(u64).
// Worker writes exactly once and closes the separate pipe BEFORE job admission;
// bootstrap completion is not a catalog validity lease or authorization proof.
std::array<uint8_t, 32> EncodeWorkerReady(uint64_t generation,
                                          uint64_t revision);
// Private frontend coordinator consuming sole ownership of a WorkerSession.
// BeginGeneration already persisted before fixed-image spawn. The direct worker
// token comes ONLY from SpawnFixedWorker/OwnedChildren, never from IPC. Caller
// retains the table until all children reaped (including every failure path);
// this class does not abandon/kill a worker or prove namespace-child absence.
// Root frontend activation, fixed FD8 mapping, bootstrap validation and source
// revision invalidation are not enabled by this standalone integration class.
class WorkerSupervisor {
 public:
  using Clock = WorkerSession::Clock;
  WorkerSupervisor(std::unique_ptr<WorkerSession>, OwnedChildren&,
                   uint64_t worker, int ready_read,
                   Clock::time_point now = Clock::now());
  ~WorkerSupervisor();
  WorkerSupervisor(const WorkerSupervisor&) = delete;
  WorkerSupervisor& operator=(const WorkerSupervisor&) = delete;
  // Internally serialized. No callbacks. Destructor requires caller quiescence.
  // PollStartup reads <=33 bytes per call; exact EOF is required. Five seconds
  // covers even zero bytes / a retained writer. Owned child must still be live.
  bool PollStartup(Clock::time_point now = Clock::now());
  uint64_t CatalogRevision() const;
  uint64_t Start(const std::string& request);  // READY + fresh owned-live check
  void Cancel(uint64_t token);
  std::optional<WorkerEvent> Step(Clock::time_point now = Clock::now());
  void PrepareStop();        // no outstanding job/transport record
  bool ConfirmNormalExit();  // nonblocking owned exit0+clean Session EOF proof
  void Abort() noexcept;
  bool Failed() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_WORKER_SUPERVISOR_HH_
