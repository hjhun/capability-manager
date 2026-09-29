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

#ifndef CAPABILITY_MANAGER_AMD_MODULE_MODULE_TASK_HH_
#define CAPABILITY_MANAGER_AMD_MODULE_MODULE_TASK_HH_

#include "amd-module/catalog_service.hh"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace capmgr {

// Private adapter for the actual tizen-core calls; host tests supply scoped mocks.
class CoreOperations {
 public:
  using Callback = bool (*)(void*);
  virtual ~CoreOperations() = default;
  virtual void Init() = 0;
  virtual void Shutdown() = 0;
  virtual int Create(void** task) = 0;
  virtual int GetCore(void* task, void** core) = 0;
  virtual int Run(void* task) = 0;
  virtual int Main(void** core) = 0;
  virtual int Idle(void* core, Callback callback, void* data,
                   void** source) = 0;
  virtual int Timer(void* core, unsigned milliseconds, Callback callback,
                    void* data, void** source) = 0;
  virtual int Remove(void* core, void* source) = 0;
  virtual int Quit(void* task) = 0;
  virtual int Destroy(void* task) = 0;
};

CoreOperations& PlatformCoreOperations();

class AmdModuleTask {
 public:
  AmdModuleTask(CoreOperations& core, ReadLeasePolicy policy,
                std::string action_path,
                GenerationLeaseOperations* operations = nullptr);
  ~AmdModuleTask();
  bool Available() const { return available_.load(); }
  bool StartupFailed() const { return startup_failed_.load(); }
  void Stop() noexcept;

 private:
  static bool MainStarted(void* data) noexcept;
  static bool StartOwner(void* data) noexcept;
  static bool Retry(void* data) noexcept;
  static bool StopOwner(void* data) noexcept;
  void Reconcile() noexcept;
  void DisposeTask() noexcept;

  CoreOperations& core_api_;
  ReadLeasePolicy policy_;
  std::string action_path_;
  GenerationLeaseOperations* operations_;
  void* task_ = nullptr;
  void* core_ = nullptr;
  void* main_ = nullptr;
  void* main_idle_ = nullptr;
  void* startup_idle_ = nullptr;
  void* timer_ = nullptr;
  bool initialized_ = false;
  bool running_ = false;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> available_{false};
  std::atomic<bool> startup_failed_{false};
  std::mutex mutex_;
  std::condition_variable condition_;
  bool stopped_ = false;
  bool stop_failed_ = false;
  // Created, physically closed and destroyed only on the dedicated owner task.
  std::unique_ptr<AmdCatalogService> service_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_AMD_MODULE_MODULE_TASK_HH_
