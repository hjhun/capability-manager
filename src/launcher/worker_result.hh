// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "launcher/worker_session.hh"
#include "launcher/runner.hh"
namespace capmgr {
enum class WorkerResultBuildStage { Parse, Compare, Failure };
// Trusted test-only fault seam, never selected by IPC; must outlive collector.
struct WorkerResultOperations {
  virtual ~WorkerResultOperations()=default;
  virtual void BeforeBuild(WorkerResultBuildStage) {}
};
// Private collector, not an ExecutionBackend or cleanup coordinator. Construct
// before admission; under coordinator serialization Start exactly Original(),
// Bind the returned worker token, then permit Step. Bind allocates nothing.
// Feed ONLY events decoded by that WorkerSession: its Complete already persisted
// ConfirmJobGone. This object cannot authenticate a raw frame or prove cleanup.
// Caller serializes all operations and retains the client-token association.
// Coordinator consumes session-validated retired-token State/Rejected/ENOENT
// cancellation acknowledgements before routing; NEVER send State to a collector.
// Those late records neither reopen a sealed collector nor produce a terminal.
class WorkerResult {
 public:
  WorkerResult(uint64_t client_token,const std::string& request,WorkerResultOperations* operations=nullptr);
  WorkerResult(const WorkerResult&)=delete;
  WorkerResult& operator=(const WorkerResult&)=delete;
  const std::string& Original() const noexcept {return request_.original;}
  uint64_t ClientToken() const noexcept {return client_token_;}
  bool Bind(uint64_t worker_token) noexcept;
  // Separate provisional streams, <=1MiB combined. Only a matching Complete
  // yields a result, once. Native bytes survive unchanged, including isError and
  // ordinary nonzero exit. Worker failure or signal wins over provisional JSON.
  std::optional<RunResult> Accept(const WorkerEvent&);
  // If terminal materialization throws, retain the already confirmed Complete
  // and both streams. No further event is accepted. Retry needs no new cleanup
  // evidence and may produce exactly one result; only then Complete() is true.
  // Caller retains delivery ownership/backpressure while TerminalPending().
  RunResult RetryTerminal();
  bool TerminalPending() const noexcept {return terminal_pending_;}
  bool NeedsCancellation() const noexcept;
  bool Complete() const noexcept {return complete_;}
  bool Uncertain() const noexcept {return uncertain_;}
  // Before confirmed Complete: no terminal, reset or numeric-PID absence
  // inference. A terminal-pending proof already received is preserved instead.
  // Caller keeps durable
  // uncertainty and cleanup coordination; it MUST NOT simply return/throw from
  // current Dispatcher framed work, which would synthesize an early terminal.
  // Production bounded destroy/retry requires a separately reviewed coordinator.
  void LoseSession() noexcept;
 private:
  RunResult BuildTerminal();
  Request request_;
  WorkerResultOperations* operations_;
  WorkerEvent terminal_{};
  uint64_t client_token_,worker_token_=0;
  std::array<std::string,2> streams_;
  size_t total_=0;
  const char* failure_=nullptr;
  bool complete_=false,uncertain_=false,terminal_pending_=false;
};
}
