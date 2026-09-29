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

#include "amd-module/module_task.hh"

#include <exception>

#include "common/logging.hh"

namespace capmgr {
namespace {

void CheckCore(int result, const char* operation) {
  if (result != 0)
    throw Error(ErrorCode::kIo,
                std::string(operation) + " failed: " + std::to_string(result));
}

void FailStop(int result, const char* operation) noexcept {
  if (result == 0) return;
  try {
    LOG(ERROR) << operation << " failed: " << result
               << "; cannot safely unload owned task";
  } catch (...) {
  }
  std::terminate();
}

}  // namespace

AmdModuleTask::AmdModuleTask(CoreOperations& core, ReadLeasePolicy policy,
                             std::string action_path,
                             GenerationLeaseOperations* operations)
    : core_api_(core),
      policy_(std::move(policy)),
      action_path_(std::move(action_path)),
      operations_(operations) {
  core_api_.Init();
  initialized_ = true;
  try {
    CheckCore(core_api_.Create(&task_), "tizen_core_task_create");
    CheckCore(core_api_.GetCore(task_, &core_),
              "tizen_core_task_get_tizen_core");
    CheckCore(core_api_.Run(task_), "tizen_core_task_run");
    running_ = true;
    CheckCore(core_api_.Main(&main_), "tizen_core_find(main)");
    std::lock_guard lock(mutex_);
    CheckCore(core_api_.Idle(main_, MainStarted, this, &main_idle_),
              "tizen_core_add_idle_job(main)");
  } catch (...) {
    DisposeTask();
    throw;
  }
}

AmdModuleTask::~AmdModuleTask() { Stop(); }

bool AmdModuleTask::MainStarted(void* data) noexcept {
  auto& self = *static_cast<AmdModuleTask*>(data);
  try {
    std::lock_guard lock(self.mutex_);
    self.main_idle_ = nullptr;  // Callback returns false: automatic removal.
    if (self.stopping_.load()) return false;
    CheckCore(
        self.core_api_.Idle(self.core_, StartOwner, &self, &self.startup_idle_),
        "AMD owner startup handoff");
  } catch (const std::exception& error) {
    self.startup_failed_.store(true);
    logging::Failure("AMD owner startup handoff failed", error.what());
  } catch (...) {
    self.startup_failed_.store(true);
    logging::Failure("AMD owner startup handoff failed", "unknown exception");
  }
  return false;
}

bool AmdModuleTask::StartOwner(void* data) noexcept {
  auto& self = *static_cast<AmdModuleTask*>(data);
  try {
    {
      std::lock_guard lock(self.mutex_);
      self.startup_idle_ = nullptr;
    }
    if (self.stopping_.load()) return false;
    CheckCore(
        self.core_api_.Timer(self.core_, 5000, Retry, &self, &self.timer_),
        "AMD retry timer creation");
    self.Reconcile();
  } catch (const std::exception& error) {
    self.startup_failed_.store(true);
    logging::Failure("AMD owner startup failed", error.what());
  } catch (...) {
    self.startup_failed_.store(true);
    logging::Failure("AMD owner startup failed", "unknown exception");
  }
  return false;
}

bool AmdModuleTask::Retry(void* data) noexcept {
  auto& self = *static_cast<AmdModuleTask*>(data);
  // StopOwner removes this source explicitly on the SAME serialized task.
  if (!self.stopping_.load()) self.Reconcile();
  return true;
}

void AmdModuleTask::Reconcile() noexcept {
  try {
    if (!service_) {
      auto candidate = std::make_unique<AmdCatalogService>(
          policy_, action_path_,
          [](uint64_t revision) {
            LOG(INFO) << "Action catalog committed revision=" << revision;
          },
          operations_);
      candidate->Start();
      service_ = std::move(candidate);
      available_.store(true);
      LOG(INFO) << "AMD catalog import active on tizen-core owner task";
    } else {
      service_->Reconcile();
    }
  } catch (const std::exception& error) {
    if (!service_) startup_failed_.store(true);
    logging::Failure(
        service_ ? "Action reconciliation failed; prior catalog retained"
                 : "AMD catalog unavailable; retry in 5000ms",
        error.what());
  } catch (...) {
    if (!service_) startup_failed_.store(true);
    logging::Failure("AMD catalog reconciliation failed", "unknown exception");
  }
}

bool AmdModuleTask::StopOwner(void* data) noexcept {
  auto& self = *static_cast<AmdModuleTask*>(data);
  try {
    if (self.startup_idle_) {
      CheckCore(self.core_api_.Remove(self.core_, self.startup_idle_),
                "remove owner startup idle");
      self.startup_idle_ = nullptr;
    }
    if (self.timer_) {
      CheckCore(self.core_api_.Remove(self.core_, self.timer_),
                "remove retry timer");
      self.timer_ = nullptr;
    }
    if (self.service_) {
      self.service_
          ->Stop();  // Physical close precedes destruction and task quit.
      self.service_.reset();
    }
    self.available_.store(false);
  } catch (const std::exception& error) {
    logging::Failure("AMD owner shutdown failed", error.what());
    self.stop_failed_ = true;
  } catch (...) {
    logging::Failure("AMD owner shutdown failed", "unknown exception");
    self.stop_failed_ = true;
  }
  {
    std::lock_guard lock(self.mutex_);
    self.stopped_ = true;
  }
  self.condition_.notify_one();
  return false;
}

void AmdModuleTask::Stop() noexcept {
  if (!task_) return;
  stopping_.store(true);
  // INIT/FINI and main-idle dispatch belong to AMD's serialized main context.
  if (main_idle_) {
    FailStop(core_api_.Remove(main_, main_idle_), "remove main startup idle");
    main_idle_ = nullptr;
  }
  if (running_) {
    void* stop_source = nullptr;
    FailStop(core_api_.Idle(core_, StopOwner, this, &stop_source),
             "post owner shutdown");
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return stopped_; });
    if (stop_failed_) std::terminate();
  }
  DisposeTask();
}

void AmdModuleTask::DisposeTask() noexcept {
  if (task_) {
    if (running_) FailStop(core_api_.Quit(task_), "tizen_core_task_quit");
    FailStop(core_api_.Destroy(task_), "tizen_core_task_destroy");
    task_ = nullptr;
    core_ = nullptr;
    running_ = false;
  }
  if (initialized_) {
    core_api_.Shutdown();
    initialized_ = false;
  }
}

}  // namespace capmgr
