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

#include "api/dispatcher.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>

namespace capmgr {

namespace {

std::atomic<unsigned> global_jobs{0};
struct ReplyFailure {
  const char* cause;
};

void ValidateReply(const std::string& text, const nlohmann::json& id,
                   bool event, bool complete, Dispatcher::Protocol protocol) {
  using Json = nlohmann::json;
  if (text.size() > 1024 * 1024) throw ReplyFailure{"response limit"};
  if (text.find_first_not_of(" \t\r\n") == std::string::npos)
    throw ReplyFailure{"empty response"};
  auto json = Json::parse(
      text,
      [](int depth, Json::parse_event_t, Json&) {
        if (depth > 128) throw ReplyFailure{"response nesting limit"};
        return true;
      },
      false);
  if (json.is_discarded() || !json.is_object() ||
      json.value("jsonrpc", Json()) != "2.0" || !json.contains("id") ||
      (json.contains("result") + json.contains("error") +
       json.contains("event")) != 1)
    throw ReplyFailure{"malformed response"};
  const auto& actual = json["id"];
  bool valid_id =
      actual.is_string() ||
      (actual.is_number_integer() &&
       (!actual.is_number_unsigned() ||
        actual.get<uint64_t>() <= static_cast<uint64_t>(INT64_MAX)));
  if (!valid_id || actual.is_string() != id.is_string() || actual != id)
    throw ReplyFailure{"response ID mismatch"};
  if (event != json.contains("event"))
    throw ReplyFailure{"response kind mismatch"};
  bool acknowledgement = false;
  if (json.contains("result") && json["result"].is_object()) {
    const auto& result = json["result"];
    acknowledgement =
        result.value("subscription", Json()) == true &&
        (!result.contains("isError") || result["isError"] == false);
  }
  // CLI result members remain native data; Action reserves this acknowledgement.
  if (protocol == Dispatcher::Protocol::kAction && acknowledgement && complete)
    throw ReplyFailure{"terminal subscription acknowledgement"};
  if (event) {
    const auto& body = json["event"];
    if (!body.is_object() || complete != body.contains("closed") ||
        (body.contains("closed") &&
         (!body["closed"].is_string() ||
          body["closed"].get_ref<const std::string&>().empty())) ||
        (body.contains("isError") && !body["isError"].is_boolean()))
      throw ReplyFailure{"malformed event response"};
  } else if (!complete) {
    if (!json.contains("result") || !json["result"].is_object() ||
        json["result"].value("subscription", Json()) != true ||
        (json["result"].contains("isError") &&
         json["result"]["isError"] != false))
      throw ReplyFailure{"invalid subscription acknowledgement"};
  }

  if (json.contains("error")) {
    const auto& error = json["error"];
    if (!error.is_object() || !error.contains("code") ||
        !error["code"].is_number_integer() ||
        (error["code"].is_number_unsigned() &&
         error["code"].get<uint64_t>() > static_cast<uint64_t>(INT64_MAX)) ||
        !error.contains("message") || !error["message"].is_string())
      throw ReplyFailure{"malformed error response"};
  }
}
}  // namespace

Dispatcher::Job::~Job() {
  if (admitted) --global_jobs;
}

Dispatcher::Dispatcher(uint64_t first, DispatcherOperations* operations)
    : operations_(operations), next_(first), dispatcher_([this] {
        Dispatch();
        try {
          dispatcher_exit_.set_value_at_thread_exit();
        } catch (...) { /* Retain missing exit proof. */
        }
      }) {}
Dispatcher::~Dispatcher() {
  if (Close() != CloseResult::kDone) std::terminate();
}

uint64_t Dispatcher::Execute(Work work, nlohmann::json rpc_id,
                             capmgr_result_cb callback, void* data,
                             bool supports_cancel) {
  if (!work) throw Error(ErrorCode::kInvalid, "Missing async operation");
  return ExecuteFrames(
      [work = std::move(work)](const auto& cancelled, const EmitFrame& emit) {
        auto result = work(cancelled, [&](std::string json) {
          emit({std::move(json), true, false});
        });
        emit({std::move(result), false, true});
      },
      std::move(rpc_id), callback, data, supports_cancel);
}

uint64_t Dispatcher::ExecuteFrames(FramedWork work, nlohmann::json rpc_id,
                                   capmgr_result_cb callback, void* data,
                                   bool supports_cancel, Protocol protocol) {
  if (!work) throw Error(ErrorCode::kInvalid, "Missing async operation");
  return AdmitJob(std::move(work), std::move(rpc_id), callback, data,
                  supports_cancel, protocol, {});
}

uint64_t Dispatcher::ExecuteManaged(std::shared_ptr<ManagedOperation> owner,
                                    nlohmann::json rpc_id,
                                    capmgr_result_cb callback, void* data) {
  if (!owner) throw Error(ErrorCode::kInvalid, "Missing managed cleanup owner");
  return AdmitJob({}, std::move(rpc_id), callback, data, true,
                  Protocol::kGeneric, std::move(owner));
}

uint64_t Dispatcher::AdmitJob(FramedWork work, nlohmann::json rpc_id,
                              capmgr_result_cb callback, void* data,
                              bool supports_cancel, Protocol protocol,
                              std::shared_ptr<ManagedOperation> owner) {
  if (!callback) throw Error(ErrorCode::kInvalid, "Missing async callback");
  std::unique_lock lock(mutex_);
  if (closing_) throw Error(ErrorCode::kBusy, "Client is closing");
  if (!next_ || jobs_.size() >= 2)
    throw Error(ErrorCode::kLimit, "Request capacity exhausted");
  for (const auto& [id, job] : jobs_)
    if (job->rpc_id == rpc_id)
      throw Error(ErrorCode::kConflict, "Duplicate in-flight JSON ID");
  const auto token = next_;
  auto job = std::make_shared<Job>();
  job->callback = callback;
  job->data = data;
  job->rpc_id = rpc_id;
  job->supports_cancel = supports_cancel;
  unsigned current = global_jobs.load();
  do {
    if (current >= 4)
      throw Error(ErrorCode::kLimit, "Global request capacity exhausted");
  } while (!global_jobs.compare_exchange_weak(current, current + 1));
  job->managed = std::move(owner);
  job->admitted = true;
  try {
    if (operations_) operations_->BeforeInsert();
    jobs_.emplace(token, job);
    if (job->managed && !job->managed->BindClientToken(token))
      throw Error(ErrorCode::kConflict, "Managed cleanup owner already bound");
    job->worker = std::thread([this, job, token, work = std::move(work),
                               rpc_id = std::move(rpc_id), protocol]() mutable {
      auto emit = [this, job, token, &rpc_id, protocol](Frame frame) {
        auto& json = frame.json;
        ValidateReply(json, rpc_id, frame.is_event, frame.complete, protocol);
        std::unique_lock lock(mutex_);
        if (closing_ || job->terminal) return;
        space_.wait(lock, [&] {
          return closing_ || job->terminal ||
                 (replies_.size() < 64 &&
                  json.size() <= 1024 * 1024 - queued_bytes_);
        });
        if (closing_ || job->terminal) return;
        auto size = json.size();
        replies_.push_back(
            {token, std::move(json), frame.is_event, frame.complete});
        queued_bytes_ += size;
        if (frame.complete) {
          job->terminal = true;
          space_.notify_all();
        }
        wake_.notify_one();
      };
      auto failure = [&](const char* cause, int backend_code = 0) {
        try {
          nlohmann::json response = {
              {"jsonrpc", "2.0"},
              {"id", rpc_id},
              {"error",
               {{"code", -32090},
                {"message", "Capability transport failure"},
                {"data", {{"cause", cause}, {"backendCode", backend_code}}}}}};
          emit({response.dump(), false, true});
        } catch (
            ...) { /* Allocation failure cannot safely allocate another reply. */
        }
      };
      if (job->managed) {
        // Run completion is only thread progress, never child-absence proof.
        try {
          job->managed->Run();
        } catch (...) { /* Retain owner and its cleanup state. */
        }
      } else
        try {
          work(job->cancelled, emit);
          bool complete;
          {
            std::lock_guard lock(mutex_);
            complete = job->terminal || closing_;
          }
          if (!complete) failure("backend ended without terminal response");
        } catch (const ReplyFailure& error) {
          failure(error.cause);
        } catch (const Error& error) {
          failure("backend error", static_cast<int>(error.code()));
        } catch (...) {
          failure("backend exception");
        }
      // Destroy potentially user-owned work captures BEFORE publishing done.
      // The remaining thread epilogue only releases trivial/local ownership.
      work = {};
      rpc_id = nullptr;
      // A body-done flag alone precedes arbitrary thread-local destructors.
      // Future readiness is delayed until those destructors have completed.
      try {
        job->exited_promise.set_value_at_thread_exit();
      } catch (...) { /* Retain missing exit proof. */
      }
      {
        std::lock_guard lock(mutex_);
        job->work_done = true;
        wake_.notify_all();
      }
    });
  } catch (...) {
    auto retired = std::move(job->managed);
    jobs_.erase(token);
    lock.unlock();
    retired.reset();
    throw;
  }
  next_ = token == UINT64_MAX ? 0 : token + 1;
  wake_.notify_all();
  return token;
}

void Dispatcher::Cancel(uint64_t token) {
  std::unique_lock lock(mutex_);
  auto it = jobs_.find(token);
  if (it == jobs_.end() || it->second->terminal)
    throw Error(ErrorCode::kNotFound, "Unknown or completed request token");
  if (!it->second->supports_cancel)
    throw Error(ErrorCode::kUnsupported, "Backend cancellation is unsupported");
  it->second->cancelled = true;
  space_.notify_all();
  auto owner = it->second->managed;
  lock.unlock();
  if (owner && !owner->RequestCancel())
    throw Error(ErrorCode::kIo, "Managed cancellation coordinator has ended");
}

bool Dispatcher::EnterCallback() {
  std::lock_guard lock(mutex_);
  if (active_ || closing_) return false;
  active_ = true;
  return true;
}

void Dispatcher::LeaveCallback() {
  std::lock_guard lock(mutex_);
  active_ = false;
  wake_.notify_one();
}

bool Dispatcher::SetChanged(capmgr_changed_cb callback, void* data) {
  std::lock_guard lock(mutex_);
  if (active_ || closing_) return false;
  changed_ = callback;
  changed_data_ = data;
  return true;
}

void Dispatcher::Changed(uint64_t revision) {
  std::lock_guard lock(mutex_);
  if (closing_ || revision <= newest_revision_) return;
  newest_revision_ = revision;
  changed_revision_ = revision;
  wake_.notify_one();
}

void Dispatcher::CheckAdmission() {
  std::lock_guard lock(mutex_);
  if (closing_) throw Error(ErrorCode::kBusy, "Client is closing");
}

bool Dispatcher::HasFinishedWorker() const {
  for (const auto& [token, job] : jobs_)
    if (job->work_done && !job->joined && !job->joining &&
        job->exited.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready)
      return true;
  return false;
}

bool Dispatcher::JoinFinished(std::unique_lock<std::mutex>& lock) {
  for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
    auto job = it->second;
    if (!job->work_done || job->joined || job->joining ||
        job->exited.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready)
      continue;
    job->joining = true;
    auto thread = std::move(job->worker);
    lock.unlock();
    if (thread.joinable()) thread.join();
    lock.lock();
    job->joined = true;
    job->joining = false;
    if (job->callback_done && !closing_ && Releasable(*job))
      RetireJob(it->first, lock);
    wake_.notify_all();
    return true;
  }
  return false;
}

void Dispatcher::RetireJob(uint64_t token, std::unique_lock<std::mutex>& lock) {
  auto it = jobs_.find(token);
  if (it == jobs_.end()) return;
  auto owner = std::move(it->second->managed);
  jobs_.erase(it);
  lock.unlock();
  owner.reset();
  lock.lock();
}

bool Dispatcher::Releasable(const Job& job) const {
  return job.joined &&
         (!job.managed || (job.proof && job.quiescent && !job.poisoned));
}

void Dispatcher::RefreshManaged(std::unique_lock<std::mutex>& lock,
                                bool offer) {
  std::array<std::pair<uint64_t, std::shared_ptr<Job>>, 2> observed;
  size_t count = 0;
  for (const auto& [token, job] : jobs_)
    if (job->managed) observed[count++] = {token, job};
  for (size_t i = 0; i < count; ++i) {
    auto [token, job] = observed[i];
    if (!jobs_.contains(token) || !job->managed) continue;
    const bool validate = offer && !closing_ && !job->terminal;
    auto owner = job->managed;
    lock.unlock();
    auto snapshot = owner->PollCleanup();
    owner.reset();
    const auto& state = *snapshot.publication;
    bool invalid = false, allocation_pending = false;
    if (validate && state.terminal) {
      try {
        ValidateReply(*state.terminal, job->rpc_id, false, true,
                      Protocol::kGeneric);
      } catch (const std::bad_alloc&) {
        allocation_pending = true;
      } catch (const std::length_error&) {
        allocation_pending = true;
      } catch (...) {
        invalid = true;
      }
    }
    lock.lock();
    // Another closer may have consumed this snapshot's last map ownership.
    auto it = jobs_.find(token);
    if (it == jobs_.end() || it->second != job) continue;
    // Concurrent polling may finish in reverse order; ignore an older coherent
    // publication. False quiescence means not observed by this try-lock sample.
    job->quiescent = job->quiescent || snapshot.quiescent;
    if (state.version < job->publication_version) continue;
    job->publication_version = state.version;
    const bool confirmed =
        state.cleanup == ManagedOperation::Cleanup::kConfirmedComplete;
    invalid = invalid || (job->proof && !confirmed) ||
              (state.terminal && !confirmed) ||
              (job->terminal_source && state.terminal != job->terminal_source);
    if (invalid) {
      job->poisoned = true;
      closing_ = true;
      for (const auto& [id, pending] : jobs_) pending->cancelled = true;
      space_.notify_all();
      wake_.notify_all();
    } else {
      job->proof = confirmed;
      if (offer && !closing_ && !job->terminal && state.terminal &&
          !allocation_pending && replies_.size() < 64 &&
          state.terminal->size() <= 1024 * 1024 - queued_bytes_) {
        try {
          replies_.push_back({token, *state.terminal, false, true});
          queued_bytes_ += state.terminal->size();
          job->terminal_source = state.terminal;
          job->terminal = true;
          space_.notify_all();
          wake_.notify_all();
        } catch (
            const std::bad_alloc&) { /* Owner retains terminal for retry. */
        } catch (
            const std::length_error&) { /* Owner retains terminal for retry. */
        }
      }
    }
    if (job->callback_done && !closing_ && Releasable(*job))
      RetireJob(it->first, lock);
  }
}

Dispatcher::CloseResult Dispatcher::Close(std::chrono::milliseconds budget) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::clamp(budget, std::chrono::milliseconds(0),
                                   std::chrono::milliseconds(1000));
  // Same-handle public calls are serialized. A concurrent private close never
  // waits behind another closer or releases that closer's retained ownership.
  std::unique_lock closer(close_mutex_, std::try_to_lock);
  if (!closer.owns_lock()) {
    std::lock_guard lock(mutex_);
    return active_ ? CloseResult::kBusy : CloseResult::kIoPending;
  }

  std::unique_lock lock(mutex_);
  if (active_) return CloseResult::kBusy;
  closing_ = true;
  for (const auto& [token, job] : jobs_) job->cancelled = true;
  space_.notify_all();
  wake_.notify_all();
  std::array<std::shared_ptr<ManagedOperation>, 2> owners;
  size_t count = 0;
  for (const auto& [token, job] : jobs_)
    if (job->managed) owners[count++] = job->managed;
  lock.unlock();
  for (size_t i = 0; i < count; ++i) {
    owners[i]->RequestCancel();
    owners[i].reset();
  }

  lock.lock();
  for (;;) {
    RefreshManaged(lock, false);
    if (JoinFinished(lock)) continue;
    bool all = true;
    for (const auto& [token, job] : jobs_)
      if (!Releasable(*job)) {
        all = false;
        break;
      }
    if (all) {
      stop_ = true;
      wake_.notify_all();
      if (dispatcher_done_ &&
          dispatcher_exited_.wait_for(std::chrono::milliseconds(0)) ==
              std::future_status::ready) {
        auto thread = std::move(dispatcher_);
        lock.unlock();
        if (thread.joinable()) thread.join();
        lock.lock();
        replies_.clear();
        queued_bytes_ = 0;
        changed_revision_.reset();
        changed_ = nullptr;
        changed_data_ = nullptr;
        count = 0;
        for (auto& [token, job] : jobs_)
          if (job->managed) owners[count++] = std::move(job->managed);
        jobs_.clear();
        lock.unlock();
        for (size_t i = 0; i < count; ++i) owners[i].reset();
        return CloseResult::kDone;
      }
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return CloseResult::kIoPending;
    wake_.wait_until(lock,
                     std::min(deadline, std::chrono::steady_clock::now() +
                                            std::chrono::milliseconds(2)));
  }
}

void Dispatcher::Dispatch() {
  for (;;) {
    std::unique_lock lock(mutex_);
    auto ready = [&] {
      return stop_ ||
             (!closing_ && !active_ &&
              (HasFinishedWorker() || !replies_.empty() || changed_revision_));
    };
    // Thread-exit futures become ready after the final notification/TLS cleanup.
    if (jobs_.empty())
      wake_.wait(lock, [&] { return ready() || !jobs_.empty(); });
    else
      wake_.wait_for(lock, std::chrono::milliseconds(2), ready);
    if (stop_) {
      dispatcher_done_ = true;
      wake_.notify_all();
      return;
    }
    if (closing_ || active_) continue;
    RefreshManaged(lock, true);
    if (closing_ || active_) continue;
    if (JoinFinished(lock)) continue;
    if (changed_revision_) {
      auto revision = *changed_revision_;
      changed_revision_.reset();
      auto callback = changed_;
      auto data = changed_data_;
      if (!callback) continue;
      active_ = true;
      lock.unlock();
      try {
        callback(revision, data);
      } catch (...) {
      }
      lock.lock();
      active_ = false;
      wake_.notify_all();
      continue;
    }
    if (replies_.empty()) continue;
    auto reply = std::move(replies_.front());
    replies_.pop_front();
    queued_bytes_ -= reply.json.size();
    space_.notify_all();
    auto it = jobs_.find(reply.token);
    if (it == jobs_.end()) continue;
    auto job = it->second;
    active_ = true;
    lock.unlock();
    try {
      job->callback(reply.token, reply.json.c_str(), reply.event, job->data);
    } catch (...) {
    }
    lock.lock();
    active_ = false;
    if (reply.complete) {
      job->callback_done = true;
      if (Releasable(*job)) RetireJob(reply.token, lock);
    }
    wake_.notify_all();
  }
}
}  // namespace capmgr
