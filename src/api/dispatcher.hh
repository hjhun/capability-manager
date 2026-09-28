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

#ifndef CAPABILITY_MANAGER_API_DISPATCHER_HH_
#define CAPABILITY_MANAGER_API_DISPATCHER_HH_

#include "api/capmgr.h"
#include "api/managed_operation.hh"
#include "common/error.hh"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace capmgr {
// Trusted allocation-failure seam; invoked before map insertion, never from IPC.
struct DispatcherOperations {
  virtual ~DispatcherOperations() = default;
  virtual void BeforeInsert() {}
};
// Private async lifetime engine. Transport jobs must observe cancellation and
// return; platform deadlines bound transport shutdown independently of callbacks.
class Dispatcher {
 public:
  using Emit = std::function<void(std::string)>;
  using Work =
      std::function<std::string(const std::atomic<bool>&, const Emit&)>;
  struct Frame {
    std::string json;
    bool is_event = false;
    bool complete = true;
  };
  using EmitFrame = std::function<void(Frame)>;
  using FramedWork =
      std::function<void(const std::atomic<bool>&, const EmitFrame&)>;
  enum class Protocol { kGeneric, kAction };
  enum class CloseResult { kDone, kBusy, kIoPending };
  explicit Dispatcher(uint64_t first_token = 1,
                      DispatcherOperations* operations = nullptr);
  ~Dispatcher();
  Dispatcher(const Dispatcher&) = delete;
  Dispatcher& operator=(const Dispatcher&) = delete;
  uint64_t Execute(Work work, nlohmann::json rpc_id, capmgr_result_cb callback,
                   void* data, bool supports_cancel = true);
  uint64_t ExecuteFrames(FramedWork work, nlohmann::json rpc_id,
                         capmgr_result_cb callback, void* data,
                         bool supports_cancel = true,
                         Protocol protocol = Protocol::kGeneric);
  // Distinct trusted managed-CLI path. Owner is bound before Run can START.
  uint64_t ExecuteManaged(std::shared_ptr<ManagedOperation>,
                          nlohmann::json rpc_id, capmgr_result_cb, void* data);
  void Cancel(uint64_t token);
  // Callback-active BUSY changes nothing. Otherwise close admission/dispatch;
  // unfinished threads remain owned on bounded IO_PENDING and may be retried.
  CloseResult Close(
      std::chrono::milliseconds budget = std::chrono::milliseconds(100));
  void
  CheckAdmission();  // before side-effect-free backend Admit on serialized C handle
  bool EnterCallback();
  void LeaveCallback();
  bool SetChanged(capmgr_changed_cb callback, void* data);
  void Changed(uint64_t revision);

 private:
  struct Job {
    std::atomic<bool> cancelled{false};
    capmgr_result_cb callback = nullptr;
    void* data = nullptr;
    std::thread worker;
    std::promise<void> exited_promise;
    std::future<void> exited = exited_promise.get_future();
    bool terminal = false;
    bool supports_cancel = true;
    bool admitted = false;
    bool work_done = false, joined = false, joining = false,
         callback_done = false;
    nlohmann::json rpc_id;
    std::shared_ptr<ManagedOperation> managed;
    bool proof = false, quiescent = false, poisoned = false;
    uint64_t publication_version = 0;
    std::shared_ptr<const std::string> terminal_source;
    ~Job();
  };
  struct Reply {
    uint64_t token;
    std::string json;
    bool event;
    bool complete;
  };
  uint64_t AdmitJob(FramedWork, nlohmann::json, capmgr_result_cb, void*, bool,
                    Protocol, std::shared_ptr<ManagedOperation>);
  void Dispatch();
  void RefreshManaged(std::unique_lock<std::mutex>&, bool offer);
  bool Releasable(const Job&) const;
  void RetireJob(uint64_t, std::unique_lock<std::mutex>&);
  bool JoinFinished(std::unique_lock<std::mutex>& lock);
  bool HasFinishedWorker() const;
  DispatcherOperations* operations_;
  std::mutex mutex_;
  std::mutex close_mutex_;
  std::condition_variable wake_, space_;
  std::map<uint64_t, std::shared_ptr<Job>> jobs_;
  std::deque<Reply> replies_;
  size_t queued_bytes_ = 0;
  uint64_t next_;
  bool closing_ = false, stop_ = false, active_ = false,
       dispatcher_done_ = false;
  capmgr_changed_cb changed_ = nullptr;
  void* changed_data_ = nullptr;
  std::optional<uint64_t> changed_revision_;
  uint64_t newest_revision_ = 0;
  std::promise<void> dispatcher_exit_;
  std::future<void> dispatcher_exited_ = dispatcher_exit_.get_future();
  std::thread dispatcher_;
};
}

#endif  // CAPABILITY_MANAGER_API_DISPATCHER_HH_
