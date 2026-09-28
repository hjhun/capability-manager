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

#ifndef CAPABILITY_MANAGER_LAUNCHER_LEASED_WORKER_LOOP_HH_
#define CAPABILITY_MANAGER_LAUNCHER_LEASED_WORKER_LOOP_HH_

#include "launcher/worker_bootstrap.hh"
#include "launcher/worker_catalog_lease.hh"

#include <optional>

namespace capmgr {
// Private fixed-image owner, never exposed to IPC. Production admission remains
// gated on executable authority, revision invalidation and crash maintenance.
// The same physically closed snapshot/lease remains inseparable from this loop.
// Factory Validate precedes acquisition; owner-bound Finish precedes all Step.
// Exclusive descriptor table ownership and no unlock/conversion are prerequisites;
// startup metadata cannot prove OFD-description identity against hostile reuse.
// Caller keeps Runtime alive. No synchronous metadata/SQL work is added to Step.
class LeasedWorkerLoop final {
 public:
  static std::unique_ptr<LeasedWorkerLoop> LoadAndFinish(
      const WorkerBootstrapPolicy&, const WorkerContext&, ReadLeasePolicy,
      WorkerRuntime&, WorkerLimits = {});
  ~LeasedWorkerLoop();
  LeasedWorkerLoop(const LeasedWorkerLoop&) = delete;
  LeasedWorkerLoop& operator=(const LeasedWorkerLoop&) = delete;
  LeasedWorkerLoop(LeasedWorkerLoop&&) = delete;
  LeasedWorkerLoop& operator=(LeasedWorkerLoop&&) = delete;

  void Step(WorkerLoop::Clock::time_point now = WorkerLoop::Clock::now());
  void Shutdown();
  bool AdmissionOpen() const;
  bool Quiescent() const;
  bool CanExitCleanly() const;
  bool DeliveryLost() const;
  size_t Jobs() const;
  uint64_t Revision() const;
  // Both terminal operations destroy the loop before releasing its lease.
  // Normal: CanExitCleanly, not merely Quiescent/no children/Shutdown.
  void RetireCleanly();
  // Abnormal LOCAL retirement: lost delivery and confirmed owned-child cleanup.
  // Undelivered job tokens may remain. Never durable Complete, clean exit0,
  // frontend reservation release or persistent no-old-job proof.
  void RetireAfterDeliveryLoss();

 private:
  friend class LeasedWorkerLoopTestAccess;  // Defined only in the test TU.
  friend void FinishWorkerBootstrap(const WorkerBootstrapPolicy&,
                                    LeasedWorkerLoop&);
  enum class State { Constructed, Finishing, Failed, Ready, Retired };
  LeasedWorkerLoop(LeasedWorkerCatalogSnapshot&&, const WorkerContext&,
                   WorkerRuntime&, WorkerLimits);
  void Creator() const;
  void Ready() const;
  void BeginStartup();
  std::array<int, 5> CatalogDescriptors();
  std::array<int, 6> LoopDescriptors() const;
  void CompleteStartup();
  void FailStartup() noexcept;
  void Release() noexcept;

  const pid_t creator_;
  State state_ = State::Constructed;
  // Reverse destruction MUST destroy the loop before the closed SQL snapshot.
  std::optional<LeasedWorkerCatalogSnapshot> snapshot_;
  std::unique_ptr<WorkerLoop> loop_;
};
}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_LEASED_WORKER_LOOP_HH_
