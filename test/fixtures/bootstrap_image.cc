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

// Build-only, explicit development-image fixture. No install target or service.
// This fixed policy is for the known root/User::Shell test launcher and root-owned
// fixture DB ONLY. It is not the provisioned live catalog or production worker.
#include "launcher/worker_bootstrap.hh"
#include "bootstrap_policy.hh"
#include "launcher/worker_catalog.hh"
#include "launcher/worker_supervisor.hh"

#include <charconv>
#include <cstring>

#include <grp.h>
#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace capmgr;
using namespace capmgr::fixture;
int main(int argc, char** argv) {
  try {
    Require(argc == 3 && !std::strcmp(argv[1], "--generation"));
    uint64_t generation = 0;
    auto* end = argv[2] + std::strlen(argv[2]);
    auto parsed = std::from_chars(argv[2], end, generation);
    Require(parsed.ec == std::errc{} && parsed.ptr == end && generation);
    WorkerInitialNamespaces namespaces;
    Reduce();
    WorkerBootstrapPolicy policy{kCaps, {}, "User::Shell", namespaces};
    ValidateWorkerBootstrap(policy);
    auto snapshot = LoadWorkerCatalog(
        7, {0, 0, 0700, 0600});  // fixture root-only publication
    WorkerRuntime runtime;
    WorkerLoop loop({generation, 3, 4, 5, 6, 301, 301, "System"},
                    snapshot.registry, runtime);
    FinishWorkerBootstrap(
        policy,
        loop.StartupDescriptors());  // SQLite is closed, fixed runtime loaded, one task
    auto ready = EncodeWorkerReady(generation, snapshot.revision);
    ssize_t sent;
    do {
      sent = write(8, ready.data(), ready.size());
    } while (sent < 0 && errno == EINTR);
    Require(sent == static_cast<ssize_t>(ready.size()));
    Require(!close(8));
    // The loop owns CLOEXEC duplicates. No inherited writer alias survives here.
    for (int fd = 3; fd <= 7; ++fd) Require(!close(fd));
    for (;;) {
      loop.Step();
      if (loop.CanExitCleanly()) return 0;
      if (loop.DeliveryLost() && loop.Quiescent() && !loop.Jobs()) return 125;
      usleep(1000);
    }
  } catch (...) {
    return 125;
  }
}
