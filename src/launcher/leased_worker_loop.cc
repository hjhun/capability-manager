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

#include "launcher/leased_worker_loop.hh"

#include <unistd.h>

#include <exception>

namespace capmgr {

namespace {

void Require(bool condition, const char* reason) {
  if (!condition) throw Error(ErrorCode::kPermission, reason);
}
}  // namespace

std::unique_ptr<LeasedWorkerLoop> LeasedWorkerLoop::LoadAndFinish(
    const WorkerBootstrapPolicy& policy, const WorkerContext& context,
    ReadLeasePolicy read_policy, WorkerRuntime& runtime, WorkerLimits limits) {
  ValidateWorkerBootstrap(policy);  // Exact initial gate BEFORE lease/SQLite.
  WorkerCatalogReader reader(7, std::move(read_policy));
  auto snapshot =
      reader.Finish();  // Same lease; physical SQLite close checked.
  auto owner = std::unique_ptr<LeasedWorkerLoop>(
      new LeasedWorkerLoop(std::move(snapshot), context, runtime, limits));
  FinishWorkerBootstrap(policy, *owner);
  return owner;  // No owner/Step/START escapes on startup failure.
}

LeasedWorkerLoop::LeasedWorkerLoop(LeasedWorkerCatalogSnapshot&& snapshot,
                                   const WorkerContext& context,
                                   WorkerRuntime& runtime, WorkerLimits limits)
    : creator_(getpid()), snapshot_(std::move(snapshot)) {
  CatalogDescriptors();  // Reject moved-from/inherited/poisoned before registry.
  loop_ = std::make_unique<WorkerLoop>(context, snapshot_->Registry(), runtime,
                                       limits);
}

void LeasedWorkerLoop::Creator() const {
  Require(creator_ == getpid(), "Inherited leased worker owner");
}

void LeasedWorkerLoop::Ready() const {
  Creator();  // BEFORE any loop lock/descriptor/signal/reap/query.
  Require(state_ == State::Ready, "Unavailable leased worker owner");
}

LeasedWorkerLoop::~LeasedWorkerLoop() {
  // A forked wrapper cannot run member destructors or touch inherited SQLite,
  // loop/child state. Kernel exit still releases FDs; crash maintenance is open.
  if (creator_ != getpid()) std::terminate();
  if (state_ == State::Ready && !loop_->CanExitCleanly()) std::terminate();
  // Failed/Constructed startup has never admitted Step/jobs. Retired is empty.
}

void LeasedWorkerLoop::BeginStartup() {
  Creator();
  Require(state_ == State::Constructed, "Worker startup is one-shot");
  state_ = State::Finishing;  // Reserve BEFORE potentially failing metadata.
}

std::array<int, 5> LeasedWorkerLoop::CatalogDescriptors() {
  Creator();
  Require(state_ == State::Constructed || state_ == State::Finishing,
          "Worker startup report unavailable");
  Require(snapshot_ && snapshot_->lease_, "Incomplete worker snapshot");
  return snapshot_->lease_->WorkerDescriptors();
}

std::array<int, 6> LeasedWorkerLoop::LoopDescriptors() const {
  Creator();
  Require(state_ == State::Finishing, "Worker startup report unavailable");
  return loop_->StartupDescriptors();
}

void LeasedWorkerLoop::CompleteStartup() {
  Creator();
  Require(state_ == State::Finishing, "Worker startup state changed");
  state_ = State::Ready;
}

void LeasedWorkerLoop::FailStartup() noexcept {
  if (creator_ != getpid()) std::terminate();
  if (state_ == State::Finishing) state_ = State::Failed;
}

void LeasedWorkerLoop::Step(WorkerLoop::Clock::time_point now) {
  Ready();
  loop_->Step(now);
}

void LeasedWorkerLoop::Shutdown() {
  Ready();
  loop_->Shutdown();
}

bool LeasedWorkerLoop::AdmissionOpen() const {
  Ready();
  return loop_->AdmissionOpen();
}

bool LeasedWorkerLoop::Quiescent() const {
  Ready();
  return loop_->Quiescent();
}

bool LeasedWorkerLoop::CanExitCleanly() const {
  Ready();
  return loop_->CanExitCleanly();
}

bool LeasedWorkerLoop::DeliveryLost() const {
  Ready();
  return loop_->DeliveryLost();
}

size_t LeasedWorkerLoop::Jobs() const {
  Ready();
  return loop_->Jobs();
}

uint64_t LeasedWorkerLoop::Revision() const {
  Ready();
  return snapshot_->Revision();
}

void LeasedWorkerLoop::Release() noexcept {
  state_ = State::Retired;
  loop_.reset();
  snapshot_.reset();
}

void LeasedWorkerLoop::RetireCleanly() {
  Ready();
  Require(loop_->CanExitCleanly(), "Worker completion/output still pending");
  Release();
}

void LeasedWorkerLoop::RetireAfterDeliveryLoss() {
  Ready();
  Require(loop_->DeliveryLost() && loop_->Quiescent(),
          "Worker delivery/child cleanup not confirmed");
  Release();
}
}  // namespace capmgr
