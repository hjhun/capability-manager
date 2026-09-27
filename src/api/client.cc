// SPDX-License-Identifier: Apache-2.0
#include "api/client.hh"
#include "catalog/catalog.hh"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
struct capmgr_client {
  explicit capmgr_client(const std::string& path)
      : catalog(path, capmgr::Database::Access::kReadOnly) {}
  capmgr::Catalog catalog;
  std::atomic<bool> callback_active{false};
};
struct capmgr_search_results { std::vector<std::string> items; };
namespace {
template<class F> int Guard(F&& function) noexcept {
  try { function(); return CAPMGR_OK; }
  catch (const capmgr::Error& e) { return static_cast<int>(e.code()); }
  catch (const capmgr::Json::exception&) { return CAPMGR_ERROR_DATABASE; }
  catch (const std::bad_alloc&) { return CAPMGR_ERROR_OUT_OF_MEMORY; }
  catch (...) { return CAPMGR_ERROR_IO; }
}
class PlatformAccessGate final : public capmgr::AccessGate {
 public:
  std::string AuthorizeAndGetDatabase() override {
    // P01 fails closed until verified TIDL/Cynara and DB policy integration.
    throw capmgr::Error(capmgr::ErrorCode::kPermission,
                        "Platform authorization is not configured");
  }
};
}
namespace capmgr {
int CreateClient(AccessGate& gate, capmgr_client_h* client) noexcept {
  if (!client) return CAPMGR_ERROR_INVALID_ARGUMENT;
  *client = nullptr;
  return Guard([&] { auto path=gate.AuthorizeAndGetDatabase();
                     *client=new capmgr_client(path); });
}
}
extern "C" {
int capmgr_client_create(capmgr_client_h* client) {
  PlatformAccessGate gate; return capmgr::CreateClient(gate,client);
}
int capmgr_client_destroy(capmgr_client_h client) {
  if (!client) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (client->callback_active.load()) return CAPMGR_ERROR_BUSY;
  delete client; return CAPMGR_OK;
}
int capmgr_client_foreach_capability(capmgr_client_h client, capmgr_kind_t kind,
                                     capmgr_foreach_cb callback, void* data) {
  if (!client || !callback) return CAPMGR_ERROR_INVALID_ARGUMENT;
  return Guard([&] { client->catalog.Foreach(static_cast<capmgr::Kind>(kind),
    [&](const auto& entry) {
      auto json=entry.dump();
      struct CallbackScope {
        std::atomic<bool>& active;
        explicit CallbackScope(std::atomic<bool>& flag) : active(flag) { active=true; }
        ~CallbackScope() { active=false; }
      } scope(client->callback_active);
      return callback(json.c_str(),data);
    }); });
}
int capmgr_client_search_capabilities(capmgr_client_h client, const char* query,
                                      capmgr_kind_t kind, capmgr_search_results_h* results) {
  if (results) *results=nullptr;
  if (!client || !query || !results) return CAPMGR_ERROR_INVALID_ARGUMENT;
  return Guard([&] {
    auto out=std::make_unique<capmgr_search_results>();
    for (const auto& entry : client->catalog.Search(query,static_cast<capmgr::Kind>(kind)))
      out->items.push_back(entry.dump());
    *results=out.release();
  });
}
void capmgr_search_results_free(capmgr_search_results_h results) { delete results; }
int capmgr_search_results_count(capmgr_search_results_h results, size_t* count) {
  if (count) *count=0;
  if (!results || !count) return CAPMGR_ERROR_INVALID_ARGUMENT;
  *count=results->items.size(); return CAPMGR_OK;
}
int capmgr_search_results_item(capmgr_search_results_h results, size_t index,
                                const char** json) {
  if (json) *json=nullptr;
  if (!results || !json) return CAPMGR_ERROR_INVALID_ARGUMENT;
  if (index>=results->items.size()) return CAPMGR_ERROR_NOT_FOUND;
  *json=results->items[index].c_str(); return CAPMGR_OK;
}
int capmgr_client_get_capability(capmgr_client_h client, const char* id, char** detail) {
  if (detail) *detail=nullptr;
  if (!client || !id || !detail) return CAPMGR_ERROR_INVALID_ARGUMENT;
  return Guard([&] {
    auto json=client->catalog.Get(id).dump();
    if (json.size()==SIZE_MAX) throw capmgr::Error(capmgr::ErrorCode::kLimit,"JSON too large");
    auto* out=static_cast<char*>(std::malloc(json.size()+1));
    if (!out) throw std::bad_alloc();
    std::memcpy(out,json.c_str(),json.size()+1); *detail=out;
  });
}
int capmgr_client_execute(capmgr_client_h client, const char* request,
                          capmgr_result_cb callback, void*, capmgr_request_token_t* token) {
  if (token) *token=0;
  if (!client || !request || !callback || !token) return CAPMGR_ERROR_INVALID_ARGUMENT;
  return CAPMGR_ERROR_NOT_SUPPORTED;
}
int capmgr_client_cancel(capmgr_client_h client, capmgr_request_token_t token) {
  if (!client || !token) return CAPMGR_ERROR_INVALID_ARGUMENT;
  return CAPMGR_ERROR_NOT_SUPPORTED;
}
int capmgr_client_remount_resources(capmgr_client_h client, const char* destination) {
  if (!client || !destination || destination[0]!='/') return CAPMGR_ERROR_INVALID_ARGUMENT;
  return CAPMGR_ERROR_NOT_SUPPORTED;
}
int capmgr_client_set_changed_callback(capmgr_client_h client, capmgr_changed_cb, void*) {
  if (!client) return CAPMGR_ERROR_INVALID_ARGUMENT;
  return CAPMGR_ERROR_NOT_SUPPORTED;
}
}
