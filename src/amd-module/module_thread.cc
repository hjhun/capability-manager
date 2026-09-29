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

#include "amd-module/module_thread.hh"

#include "common/logging.hh"

namespace capmgr {

AmdModuleThread::AmdModuleThread(ReadLeasePolicy policy,
                                 std::string action_path,
                                 GenerationLeaseOperations* operations)
    : worker_([this, policy = std::move(policy), path = std::move(action_path),
               operations](std::stop_token stop) mutable {
        Run(stop, std::move(policy), std::move(path), operations);
      }) {}

AmdModuleThread::~AmdModuleThread() { Stop(); }

void AmdModuleThread::Stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  worker_.request_stop();
  condition_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void AmdModuleThread::Run(std::stop_token stop, ReadLeasePolicy policy,
                          std::string action_path,
                          GenerationLeaseOperations* operations) noexcept {
  std::unique_ptr<AmdCatalogService> service;
  while (!stop.stop_requested()) {
    try {
      if (!service) {
        auto candidate = std::make_unique<AmdCatalogService>(
            policy, action_path,
            [](uint64_t revision) {
              LOG(INFO) << "Action catalog committed revision=" << revision;
            },
            operations);
        candidate->Start();
        service = std::move(candidate);
        available_.store(true);
        LOG(INFO)
            << "AMD catalog import active; five-second fallback reconciliation";
      } else {
        service->Reconcile();
      }
    } catch (const std::exception& error) {
      if (!service) startup_failed_.store(true);
      logging::Failure(
          service ? "Action reconciliation failed; prior catalog retained"
                  : "AMD catalog unavailable; retry in five seconds",
          error.what());
    } catch (...) {
      if (!service) startup_failed_.store(true);
      logging::Failure("AMD catalog unavailable; retry in five seconds",
                       "unknown exception");
    }
    std::unique_lock lock(mutex_);
    if (condition_.wait_for(lock, std::chrono::seconds(5), [this, stop] {
          return stopping_ || stop.stop_requested();
        }))
      break;
  }
  try {
    if (service)
      service->Stop();  // SAME owner, physical close before lease release.
    available_.store(false);
    LOG(INFO) << "AMD catalog owner stopped";
  } catch (const std::exception& error) {
    logging::Failure("AMD catalog shutdown failed", error.what());
  } catch (...) {
    logging::Failure("AMD catalog shutdown failed", "unknown exception");
  }
}

}  // namespace capmgr
