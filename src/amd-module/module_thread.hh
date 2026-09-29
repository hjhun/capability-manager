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

#ifndef CAPABILITY_MANAGER_AMD_MODULE_MODULE_THREAD_HH_
#define CAPABILITY_MANAGER_AMD_MODULE_MODULE_THREAD_HH_

#include "amd-module/catalog_service.hh"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace capmgr {

class AmdModuleThread {
 public:
  AmdModuleThread(ReadLeasePolicy policy, std::string action_path,
                  GenerationLeaseOperations* operations = nullptr);
  ~AmdModuleThread();
  bool Available() const { return available_.load(); }
  bool StartupFailed() const { return startup_failed_.load(); }
  void Stop();

 private:
  void Run(std::stop_token stop, ReadLeasePolicy policy,
           std::string action_path,
           GenerationLeaseOperations* operations) noexcept;
  std::mutex mutex_;
  std::condition_variable condition_;
  bool stopping_ = false;
  std::atomic<bool> available_{false};
  std::atomic<bool> startup_failed_{false};
  std::jthread worker_;  // Last: synchronization state exists before launch.
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_AMD_MODULE_MODULE_THREAD_HH_
