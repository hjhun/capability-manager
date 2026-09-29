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

// Separate build-only image. Reuse unchanged generated/default real Cancel route
// and its creator-context types; original C entry/modes remain compiled unchanged.
#include "../fixtures/read_policy_native_support.hh"
#include "../fixtures/read_policy_survivor_hold.hh"
#include "../fixtures/read_policy_survivor_publication.hh"
#include "../fixtures/read_policy_route_observation.hh"

#include <aul.h>
#include <aul_proc.h>
#include <aul_rpc_port.h>

#include <limits>

namespace {

void PublishReferenceRecord(const std::string& path, const Json& record) {
  PublishSurvivorRecord(path, record.dump() + "\n");
}

bool observation_failed = false;

struct RouteCounts {
  unsigned created, rejected, services, cancel, other;
};

void ObserveRoute(const char* mode, const char* phase,
                  const std::string& endpoint, const char* original_stage,
                  const char* native_failure,
                  const RouteCounts* counts = nullptr) noexcept {
  try {
    char* own_name = nullptr;
    const int name_status = aul_proc_get_name(getpid(), &own_name);
    std::unique_ptr<char, decltype(&std::free)> name_guard(own_name, std::free);
    const auto name = CaptureRouteText(name_status == 0 ? own_name : nullptr);
    const bool client = std::string_view(mode) == "reader-hold";
    const uid_t query_uid = client ? 301 : aul_getuid();
    char* port_path = nullptr;
    const int path_status = aul_rpc_port_usr_get_path(
        endpoint.c_str(), "CapabilityManager", query_uid, &port_path);
    std::unique_ptr<char, decltype(&std::free)> path_guard(port_path,
                                                           std::free);
    const auto path = CaptureRouteText(path_status == 0 ? port_path : nullptr);
    const std::string expected =
        "/run/aul/rpcport/." + endpoint + "::CapabilityManager";
    auto name_report = name.Report();
    name_report["api_status"] = name_status;
    name_report["expected_match"] =
        name.complete && name.bytes == endpoint + (client ? ".client" : "");
    auto path_report = path.Report();
    path_report["api_status"] = path_status;
    path_report["expected"] = expected;
    path_report["expected_match"] = path.complete && path.bytes == expected;
    path_report["length_representable"] =
        expected.size() < sizeof(sockaddr_un{}.sun_path);
    Json record{
        {"stage", "reference-route-observation"},
        {"mode", mode},
        {"phase", phase},
        {"pid", getpid()},
        {"endpoint", endpoint},
        {"query_kind", client ? "independent" : "stub-source-query"},
        {"query_uid", query_uid},
        {"actual_proxy_target_path",
         client ? "NOT_OBSERVED" : "NOT_APPLICABLE"},
        {"native_stage", original_stage},
        {"native_exception", native_failure ? native_failure : "NONE"},
        {"own_name", std::move(name_report)},
        {"path", std::move(path_report)},
        {"metadata", RouteMetadata(path, path_status == 0 ? expected : "")},
        {"server_counts", nullptr}};
    if (counts)
      record["server_counts"] = {{"created", counts->created},
                                 {"rejected", counts->rejected},
                                 {"services", counts->services},
                                 {"cancel", counts->cancel},
                                 {"other", counts->other}};
    SurvivorEvidence(record.dump() + "\n");
    if (!client && std::string_view(phase) == "server-listen")
      Check(ReferenceServerSocketReady(path, path_status, expected,
                                       record.at("metadata")),
            "fresh root server socket creation prerequisite");
  } catch (...) {
    // Diagnostics must not replace native failure or escape a GLib callback.
    observation_failed = true;
    try {
      SurvivorEvidence(
          Json{{"stage", "reference-route-error"},
               {"mode", mode},
               {"phase", phase},
               {"pid", getpid()},
               {"native_stage", original_stage},
               {"native_exception", native_failure ? native_failure : "NONE"},
               {"reason", "observation-failed"}}
              .dump() +
          "\n");
    } catch (...) {
      // The caller still tears down and fails; missing output is not progress.
    }
  }
}

void ObserveClient(const char* phase, const std::string& endpoint, FixedRole,
                   const char* original_stage,
                   const char* native_failure) noexcept {
  ObserveRoute("reader-hold", phase, endpoint, original_stage, native_failure);
}

struct ReferenceFactory final : Stub::ServiceBase::Factory {
  std::string root;
  std::shared_ptr<Counts> counts = std::make_shared<Counts>();
  unsigned created = 0, rejected = 0;
  explicit ReferenceFactory(std::string value) : root(std::move(value)) {}
  void OnRejectedConnection(rpc_port_stub_h,
                            const std::string&) noexcept override {
    ++rejected;
  }
  std::unique_ptr<Stub::ServiceBase> CreateService(
      std::string sender, std::string instance) override {
    ++created;  // Never reset when a disconnected service disappears.
    return std::make_unique<Service>(std::move(sender), std::move(instance),
                                     counts, root, kPlatformRoles.front(),
                                     PublishReferenceRecord);
  }
  bool Zero() const {
    return !created && !rejected && !counts->cancel && !counts->other;
  }
};

void ObserveServer(ReferenceFactory& factory, Stub& stub, bool hold,
                   const char* phase, const std::string& endpoint) noexcept {
  try {
    RouteCounts counts{factory.created, factory.rejected,
                       static_cast<unsigned>(stub.GetServices().size()),
                       factory.counts->cancel, factory.counts->other};
    ObserveRoute(hold ? "server-hold" : "reader-server", phase, endpoint, stage,
                 nullptr, &counts);
  } catch (...) {
    observation_failed = true;
  }
}

int ReferenceServer(const std::string& root, const std::string& endpoint,
                    const RecoveryReference& reference, bool hold) {
  stage = "reference-server-registration";
  Context context;
  Registration registration(endpoint);
  auto factory = std::make_shared<ReferenceFactory>(root);
  GMainLoop* loop = g_main_loop_new(context.value, false);
  Check(loop, "reference server loop");
  std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop_guard(
      loop, &g_main_loop_unref);
  {
    stage = "reference-server-listen";
    Stub stub;
    stub.Listen(factory);
    Check(stub.GetServices().empty() && factory->Zero(),
          "initial Listen activity");
    struct Tick {
      GMainLoop* loop;
      ReferenceFactory* factory;
      Stub* stub;
      bool hold;
      bool failed = false;
      SurvivorClock::time_point end;
      std::string stop;
      std::string endpoint;
    } tick{loop,
           factory.get(),
           &stub,
           hold,
           false,
           SurvivorClock::now() + std::chrono::seconds(20),
           root + "/stop-system301-platform",
           endpoint};
    GSource* timer = g_timeout_source_new(50);
    Check(timer, "reference server timer");
    auto destroy = [](GSource* value) {
      g_source_destroy(value);
      g_source_unref(value);
    };
    std::unique_ptr<GSource, decltype(destroy)> timer_guard(timer, destroy);
    g_source_set_callback(
        timer,
        [](gpointer data) -> gboolean {
          auto& t = *static_cast<Tick*>(data);
          std::error_code error;
          const bool stop = !t.hold && std::filesystem::exists(t.stop, error);
          const auto decision = SurvivorDrainDecision(
              t.hold, stop, t.stub->GetServices().empty(), t.factory->Zero(),
              static_cast<bool>(error), SurvivorClock::now() >= t.end);
          if (decision == SurvivorDrain::kFail) t.failed = true;
          if (decision != SurvivorDrain::kContinue) {
            ObserveServer(*t.factory, *t.stub, t.hold, "server-terminal",
                          t.endpoint);
            // The bounded observer may still outlast the reader drain budget.
            // Hold expiry is a different completion rule; never extend t.end.
            if (!t.hold && SurvivorClock::now() >= t.end) t.failed = true;
            g_main_loop_quit(t.loop);
            return G_SOURCE_REMOVE;
          }
          return G_SOURCE_CONTINUE;
        },
        &tick, nullptr);
    Check(g_source_attach(timer, context.value), "reference timer attach");
    reference.Validate();
    ObserveServer(*factory, stub, hold, "server-listen", endpoint);
    Check(!observation_failed, "reference route observation");
    if (hold) {
      SurvivorEvidence(Json{{"stage", "server-hold"},
                            {"pid", getpid()},
                            {"listening", true},
                            {"services", 0},
                            {"cancel", 0},
                            {"other", 0},
                            {"rejected", 0},
                            {"created", 0}}
                           .dump() +
                       "\n");
    } else {
      PublishReferenceRecord(root + "/ready-system301-platform",
                             {{"stage", "ready"}});
    }
    stage = "reference-server-loop";
    g_main_loop_run(loop);
    Check(!observation_failed, "reference route observation");
    Check(!tick.failed && stub.GetServices().empty(),
          "reference empty service drain");
    if (hold)
      Check(factory->Zero(), "unexpected server-only activity");
    else
      Check(factory->created == 1 && !factory->rejected &&
                factory->counts->cancel == 1 && !factory->counts->other &&
                std::filesystem::exists(tick.stop),
            "reader server activity/drain");
    reference.Validate();
    // timer_guard is destroyed before Tick, Stub, loop and creator context.
  }
  registration.Close();
  return 0;
}
}  // namespace

extern "C" __attribute__((visibility("default"))) int
CapmgrReferenceModuleFixture(const char* kind_arg, const char* root_arg,
                             const char* endpoint_arg, uint64_t device,
                             uint64_t inode) noexcept {
  try {
    Check(kind_arg && root_arg && endpoint_arg &&
              device <= std::numeric_limits<dev_t>::max() &&
              inode <= std::numeric_limits<ino_t>::max(),
          "reference entry arguments");
    const std::string kind(kind_arg), root(root_arg), endpoint(endpoint_arg);
    Check(ReferenceModuleTopology(root, endpoint, getppid()),
          "fixed reference topology");
    Check(kind == "server-hold" || kind == "reader-server" ||
              kind == "reader-hold",
          "fixed reference native mode");
    // Launcher validated strict topology and parent-bound endpoint BEFORE load.
    // Do not rebind authority to a changed parent after intentional adoption.
    struct stat expected{};
    expected.st_dev = device;
    expected.st_ino = inode;
    expected.st_uid = expected.st_gid = 0;
    expected.st_nlink = 1;
    expected.st_mode = S_IFREG | 0600;
    RecoveryReference reference(expected, RecoveryReference::State::kMarked);
    if (kind != "reader-hold") {
      uid_t r, e, s;
      gid_t rg, eg, sg;
      Check(!getresuid(&r, &e, &s) && !r && !e && !s &&
                !getresgid(&rg, &eg, &sg) && !rg && !eg && !sg &&
                TaskLabel() == "User::Shell",
            "never-drop reference server");
      return ReferenceServer(root, endpoint, reference, kind == "server-hold");
    }
    const auto role = kPlatformRoles.front();
    VerifyContext(role.label, role.uid, true);
    stage = "reference-reader-real-rpc";
    Check(Client(endpoint, role, false, ObserveClient) == 10,
          "reader provisional reply/teardown");
    Check(!observation_failed, "reference route observation");
    // Client's proxy/listener/context and registration are physically retired.
    VerifyContext(role.label, role.uid, true);
    reference.Validate();
    const auto end = SurvivorClock::now() + std::chrono::seconds(20);
    SurvivorEvidence(
        Json{{"stage", "reader-hold"},
             {"pid", getpid()},
             {"role", role.name},
             {"token", std::string(role.name) + ":" + std::to_string(getpid())},
             {"reply", -6},
             {"teardown", true}}
            .dump() +
        "\n");
    stage = "reference-reader-hold";
    SurvivorHold(
        end);  // No stdin/HUP processing, exec/fork or new credential transition.
    return 0;  // Dedicated survivor exit; original Client10/20 stays unchanged.
  } catch (const std::exception& error) {
    std::cerr << "REFERENCE_NATIVE_FAIL stage=" << stage << " " << error.what()
              << std::endl;
  } catch (...) {
    std::cerr << "REFERENCE_NATIVE_FAIL unknown exception" << std::endl;
  }
  return 1;  // Borrowed FD4 and successful module mapping remain to kernel exit.
}
