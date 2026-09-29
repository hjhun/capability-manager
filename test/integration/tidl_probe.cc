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

// Fixture only: proves generated transport authorization, not a product service.
#include "capability_manager_stub.h"
#include "capability_manager_proxy.h"

#include <rpc-port-internal.h>
#include <glib.h>

#include <fstream>
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <unistd.h>

using Stub = rpc_port::capability_manager_stub::stub::CapabilityManager;
using Proxy = rpc_port::capability_manager_proxy::proxy::CapabilityManager;

namespace {

std::atomic<int> warm_fds{0};
int FdCount() {
  return static_cast<int>(
      std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                    std::filesystem::directory_iterator{}));
}

class Service : public Stub::ServiceBase {
 public:
  Service(std::string sender, std::string instance)
      : ServiceBase(std::move(sender), std::move(instance)) {}
  void OnCreate() override {}
  void OnTerminate() override {}
  std::string AuthorizeCatalog() override {
    if (warm_fds.load() == 0) warm_fds.store(FdCount());
    return std::to_string(MainPrincipal()->pid());
  }

  int ConfirmCatalog(std::string) override { return -6; }
  int Execute(std::string, std::string) override { return -6; }
  int Cancel(std::string) override { return -6; }
  int RemountResources(std::string) override { return -6; }
  int RegisterReply(std::unique_ptr<Stub::Reply>) override { return -6; }
  bool UnregisterReply(int) override { return false; }
  int RegisterChanged(std::unique_ptr<Stub::Changed>) override { return -6; }
  bool UnregisterChanged(int) override { return false; }
};

class Factory : public Stub::ServiceBase::Factory {
 public:
  std::unique_ptr<Stub::ServiceBase> CreateService(
      std::string sender, std::string instance) override {
    return std::make_unique<Service>(std::move(sender), std::move(instance));
  }

  void OnRejectedConnection(rpc_port_stub_h stub,
                            const std::string& instance) noexcept override {
    try {
      rejected.emplace_back(stub, instance);
    } catch (...) {
      std::abort();
    }
  }

  size_t RetainedPorts() const {
    size_t count = 0;
    for (const auto& [stub, instance] : rejected) {
      rpc_port_h port = nullptr;
      if (rpc_port_stub_get_port(stub, RPC_PORT_PORT_MAIN, instance.c_str(),
                                 &port) == 0)
        ++count;
      if (rpc_port_stub_get_port(stub, RPC_PORT_PORT_CALLBACK, instance.c_str(),
                                 &port) == 0)
        ++count;
    }
    return count;
  }
  std::vector<std::pair<rpc_port_stub_h, std::string>> rejected;
};

class Listener : public Proxy::IEventListener {
 public:
  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }
  void OnRejected() override { connected = false; }
  bool connected = false;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  try {
    std::string mode = argv[1], endpoint = argv[2];
    if (mode == "server") {
      if (rpc_port_register_proc_info(endpoint.c_str(), nullptr)) return 3;
      GMainLoop* loop = g_main_loop_new(nullptr, false);
      {
        Stub stub;
        auto factory = std::make_shared<Factory>();
        stub.Listen(factory);
        std::ofstream ready(argv[3]);
        ready << "ready\n";
        ready.close();
        g_timeout_add_seconds(
            15,
            [](void* data) -> gboolean {
              g_main_loop_quit(static_cast<GMainLoop*>(data));
              return false;
            },
            loop);
        g_main_loop_run(loop);
        auto retained = factory->RetainedPorts();
        auto services = stub.GetServices().size();
        auto final_fds = FdCount();
        std::cout << "rejected=" << factory->rejected.size()
                  << " retained_ports=" << retained << " services=" << services
                  << " final_fds=" << final_fds
                  << " warm_fds=" << warm_fds.load() << "\n";
        if (factory->rejected.size() != 32 || retained || services ||
            warm_fds.load() == 0 || final_fds > warm_fds.load())
          return 8;
      }
      g_main_loop_unref(loop);
      return 0;
    }
    if (mode == "rejected-client") {
      if (setuid(1))
        return 9;  // TIDL's system-UID shortcut cannot bypass our check.
      if (rpc_port_register_proc_info((endpoint + ".denied").c_str(), nullptr))
        return 4;
      for (int i = 0; i < 32; ++i) {
        bool connected = false;
        try {
          Listener listener;
          Proxy proxy(&listener, endpoint);
          proxy.Connect(true);
          connected = listener.connected;
          if (!connected) return 10;
          proxy.AuthorizeCatalog();
          return 11;  // any accepted method is a failure
        } catch (...) {
          if (!connected) return 12;
        }
      }
      std::cout << "32 connected system-UID requests rejected\n";
      return 0;
    }
    if (mode != "client") return 2;
    if (rpc_port_register_proc_info((endpoint + ".client").c_str(), nullptr))
      return 4;
    Listener listener;
    Proxy proxy(&listener, endpoint);
    proxy.Connect(true);
    if (!listener.connected) return 5;
    auto result = proxy.AuthorizeCatalog();
    if (result != std::to_string(getpid()) ||
        proxy.RemountResources("/tmp/not-mounted") != -6)
      return 6;
    proxy.Disconnect();
    std::cout
        << "TIDL MAIN/callback principal matched; real policy passed; remount unsupported\n";
    return 0;
  } catch (...) {
    std::cerr << "TIDL fixture exception\n";
    return 7;
  }
}
