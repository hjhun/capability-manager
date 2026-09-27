// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "launcher/namespace_init.hh"
#include "launcher/owned_children.hh"
#include "launcher/worker_command.hh"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace capmgr {
// Trusted bounded registration snapshot prepared BEFORE worker admission and
// copied into the loop. Max256 entries, exact CLI IDs <=1024 bytes and paths
// <4096 bytes. Lookup performs no I/O, callbacks, locks or allocation. This is
// not authorization: production must independently load a trusted private catalog
// and invalidate/restart quiescent generations on changes before using a snapshot.
// That refresh/source trust path is not implemented by this fixture engine.
struct RegisteredCli { std::string id,executable; };
class WorkerRegistry {
 public:
  explicit WorkerRegistry(std::vector<RegisteredCli> entries);
  std::string_view Resolve(std::string_view id) const noexcept;
 private:
  std::vector<RegisteredCli> entries_;
};
// Trusted in-process spawn seam, never an IPC/plugin selection. Caller must
// durably reserve the token before sending START. Single-threaded worker only.
class WorkerRuntime {
 public:
  virtual ~WorkerRuntime()=default;
  // Returns clone's direct child or -1/errno. No allocation, Observe or callback
  // may occur after a positive clone return. Default uses NamespaceInit.
  virtual pid_t Spawn(NamespaceInitConfig& config,void* stack_top) noexcept;
};
enum class WorkerReplyKind : uint16_t { Accepted=1, Stdout=2, Stderr=3, Complete=4, State=5 };
enum class WorkerFailure : uint32_t {
  None=0, Rejected, Clone, Setup, Timeout, Cancelled, ParentLost, Protocol,
  OutputLimit, Backpressure, Channel
};
struct WorkerContext {
  uint64_t generation;
  int command_read, cancel_read, reply_write;
  // Front end supplies its own preopened trusted proc object. Numeric PID lookup
  // is forbidden here. Provenance/mount and fixed worker exec are caller gates.
  int parent_process;
  uid_t uid;
  gid_t gid;
  std::string smack_label;
};
struct WorkerLimits {
  std::chrono::milliseconds runtime{30000},setup{5000};
  size_t output_bytes=1024*1024;
};
// Single-threaded fixed worker only, SIGPIPE ignored, SIGCHLD default, no competing
// reaper. Not a daemon, public endpoint, authorization or resource-control policy.
// Step uses only the bounded in-memory registry, nonblocking pipe reads/writes
// and short ownership checks (not a hard wall-clock bound on OS scheduling);
// no waitpid without WNOHANG, StopAndWait, callback or whole-response flush.
// Every job has a separate 1MiB combined-output ceiling; pending transport queue
// has a fixed 128*4152-byte ceiling. Pressure closes admission and cancels jobs.
// CWR1 replies: LE version/kind u16, generation/sequence/token u64, payload-size,
// failure, exit-code, signal, errno and reserved u32; 56-byte header, <=4096 body.
// Complete is the ONLY positive no-child proof: sent after WNOWAIT/reap/Release
// (or proven no clone). Output without Complete is incomplete, never native success.
// Losing status retains frontend journal uncertainty even if this process later
// reaps everything. No automatic worker restart or reservation clearing is valid.
class WorkerLoop {
 public:
  using Clock=std::chrono::steady_clock;
  WorkerLoop(const WorkerContext&,const WorkerRegistry&,WorkerRuntime&,WorkerLimits={});
  ~WorkerLoop(); // Fail-stop with unreaped children; keep stepping during cleanup.
  WorkerLoop(const WorkerLoop&)=delete;
  WorkerLoop& operator=(const WorkerLoop&)=delete;
  void Step(Clock::time_point now=Clock::now()) noexcept;
  void Shutdown() noexcept;
  bool AdmissionOpen() const noexcept;
  bool Quiescent() const noexcept; // Closed + no owned child; NOT remote delivery.
  bool DeliveryLost() const noexcept;
  size_t Jobs() const noexcept;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
