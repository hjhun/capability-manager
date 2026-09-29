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

// Separate build-only no-job fixture. Never installed or run by CTest.
#include "launcher/leased_worker_loop.hh"

#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <exception>
#include <string_view>
#include <utility>

#include "bootstrap_policy.hh"
#include "leased_bootstrap_paths.hh"
#include "launcher/worker_supervisor.hh"

#ifdef CAPMGR_LEASED_BOOTSTRAP_CLOSE_FAULT
void ArmLeasedBootstrapCloseFault();
bool LeasedBootstrapCloseFaultFired();
#endif

int main(int argc, char** argv) {
  using namespace capmgr;
  const char* stage = "arguments";
  try {
    fixture::Require(argc == 3 && !std::strcmp(argv[1], "--generation"));
    uint64_t generation = 0;
    const auto* end = argv[2] + std::strlen(argv[2]);
    const auto parsed = std::from_chars(argv[2], end, generation);
    fixture::Require(parsed.ec == std::errc{} && parsed.ptr == end &&
                     generation);
    // Captures initial PID1 witnesses while privileged, before Reduce. No later
    // path helper reopens cross-label PID1 namespaces or changes capabilities.
    stage = "namespace capture";
    WorkerInitialNamespaces namespaces;
    fixture::Reduce();
    WorkerBootstrapPolicy bootstrap{
        fixture::kCaps, {}, "User::Shell", namespaces};
    ValidateWorkerBootstrap(bootstrap);
    stage = "private fixture paths";
    const auto paths = fixture::leasedbootstrap::ResolveCatalog(7);
    ReadLeasePolicy read_policy{paths.catalog,
                                paths.lock,
                                0,
                                0,
                                0,
                                0,
                                0700,
                                0600,
                                0600,
                                "User::Shell",
                                "User::Shell",
                                "User::Shell"};
    WorkerRuntime runtime;
#ifdef CAPMGR_LEASED_BOOTSTRAP_CLOSE_FAULT
    ArmLeasedBootstrapCloseFault();
#endif
    stage = "factory";
    auto owner = LeasedWorkerLoop::LoadAndFinish(
        bootstrap, {generation, 3, 4, 5, 6, 301, 301, "System"},
        std::move(read_policy), runtime);
    stage = "ready";
    const auto ready = EncodeWorkerReady(generation, owner->Revision());
    ssize_t sent;
    do {
      sent = write(8, ready.data(), ready.size());
    } while (sent < 0 && errno == EINTR);
    fixture::Require(sent == static_cast<ssize_t>(ready.size()) && !close(8));
    for (int fd = 3; fd <= 7; ++fd) fixture::Require(!close(fd));
    // No START is sent by this fixture. Parent loss/output error remains an
    // abnormal local retirement, never clean generation success.
    stage = "drain";
    const auto deadline = WorkerLoop::Clock::now() + std::chrono::seconds(15);
    while (WorkerLoop::Clock::now() < deadline) {
      owner->Step();
      if (owner->CanExitCleanly()) {
        owner->RetireCleanly();
        return 0;
      }
      if (owner->DeliveryLost() && owner->Quiescent()) {
        owner->RetireAfterDeliveryLoss();
        return 125;
      }
      usleep(1000);
    }
    // Ready-owner destruction intentionally fail-stops if retirement is unsafe.
    return 125;
  } catch (const std::exception& error) {
#ifdef CAPMGR_LEASED_BOOTSTRAP_CLOSE_FAULT
    // Dedicated source-backed status proves BOTH the armed non-null real close
    // fired and the typed table gate rejected. Other setup failures stay125.
    if (!std::strcmp(stage, "factory") && LeasedBootstrapCloseFaultFired() &&
        std::string_view(error.what()).find("bootstrap unexpected FD") !=
            std::string_view::npos)
      return 126;
#else
    (void)stage;
    (void)error;
#endif
    return 125;
  } catch (...) {
    return 125;
  }
}
