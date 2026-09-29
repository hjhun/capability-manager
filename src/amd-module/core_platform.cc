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

#include <tizen_core.h>

namespace capmgr {
namespace {

class PlatformCore final : public CoreOperations {
 public:
  void Init() override { tizen_core_init(); }
  void Shutdown() override { tizen_core_shutdown(); }
  int Create(void** task) override {
    return tizen_core_task_create("capmgr-module", true, task);
  }
  int GetCore(void* task, void** core) override {
    return tizen_core_task_get_tizen_core(task, core);
  }
  int Run(void* task) override { return tizen_core_task_run(task); }
  int Main(void** core) override { return tizen_core_find("main", core); }
  int Idle(void* core, Callback callback, void* data, void** source) override {
    return tizen_core_add_idle_job(core, callback, data, source);
  }
  int Timer(void* core, unsigned milliseconds, Callback callback, void* data,
            void** source) override {
    return tizen_core_add_timer(core, milliseconds, callback, data, source);
  }
  int Remove(void* core, void* source) override {
    return tizen_core_remove_source(core, source);
  }
  int Quit(void* task) override { return tizen_core_task_quit(task); }
  int Destroy(void* task) override { return tizen_core_task_destroy(task); }
};

}  // namespace

CoreOperations& PlatformCoreOperations() {
  static PlatformCore core;
  return core;
}

}  // namespace capmgr
