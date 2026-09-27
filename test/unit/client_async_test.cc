// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "api/client.hh"
#include "launcher/action_exchange.hh"
#include <future>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
class Gate : public AccessGate {
 public:
  explicit Gate(std::string path) : path_(std::move(path)) {}
  std::string AuthorizeAndGetDatabase() override { return path_; }

 private:
  std::string path_;
};
class Backend : public ExecutionBackend {
 public:
  bool deny = false;
  bool SupportsCancel(const Entry&) const override { return true; }
  void Admit(const Entry& entry, const Request&) override {
    if (deny || entry.id != "cli:test")
      throw Error(ErrorCode::kPermission, "Denied fixture");
  }
  std::string Execute(const Entry&, const Request& request,
                      const std::atomic<bool>& cancel,
                      const Dispatcher::Emit& event) override {
    event(Json{{"jsonrpc", "2.0"}, {"id", request.id}, {"event", {{"seq", 1}}}}
              .dump());
    while (!cancel) std::this_thread::sleep_for(1ms);
    return Json{{"jsonrpc", "2.0"},
                {"id", request.id},
                {"error", {{"code", -32800}, {"message", "cancelled"}}}}
        .dump();
  }
};
}
TEST_F(CatalogTest, PublicAsyncCallbackCancellationAndDestroyLifetime) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make("test", "pkg.one", Kind::kCli)});
  Gate gate(path_);
  auto backend = std::make_shared<Backend>();
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, backend, &client), 0);
  struct State {
    capmgr_client_h client;
    std::promise<std::string> result;
    bool busy = false;
  } state{client, {}};
  auto callback = +[](uint64_t token, const char* json, bool event,
                      void* data) {
    auto& state = *static_cast<State*>(data);
    if (event) {
      state.busy = capmgr_client_destroy(state.client) == CAPMGR_ERROR_BUSY &&
                   capmgr_client_set_changed_callback(
                       state.client, nullptr, nullptr) == CAPMGR_ERROR_BUSY;
      EXPECT_EQ(capmgr_client_cancel(state.client, token), 0);
    } else
      state.result.set_value(json);
  };
  auto request =
      Json{{"jsonrpc", "2.0"},
           {"id", INT64_MIN},
           {"method", "tools/call"},
           {"params", {{"name", "cli:test"}, {"arguments", Json::object()}}}}
          .dump();
  uint64_t token = 0;
  ASSERT_EQ(
      capmgr_client_execute(client, request.c_str(), callback, &state, &token),
      0);
  EXPECT_EQ(token, 1u);
  auto result = state.result.get_future();
  ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(Json::parse(result.get())["id"], INT64_MIN);
  EXPECT_TRUE(state.busy);
  int status;
  do {
    status = capmgr_client_destroy(client);
    if (status == CAPMGR_ERROR_BUSY) std::this_thread::yield();
  } while (status == CAPMGR_ERROR_BUSY);
  EXPECT_EQ(status, 0);
}
TEST_F(CatalogTest, AdmissionFailureClearsTokenAndNeverCallsBack) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make("test", "pkg.one", Kind::kCli)});
  Gate gate(path_);
  auto backend = std::make_shared<Backend>();
  backend->deny = true;
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, backend, &client), 0);
  int calls = 0;
  auto callback = +[](uint64_t, const char*, bool, void* data) {
    ++*static_cast<int*>(data);
  };
  auto request =
      Json{{"jsonrpc", "2.0"},
           {"id", 7},
           {"method", "tools/call"},
           {"params", {{"name", "cli:test"}, {"arguments", Json::object()}}}}
          .dump();
  uint64_t token = 77;
  EXPECT_EQ(
      capmgr_client_execute(client, request.c_str(), callback, &calls, &token),
      CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(token, 0u);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(capmgr_client_execute(client, "[]", callback, &calls, &token),
            CAPMGR_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  EXPECT_EQ(calls, 0);
}
TEST_F(CatalogTest, ChangedCallbacksDeduplicateAndUnregisterQuiescesData) {
  Catalog writer(path_, Database::Access::kWriter);
  Gate gate(path_);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, &client), 0);
  std::promise<uint64_t> result;
  auto changed = +[](uint64_t revision, void* data) {
    static_cast<std::promise<uint64_t>*>(data)->set_value(revision);
  };
  ASSERT_EQ(capmgr_client_set_changed_callback(client, changed, &result), 0);
  NotifyChanged(client, 4);
  NotifyChanged(client, 4);
  NotifyChanged(client, 2);
  auto ready = result.get_future();
  ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(ready.get(), 4u);
  int status;
  do {
    status = capmgr_client_set_changed_callback(client, nullptr, nullptr);
    if (status == CAPMGR_ERROR_BUSY) std::this_thread::yield();
  } while (status == CAPMGR_ERROR_BUSY);
  EXPECT_EQ(status, 0);
  NotifyChanged(client, 5);
  EXPECT_EQ(capmgr_client_destroy(client), 0);
}

TEST_F(CatalogTest, InvalidBackendResponsesBecomeConfirmedTransportFailures) {
  class InvalidBackend : public ExecutionBackend {
   public:
    std::string reply;
    bool throws = false;
    void Admit(const Entry&, const Request&) override {}
    std::string Execute(const Entry&, const Request&, const std::atomic<bool>&,
                        const Dispatcher::Emit&) override {
      if (throws) throw std::runtime_error("fixture failure");
      return reply;
    }
  };
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make("test", "pkg.one", Kind::kCli)});
  Gate gate(path_);
  auto request =
      Json{{"jsonrpc", "2.0"},
           {"id", "original"},
           {"method", "tools/call"},
           {"params", {{"name", "cli:test"}, {"arguments", Json::object()}}}}
          .dump();
  for (
      const auto& [reply, cause] :
      std::vector<std::pair<std::string, std::string>>{
          {"not-json", "malformed response"},
          {"", "empty response"},
          {R"({"jsonrpc":"2.0","id":"wrong","result":true})",
           "response ID mismatch"},
          {std::string(1024 * 1024 + 1, 'x'), "response limit"},
          {"throw", "backend exception"},
          {" {\"jsonrpc\":\"2.0\",\"id\":\"original\",\"result\":{\"isError\":true}} ",
           "native"}}) {
    auto backend = std::make_shared<InvalidBackend>();
    backend->reply = reply;
    backend->throws = reply == "throw";
    capmgr_client_h client = nullptr;
    ASSERT_EQ(CreateClient(gate, backend, &client), 0);
    std::promise<std::string> result;
    auto callback = +[](uint64_t, const char* json, bool event, void* data) {
      if (!event)
        static_cast<std::promise<std::string>*>(data)->set_value(json);
    };
    uint64_t token = 0;
    ASSERT_EQ(capmgr_client_execute(client, request.c_str(), callback, &result,
                                    &token),
              0);
    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    auto bytes = future.get();
    auto json = Json::parse(bytes);
    EXPECT_EQ(json["id"], "original");
    if (cause == "native")
      EXPECT_EQ(bytes, reply);
    else
      EXPECT_EQ(json["error"]["data"]["cause"], cause);
    int status;
    do {
      status = capmgr_client_destroy(client);
    } while (status == CAPMGR_ERROR_BUSY);
    EXPECT_EQ(status, 0);
  }
}

TEST_F(CatalogTest, SubscriptionAckRetainsTokenAndClosedEventEndsLifetime) {
  class StreamBackend : public ExecutionBackend {
   public:
    bool SupportsCancel(const Entry&) const override { return true; }
    void Admit(const Entry&, const Request&) override {}
    void Run(const Entry& entry, const Request& request,
             const std::atomic<bool>& cancelled,
             const Dispatcher::EmitFrame& emit) override {
      ActionExchange exchange(entry, request, 7, true);
      auto send = [&](const char* text) {
        auto frame = exchange.Accept(7, text);
        emit({std::move(frame.json), frame.is_event, frame.complete});
      };
      send(
          R"({"jsonrpc":"2.0","id":7,"result":{"subscription":true,"isError":false}})");
      send(R"({"jsonrpc":"2.0","id":7,"event":{"seq":1,"isError":true}})");
      auto deadline = std::chrono::steady_clock::now() + 2s;
      while (!cancelled && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
      if (!cancelled) throw std::runtime_error("fixture cancellation deadline");
      send(
          R"({"jsonrpc":"2.0","id":7,"event":{"seq":2,"closed":"cancelled"}})");
    }
  };
  Catalog writer(path_, Database::Access::kWriter);
  auto entry = Make("stream", "pkg.one", Kind::kAction);
  entry.detail["eventSchema"] = {{"type", "object"}};
  Publish(writer, "pkg.one", {entry});
  Gate gate(path_);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, std::make_shared<StreamBackend>(), &client), 0);
  struct State {
    capmgr_client_h client;
    std::promise<void> done;
    std::vector<bool> kinds;
    std::vector<Json> replies;
  } state{client, {}, {}, {}};
  auto callback =
      +[](uint64_t token, const char* text, bool event, void* data) {
        auto& state = *static_cast<State*>(data);
        auto json = Json::parse(text);
        state.kinds.push_back(event);
        state.replies.push_back(json);
        if (json.contains("event") && !json["event"].contains("closed")) {
          EXPECT_EQ(capmgr_client_destroy(state.client), CAPMGR_ERROR_BUSY);
          EXPECT_EQ(capmgr_client_cancel(state.client, token), 0);
        }
        if (json.contains("event") && json["event"].contains("closed")) {
          EXPECT_EQ(capmgr_client_cancel(state.client, token),
                    CAPMGR_ERROR_NOT_FOUND);
        }
        if (json.contains("error") ||
            (json.contains("event") && json["event"].contains("closed")))
          state.done.set_value();
      };
  auto request =
      Json{{"jsonrpc", "2.0"},
           {"id", "public-id"},
           {"method", "tools/call"},
           {"params", {{"name", entry.id}, {"arguments", Json::object()}}}}
          .dump();
  uint64_t token = 0;
  ASSERT_EQ(
      capmgr_client_execute(client, request.c_str(), callback, &state, &token),
      0);
  auto future = state.done.get_future();
  ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
  int status;
  do {
    status = capmgr_client_destroy(client);
  } while (status == CAPMGR_ERROR_BUSY);
  EXPECT_EQ(status, 0);
  EXPECT_EQ(state.kinds, (std::vector<bool>{false, true, true}));
  ASSERT_EQ(state.replies.size(), 3u);
  for (const auto& reply : state.replies) EXPECT_EQ(reply["id"], "public-id");
  EXPECT_TRUE(state.replies[1]["event"]["isError"]);
  EXPECT_EQ(state.replies[2]["event"]["closed"], "cancelled");
}

TEST_F(CatalogTest, TerminalActionAckRejectedButIdenticalCliResultPreserved) {
  class AckBackend : public ExecutionBackend {
   public:
    void Admit(const Entry&, const Request&) override {}
    std::string Execute(const Entry&, const Request& request,
                        const std::atomic<bool>&,
                        const Dispatcher::Emit&) override {
      return Json{{"jsonrpc", "2.0"},
                  {"id", request.id},
                  {"result", {{"subscription", true}}}}
          .dump();
    }
  };
  Catalog writer(path_, Database::Access::kWriter);
  Gate gate(path_);
  for (auto kind : {Kind::kAction, Kind::kCli}) {
    auto entry = Make("ack", "pkg.one", kind);
    Publish(writer, "pkg.one", {entry});
    capmgr_client_h client = nullptr;
    ASSERT_EQ(CreateClient(gate, std::make_shared<AckBackend>(), &client), 0);
    std::promise<Json> result;
    auto request =
        Json{{"jsonrpc", "2.0"},
             {"id", 1},
             {"method", "tools/call"},
             {"params", {{"name", entry.id}, {"arguments", Json::object()}}}}
            .dump();
    uint64_t token = 0;
    ASSERT_EQ(capmgr_client_execute(
                  client, request.c_str(),
                  +[](uint64_t, const char* text, bool event, void* data) {
                    EXPECT_FALSE(event);
                    static_cast<std::promise<Json>*>(data)->set_value(
                        Json::parse(text));
                  },
                  &result, &token),
              0);
    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    auto reply = future.get();
    if (kind == Kind::kAction)
      EXPECT_EQ(reply["error"]["data"]["cause"],
                "terminal subscription acknowledgement");
    else
      EXPECT_EQ(reply["result"]["subscription"], true);
    int status;
    do {
      status = capmgr_client_destroy(client);
    } while (status == CAPMGR_ERROR_BUSY);
    EXPECT_EQ(status, 0);
  }
}

TEST_F(CatalogTest, DestroyIoRetainsHandleAndRejectsBeforeBackendAdmission) {
  class HeldBackend : public ExecutionBackend {
   public:
    std::promise<void> release, entered;
    std::shared_future<void> allowed = release.get_future().share();
    int admissions = 0;
    void Admit(const Entry&, const Request&) override { ++admissions; }
    std::string Execute(const Entry&, const Request& request,
                        const std::atomic<bool>&,
                        const Dispatcher::Emit&) override {
      entered.set_value();
      allowed.wait();
      return Json{{"jsonrpc", "2.0"}, {"id", request.id}, {"result", 0}}.dump();
    }
  };
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg.one", {Make("test", "pkg.one", Kind::kCli)});
  Gate gate(path_);
  auto backend = std::make_shared<HeldBackend>();
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, backend, &client), 0);
  std::atomic<int> callbacks = 0;
  auto callback = +[](uint64_t, const char*, bool, void* p) {
    ++*static_cast<std::atomic<int>*>(p);
  };
  auto request =
      Json{{"jsonrpc", "2.0"},
           {"id", 1},
           {"method", "tools/call"},
           {"params", {{"name", "cli:test"}, {"arguments", Json::object()}}}}
          .dump();
  uint64_t token = 0;
  ASSERT_EQ(capmgr_client_execute(client, request.c_str(), callback, &callbacks,
                                  &token),
            0);
  EXPECT_EQ(backend->entered.get_future().wait_for(2s),
            std::future_status::ready);
  auto before = std::chrono::steady_clock::now();
  EXPECT_EQ(capmgr_client_destroy(client), CAPMGR_ERROR_IO);
  EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
  token = 99;
  EXPECT_EQ(capmgr_client_execute(client, request.c_str(), callback, &callbacks,
                                  &token),
            CAPMGR_ERROR_BUSY);
  EXPECT_EQ(token, 0u);
  EXPECT_EQ(backend->admissions, 1);
  EXPECT_EQ(callbacks, 0);
  EXPECT_EQ(capmgr_client_set_changed_callback(client, nullptr, nullptr),
            CAPMGR_ERROR_BUSY);
  EXPECT_EQ(capmgr_client_destroy(client), CAPMGR_ERROR_IO);
  backend->release.set_value();
  int status;
  do {
    status = capmgr_client_destroy(client);
  } while (status == CAPMGR_ERROR_IO);
  EXPECT_EQ(status, 0);
  EXPECT_EQ(callbacks, 0);
}
