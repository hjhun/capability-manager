// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "api/capmgr.h"
#include "common/error.hh"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace capmgr {
// Private async lifetime engine. Transport jobs must observe cancellation and
// return; platform deadlines bound transport shutdown independently of callbacks.
class Dispatcher {
 public:
  using Emit=std::function<void(std::string)>;
  using Work=std::function<std::string(const std::atomic<bool>&,const Emit&)>;
  struct Frame {std::string json;bool is_event=false;bool complete=true;};
  using EmitFrame=std::function<void(Frame)>;
  using FramedWork=std::function<void(const std::atomic<bool>&,const EmitFrame&)>;
  enum class Protocol { kGeneric, kAction };
  explicit Dispatcher(uint64_t first_token=1);
  ~Dispatcher();
  Dispatcher(const Dispatcher&)=delete;
  Dispatcher& operator=(const Dispatcher&)=delete;
  uint64_t Execute(Work work,nlohmann::json rpc_id,capmgr_result_cb callback,void* data,bool supports_cancel=true);
  uint64_t ExecuteFrames(FramedWork work,nlohmann::json rpc_id,capmgr_result_cb callback,
                         void* data,bool supports_cancel=true,Protocol protocol=Protocol::kGeneric);
  void Cancel(uint64_t token);
  bool Close(); // false = BUSY, no state change
  bool EnterCallback();
  void LeaveCallback();
  bool SetChanged(capmgr_changed_cb callback,void* data);
  void Changed(uint64_t revision);
 private:
  struct Job {
    std::atomic<bool> cancelled{false};
    capmgr_result_cb callback=nullptr;
    void* data=nullptr;
    std::thread worker;
    bool terminal=false;
    bool supports_cancel=true;
    bool admitted=false;
    nlohmann::json rpc_id;
    ~Job();
  };
  struct Reply {uint64_t token;std::string json;bool event;bool complete;};
  void Dispatch();
  std::mutex mutex_;
  std::condition_variable wake_,space_;
  std::map<uint64_t,std::shared_ptr<Job>> jobs_;
  std::deque<Reply> replies_;
  size_t queued_bytes_=0;
  uint64_t next_;
  bool closing_=false,stop_=false,active_=false;
  capmgr_changed_cb changed_=nullptr;
  void* changed_data_=nullptr;
  std::optional<uint64_t> changed_revision_;
  uint64_t newest_revision_=0;
  std::thread dispatcher_;
};
}
