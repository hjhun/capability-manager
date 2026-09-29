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

#include "api/client.hh"
#include "catalog/catalog.hh"
#include "common/logging.hh"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <unistd.h>

struct capmgr_read_catalog {
  explicit capmgr_read_catalog(std::unique_ptr<capmgr::ReadAccess> admission)
      : access(std::move(admission)),
        catalog(access->Path(), capmgr::Database::Access::kReadOnly) {
    access->Opened(catalog.database());
  }
  // Declared first, destroyed last: lease outlives SQLite and destroy-IO retry.
  std::unique_ptr<capmgr::ReadAccess> access;
  capmgr::Catalog catalog;
};

struct capmgr_client : capmgr_read_catalog {
  explicit capmgr_client(std::unique_ptr<capmgr::ReadAccess> admission)
      : capmgr_read_catalog(std::move(admission)) {}
  const pid_t creator = getpid();
  // Base validation finishes before any dispatcher thread exists.
  capmgr::Dispatcher dispatcher;
  std::shared_ptr<capmgr::ExecutionBackend> backend;
};

struct capmgr_search_results {
  std::vector<std::string> items;
};


namespace {

bool SameProcess(capmgr_client_h client) noexcept {
  return client->creator == getpid();
}

template <class F>
int Guard(F&& function) noexcept {
  try {
    function();
    return CAPMGR_OK;
  } catch (const capmgr::Error& e) {
    capmgr::logging::Failure("C API request failed", e.what());
    return static_cast<int>(e.code());
  } catch (const capmgr::Json::exception& error) {
    capmgr::logging::Failure("C API JSON failure", error.what());
    return CAPMGR_ERROR_DATABASE;
  } catch (const std::bad_alloc&) {
    capmgr::logging::Failure("C API request failed", "allocation failure");
    return CAPMGR_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    capmgr::logging::Failure("C API request failed", "unknown exception");
    return CAPMGR_ERROR_IO;
  }
}

class PlatformAccessGate final : public capmgr::AccessGate {
 public:
  std::string AuthorizeAndGetDatabase() override {
    // P01 fails closed until verified TIDL/Cynara and DB policy integration.
    throw capmgr::Error(capmgr::ErrorCode::kPermission,
                        "Platform authorization is not configured");
  }
};

}  // namespace

namespace capmgr {

namespace {

class LegacyReadAccess final : public ReadAccess {
 public:
  explicit LegacyReadAccess(std::string path) : path_(std::move(path)) {}
  const std::string& Path() const noexcept override { return path_; }
  void Check() override {}
  void Opened(Database&) override {}

 private:
  std::string path_;
};

}  // namespace

std::unique_ptr<ReadAccess> AccessGate::AuthorizeReadAccess() {
  return std::make_unique<LegacyReadAccess>(AuthorizeAndGetDatabase());
}

int CreateClient(AccessGate& gate, capmgr_client_h* client) noexcept {
  return CreateClient(gate, {}, client);
}

int CreateClient(AccessGate& gate, std::shared_ptr<ExecutionBackend> backend,
                 capmgr_client_h* client) noexcept {
  if (!client) return CAPMGR_ERROR_INVALID_ARGUMENT;
  *client = nullptr;
  return Guard([&] {
    auto access = gate.AuthorizeReadAccess();
    if (!access)
      throw Error(ErrorCode::kPermission, "Missing catalog admission");
    access->Check();
    auto out = std::make_unique<capmgr_client>(std::move(access));
    out->backend = std::move(backend);
    *client = out.release();
  });
}

void NotifyChanged(capmgr_client_h client, uint64_t revision) {
  if (client && SameProcess(client)) client->dispatcher.Changed(revision);
}
}  // namespace capmgr
extern "C" {
int capmgr_client_create(capmgr_client_h* client) {
  PlatformAccessGate gate;
  return capmgr::CreateClient(gate, client);
}

int capmgr_client_destroy(capmgr_client_h client) {
  if (!client) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return Guard([&] {
    auto result = client->dispatcher.Close();
    if (result == capmgr::Dispatcher::CloseResult::kBusy)
      throw capmgr::Error(capmgr::ErrorCode::kBusy, "Callback active");
    if (result == capmgr::Dispatcher::CloseResult::kIoPending)
      throw capmgr::Error(capmgr::ErrorCode::kIo, "Cleanup pending");
    delete client;
  });
}

int capmgr_client_foreach_capability(capmgr_client_h client, capmgr_kind_t kind,
                                     capmgr_foreach_cb callback, void* data) {
  if (!client || !callback) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  if (!client->dispatcher.EnterCallback()) return CAPMGR_ERROR_BUSY;
  struct Scope {
    capmgr::Dispatcher& dispatcher;
    ~Scope() { dispatcher.LeaveCallback(); }
  } scope{client->dispatcher};
  return Guard([&] {
    client->access->Check();
    client->catalog.Foreach(static_cast<capmgr::Kind>(kind),
                            [&](const auto& entry) {
                              auto json = entry.dump();
                              client->access->Check();
                              return callback(json.c_str(), data);
                            });
    client->access->Check();
  });
}

int capmgr_client_search_capabilities(capmgr_client_h client, const char* query,
                                      capmgr_kind_t kind,
                                      capmgr_search_results_h* results) {
  if (results) *results = nullptr;
  if (!client || !query || !results) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return Guard([&] {
    client->access->Check();
    auto out = std::make_unique<capmgr_search_results>();
    for (const auto& entry :
         client->catalog.Search(query, static_cast<capmgr::Kind>(kind)))
      out->items.push_back(entry.dump());
    client->access->Check();
    *results = out.release();
  });
}

void capmgr_search_results_free(capmgr_search_results_h results) {
  delete results;
}

int capmgr_search_results_count(capmgr_search_results_h results,
                                size_t* count) {
  if (count) *count = 0;
  if (!results || !count) return CAPMGR_ERROR_INVALID_ARGUMENT;
  *count = results->items.size();
  return CAPMGR_OK;
}

int capmgr_search_results_item(capmgr_search_results_h results, size_t index,
                               const char** json) {
  if (json) *json = nullptr;
  if (!results || !json) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (index >= results->items.size()) return CAPMGR_ERROR_NOT_FOUND;
  *json = results->items[index].c_str();
  return CAPMGR_OK;
}

int capmgr_client_get_capability(capmgr_client_h client, const char* id,
                                 char** detail) {
  if (detail) *detail = nullptr;
  if (!client || !id || !detail) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return Guard([&] {
    client->access->Check();
    auto json = client->catalog.Get(id).dump();
    client->access->Check();
    if (json.size() == SIZE_MAX)
      throw capmgr::Error(capmgr::ErrorCode::kLimit, "JSON too large");
    auto* out = static_cast<char*>(std::malloc(json.size() + 1));
    if (!out) throw std::bad_alloc();
    std::memcpy(out, json.c_str(), json.size() + 1);
    *detail = out;
  });
}

int capmgr_client_execute(capmgr_client_h client, const char* request,
                          capmgr_result_cb callback, void* data,
                          capmgr_request_token_t* token) {
  if (token) *token = 0;
  if (!client || !request || !callback || !token)
    return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return Guard([&] {
    client->dispatcher.CheckAdmission();
    auto parsed = capmgr::ParseRequest(request);
    client->access->Check();
    auto entry = client->catalog.GetPrivate(parsed.capability_id);
    client->access->Check();
    if (entry.kind != capmgr::Kind::kCli && entry.kind != capmgr::Kind::kAction)
      throw capmgr::Error(capmgr::ErrorCode::kUnsupported,
                          "Skill execution belongs to the agent");
    if (!client->backend)
      throw capmgr::Error(capmgr::ErrorCode::kUnsupported,
                          "Execution transport is unavailable");
    auto backend = client->backend;
    backend->Admit(entry, parsed);
    if (entry.kind == capmgr::Kind::kCli) {
      auto managed = backend->PrepareManagedCli(entry, parsed);
      if (managed) {
        *token = client->dispatcher.ExecuteManaged(std::move(managed),
                                                   parsed.id, callback, data);
        return;
      }
    }
    auto id = parsed.id;
    bool supports_cancel = backend->SupportsCancel(entry);
    auto protocol = entry.kind == capmgr::Kind::kAction
                        ? capmgr::Dispatcher::Protocol::kAction
                        : capmgr::Dispatcher::Protocol::kGeneric;
    *token = client->dispatcher.ExecuteFrames(
        [backend, entry = std::move(entry), parsed = std::move(parsed)](
            const auto& cancelled, const auto& emit) {
          backend->Run(entry, parsed, cancelled, emit);
        },
        std::move(id), callback, data, supports_cancel, protocol);
  });
}

int capmgr_client_cancel(capmgr_client_h client, capmgr_request_token_t token) {
  if (!client || !token) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return Guard([&] { client->dispatcher.Cancel(token); });
}

int capmgr_client_remount_resources(capmgr_client_h client,
                                    const char* destination) {
  if (!client || !destination || destination[0] != '/')
    return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return CAPMGR_ERROR_NOT_SUPPORTED;
}

int capmgr_client_set_changed_callback(capmgr_client_h client,
                                       capmgr_changed_cb callback, void* data) {
  if (!client) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (!SameProcess(client)) return CAPMGR_ERROR_PERMISSION_DENIED;
  return client->dispatcher.SetChanged(callback, data) ? CAPMGR_OK
                                                       : CAPMGR_ERROR_BUSY;
}
}
