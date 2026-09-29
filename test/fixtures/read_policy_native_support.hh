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

#ifndef CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_NATIVE_SUPPORT_HH_
#define CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_NATIVE_SUPPORT_HH_

#include "read_policy_context.hh"
#include "platform/tidl_channels.hh"
#include "capability_manager_proxy.h"
#include "capability_manager_stub.h"

#include <glib.h>
#include <rpc-port-internal.h>

#include <thread>

using namespace capmgr;
using namespace capmgr::fixture::realpolicy;
using Stub = rpc_port::capability_manager_stub::stub::CapabilityManager;
using Proxy = rpc_port::capability_manager_proxy::proxy::CapabilityManager;
namespace {

const char* stage = "native-startup";

struct Context {
  const std::thread::id owner = std::this_thread::get_id();
  GMainContext* value = g_main_context_new();
  Context() {
    Check(value, "private context");
    g_main_context_push_thread_default(value);
  }
  ~Context() {
    CheckOwner();
    g_main_context_pop_thread_default(value);
    g_main_context_unref(value);
  }
  void CheckOwner() const noexcept {
    if (owner != std::this_thread::get_id() ||
        g_main_context_get_thread_default() != value)
      std::terminate();
  }
};

struct Listener : Proxy::IEventListener {
  explicit Listener(Context& value) : context(value) {}
  Context& context;
  bool connected = false;
  int calls = 0;
  void OnConnected() override {
    context.CheckOwner();
    connected = true;
    ++calls;
  }
  void OnDisconnected() override {
    context.CheckOwner();
    connected = false;
    ++calls;
  }
  void OnRejected() override {
    context.CheckOwner();
    connected = false;
    ++calls;
  }
};

struct Registration {
  bool active = false;
  explicit Registration(const std::string& endpoint) {
    Check(!rpc_port_register_proc_info(endpoint.c_str(), nullptr),
          "process registration");
    active = true;
  }
  ~Registration() {
    if (active && rpc_port_deregister_proc_info())
      std::cerr << "PROC_DEREGISTER_FAILED\n";
  }
  void Close() {
    Check(!rpc_port_deregister_proc_info(), "process deregistration");
    active = false;
  }
};

void WriteRecord(const std::string& name, const Json& value) {
  auto bytes = value.dump() + "\n";
  Check(bytes.size() <= 4096, "fixture record limit");
  int fd = open(name.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  Check(fd >= 0, "fixture record creation");
  size_t offset = 0;
  while (offset != bytes.size()) {
    ssize_t n = write(fd, bytes.data() + offset, bytes.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      close(fd);
      throw std::runtime_error("fixture record write");
    }
    offset += static_cast<size_t>(n);
  }
  int mode = fchmod(fd, 0600);
  int closed = close(fd);
  Check(!mode && !closed, "fixture record mode/close");
}
struct Counts {
  unsigned cancel = 0;
  unsigned other = 0;
};

class Service final : public Stub::ServiceBase {
 public:
  Service(std::string sender, std::string instance,
          std::shared_ptr<Counts> count, std::string root, FixedRole role,
          void (*record_writer)(const std::string&, const Json&) = WriteRecord)
      : ServiceBase(std::move(sender), std::move(instance)),
        count_(std::move(count)),
        root_(std::move(root)),
        role_(role),
        record_writer_(record_writer) {}
  void OnCreate() noexcept override {}
  void OnTerminate() noexcept override {}
  int Cancel(std::string token) override {
    ++count_->cancel;
    auto peer = MainPrincipal();
    Check(peer && peer->Connected() && peer->uid() == role_.uid &&
              peer->gid() == role_.uid &&
              peer->security_label() == role_.label &&
              token ==
                  std::string(role_.name) + ":" + std::to_string(peer->pid()) &&
              count_->cancel == 1 && count_->other == 0,
          "unexpected diagnostic caller/method");
    // The generated route has already checked actual MAIN/callback and real
    // all-UID MAIN Cynara. Do not call/inject a replacement allow policy.
    Json record{{"stage", "server-cancel-body"},
                {"role", role_.name},
                {"pid", peer->pid()},
                {"uid", peer->uid()},
                {"gid", peer->gid()},
                {"socket_label", peer->security_label()},
                {"token", token},
                {"cancel_calls", count_->cancel},
                {"other_calls", count_->other}};
    record_writer_(root_ + "/body-" + role_.name, record);
    std::cout << "REAL_GATE_BODY=" << record.dump() << std::endl;
    return -6;  // Unsupported probe, never cancellation/termination success.
  }
  std::string AuthorizeCatalog() override {
    ++count_->other;
    return {};
  }
  int ConfirmCatalog(std::string) override {
    ++count_->other;
    return -6;
  }
  int Execute(std::string, std::string) override {
    ++count_->other;
    return -6;
  }
  int RemountResources(std::string) override {
    ++count_->other;
    return -6;
  }
  int RegisterReply(std::unique_ptr<Stub::Reply>) override {
    ++count_->other;
    return -6;
  }
  bool UnregisterReply(int) override {
    ++count_->other;
    return false;
  }
  int RegisterChanged(std::unique_ptr<Stub::Changed>) override {
    ++count_->other;
    return -6;
  }
  bool UnregisterChanged(int) override {
    ++count_->other;
    return false;
  }

 private:
  std::shared_ptr<Counts> count_;
  std::string root_;
  FixedRole role_;
  void (*const record_writer_)(const std::string&, const Json&);
};

class Factory final : public Stub::ServiceBase::Factory {
 public:
  Factory(std::string root, FixedRole role)
      : root_(std::move(root)), role_(role) {}
  std::shared_ptr<Counts> counts = std::make_shared<Counts>();
  unsigned rejected = 0;
  void OnRejectedConnection(rpc_port_stub_h,
                            const std::string&) noexcept override {
    ++rejected;  // Bind rejection, not a specific Cynara reason.
  }
  std::unique_ptr<Stub::ServiceBase> CreateService(
      std::string sender, std::string instance) override {
    return std::make_unique<Service>(std::move(sender), std::move(instance),
                                     counts, root_, role_);
  }

 private:
  std::string root_;
  FixedRole role_;
};

[[maybe_unused]] int Server(const std::string& root,
                            const std::string& endpoint, FixedRole role) {
  stage = "server-registration";
  Context context;
  Registration registration(endpoint);
  auto factory = std::make_shared<Factory>(root, role);
  GMainLoop* loop = g_main_loop_new(context.value, false);
  Check(loop, "server main loop");
  std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop_guard(
      loop, &g_main_loop_unref);
  {
    stage = "server-listen";
    Stub stub;
    stub.Listen(factory);
    struct Tick {
      std::string stop;
      GMainLoop* loop;
      Stub* stub;
    };
    Tick tick{root + "/stop-" + role.name, loop, &stub};
    GSource* timer = g_timeout_source_new(100);
    Check(timer, "server timer");
    auto destroy_source = [](GSource* source) {
      g_source_destroy(source);
      g_source_unref(source);
    };
    std::unique_ptr<GSource, decltype(destroy_source)> timer_guard(
        timer, destroy_source);
    g_source_set_callback(
        timer,
        [](gpointer data) -> gboolean {
          auto& t = *static_cast<Tick*>(data);
          if (std::filesystem::exists(t.stop) && t.stub->GetServices().empty())
            g_main_loop_quit(t.loop);
          return G_SOURCE_CONTINUE;
        },
        &tick, nullptr);
    Check(g_source_attach(timer, context.value), "server timer attachment");
    WriteRecord(root + "/ready-" + role.name, {{"stage", "ready"}});
    stage = "server-loop";
    g_main_loop_run(loop);
    Check(stub.GetServices().empty() && factory->counts->other == 0,
          "services/unused-method drain");
    std::cout << "REAL_GATE_SERVER_DRAINED role=" << role.name
              << " cancel=" << factory->counts->cancel
              << " rejected_bind=" << factory->rejected << std::endl;
  }
  registration.Close();
  return 0;
}
using ClientObservation = void (*)(const char*, const std::string&, FixedRole,
                                   const char*, const char*) noexcept;

int Client(const std::string& endpoint, FixedRole role, bool context_only,
           ClientObservation observe = nullptr) {
  stage = "client-context-postcondition";
  Check(!context_only, "context-only must stay module-free");
  VerifyContext(role.label, role.uid, role.platform_group);
  stage = "client-registration";
  Registration registration(endpoint + ".client");
  bool positive = false;
  {
    // Listener outlives proxy; private uniterated creator context outlives both.
    Context context;
    Listener listener(context);
    auto proxy = std::make_unique<Proxy>(&listener, endpoint);
    const char* native_failure = nullptr;
    try {
      stage = "client-connect";
      if (observe) observe("client-preconnect", endpoint, role, stage, nullptr);
      proxy->Connect(true);
      Check(listener.connected, "connect listener unavailable");
      stage = "client-method-reply";
      int result = proxy->Cancel(std::string(role.name) + ":" +
                                 std::to_string(getpid()));
      Check(result == -6, "unexpected fixed method result");
      positive = true;
      std::cout << "REAL_GATE_CLIENT_REPLY role=" << role.name
                << " pid=" << getpid() << " result=" << result << std::endl;
    } catch (const rpc_port::capability_manager_proxy::proxy::
                 PermissionDeniedException&) {
      native_failure = "PermissionDeniedException";
    } catch (
        const rpc_port::capability_manager_proxy::proxy::InvalidIOException&) {
      native_failure = "InvalidIOException";
    } catch (const rpc_port::capability_manager_proxy::proxy::
                 InvalidProtocolException&) {
      native_failure = "InvalidProtocolException";
    }
    const char* const original_stage = stage;
    if (native_failure && observe)
      observe("client-native-failure", endpoint, role, original_stage,
              native_failure);
    if (native_failure)
      std::cout << "REAL_GATE_NOT_PROVED role=" << role.name
                << " stage=" << original_stage << " native=" << native_failure
                << " specific_Cynara_decision=NOT_OBSERVED" << std::endl;
    stage = "client-teardown";
    if (listener.connected) proxy->Disconnect();
    proxy.reset();
  }
  registration.Close();
  return positive ? 10 : 20;  // Neither is an ordinary method failure mapping.
}

}  // namespace

#endif  // CAPABILITY_MANAGER_TEST_FIXTURES_READ_POLICY_NATIVE_SUPPORT_HH_
