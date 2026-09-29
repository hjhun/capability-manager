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

// Separate build-only image. Reuse unchanged generated/default real Cancel route
// and its creator-context types; original C entry/modes remain compiled unchanged.
#include "../fixtures/read_policy_native_support.hh"
#include "../fixtures/read_policy_survivor_hold.hh"
#include "../fixtures/read_policy_survivor_publication.hh"

#include <limits>

namespace {

void PublishReferenceRecord(const std::string& path, const Json& record) {
  PublishSurvivorRecord(path, record.dump() + "\n");
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
    } tick{loop,
           factory.get(),
           &stub,
           hold,
           false,
           SurvivorClock::now() + std::chrono::seconds(20),
           root + "/stop-system301-platform"};
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
            g_main_loop_quit(t.loop);
            return G_SOURCE_REMOVE;
          }
          return G_SOURCE_CONTINUE;
        },
        &tick, nullptr);
    Check(g_source_attach(timer, context.value), "reference timer attach");
    reference.Validate();
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
    Check(Client(endpoint, role, false) == 10,
          "reader provisional reply/teardown");
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
