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

#ifndef CAPABILITY_MANAGER_LAUNCHER_WORKER_SESSION_HH_
#define CAPABILITY_MANAGER_LAUNCHER_WORKER_SESSION_HH_

#include "launcher/broker_journal.hh"
#include "launcher/worker_loop.hh"

#include <array>
#include <memory>
#include <optional>
#include <string_view>

namespace capmgr {

struct WorkerEvent {
  WorkerReplyKind kind;
  uint64_t token;
  WorkerFailure failure;
  int code, signal, error;
  std::array<char, 4096> bytes{};
  size_t size = 0;
  std::string_view Data() const noexcept { return {bytes.data(), size}; }
};

// Private frontend end of one trusted worker generation. NOT an authorization,
// spawn, public result adapter, worker-death proof or restart mechanism. Exclusive
// journal ownership is required for this object's lifetime; journal outlives it.
// Calls are internally serialized; destruction requires caller quiescence.
// Constructor duplicates trusted anonymous pipes (write/write/read), then fsyncs
// BeginGeneration BEFORE returning. Caller closes its original pipe aliases and
// spawns the fixed worker only after this succeeds, passing Generation().
// SIGPIPE must be ignored. No callback runs under the lock. Start/Step/Abort can
// perform journal fsync: run them in frontend coordination, NEVER the worker's
// single-threaded cleanup loop. Disk stalls are not claimed to be bounded.
// Partial input and untransmitted records have a five-second deadline.
// Step performs <=one read and one write per outbound channel; retained memory is
// four <=64KiB START records, four 40-byte CANCELs and one <=4152-byte reply.
// At most four exact sent-CANCEL late-State entitlements are retained. A
// Cancelled Complete consumes its entitlement; other Complete outcomes retain it
// until one matching State/Rejected/ENOENT or clean EOF. Exhaustion poisons the
// generation rather than accepting unmatched/duplicate State or guessing delivery.
// Output events are provisional bytes. Only Complete proves namespace cleanup;
// a separate adapter must validate both native streams before reporting success.
class WorkerSession {
 public:
  using Clock = std::chrono::steady_clock;
  WorkerSession(BrokerJournal&, int command_write, int cancel_write,
                int reply_read);
  ~WorkerSession();  // Unfinished generation is made uncertain; never reset here.
  WorkerSession(const WorkerSession&) = delete;
  WorkerSession& operator=(const WorkerSession&) = delete;
  uint64_t Generation() const;
  uint64_t Start(
      const std::string& request);  // durable Reserve before any bytes
  void Cancel(
      uint64_t token);  // idempotent while outstanding; independent pipe
  std::optional<WorkerEvent> Step(Clock::time_point now = Clock::now());
  void
  Abort() noexcept;  // close admission/channels, persist uncertainty best effort
  bool Failed() const;
  // Only when all jobs completed and both outbound queues drained. Closes owned
  // write ends; external owner must then observe/reap its direct worker normally.
  void PrepareStop();
  // Trusted integration proof input, NOT callable from IPC. Requires PrepareStop,
  // clean reply EOF (Step drains buffered frames even with HUP), and caller has
  // observed/reaped worker exit0. Clean EOF may itself prepare the stop. Does not
  // infer old-job absence from that exit: every job already required Complete.
  void ConfirmNormalExit();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_LAUNCHER_WORKER_SESSION_HH_
