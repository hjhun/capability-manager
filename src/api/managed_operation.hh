// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
namespace capmgr {
// Trusted private coordinator, never IPC data. The only coordination thread is
// owned here; subclasses do not spawn threads/native callbacks outside this table.
// Construction/token binding have no side effects. Coordinate may START only
// after Dispatcher binds the client token. All Session/Journal work (including
// their destruction) belongs in Coordinate, not a destructor or snapshot reader.
struct ManagedPublicationOperations {
  virtual ~ManagedPublicationOperations()=default;
  virtual void BeforeTerminalPublication() {} // trusted fault seam, coordinator only
};
class ManagedOperation {
 public:
  enum class Cleanup { kPending, kUncertain, kConfirmedComplete };
  struct Publication {
    Cleanup cleanup=Cleanup::kPending;
    std::shared_ptr<const std::string> terminal;
    uint64_t version=0;
  };
  struct Snapshot {
    std::shared_ptr<const Publication> publication;
    // Derived only from observed coordinator exit AFTER TLS teardown and join.
    bool quiescent=false;
  };
  explicit ManagedOperation(ManagedPublicationOperations* operations=nullptr);
  virtual ~ManagedOperation(); // fail-stop if caller releases live coordination
  ManagedOperation(const ManagedOperation&)=delete;
  ManagedOperation& operator=(const ManagedOperation&)=delete;
  uint64_t ClientToken() const noexcept {return token_.load();}
  void Run(); // Starts tracked coordination; return/throw is NEVER cleanup proof.
  void RequestCancel() noexcept {cancelled_.store(true,std::memory_order_release);}
  // Memory-only publication + observed-exit join; never Session/Journal locks.
  Snapshot PollCleanup() noexcept;
 protected:
  virtual void Coordinate()=0;
  bool CancellationRequested() const noexcept {return cancelled_.load(std::memory_order_acquire);}
  // Single coordinator writer. All three publications are preallocated before
  // admission. Attaching an already materialized immutable terminal allocates
  // nothing. The fault seam may throw; proof survives and caller retries.
  // Confirm ONLY from matching WorkerResult pending/complete after Session fsync.
  // A lost unconfirmed session cannot later be inferred confirmed. Once confirmed,
  // later session loss cannot remove that per-job proof. Terminal is immutable.
  void Publish(Cleanup,std::shared_ptr<const std::string> terminal={});
 private:
  friend class Dispatcher;
  bool BindClientToken(uint64_t token) noexcept {
    uint64_t unset=0;return token && token_.compare_exchange_strong(unset,token);
  }
  std::atomic<uint64_t> token_{0};
  std::atomic<bool> cancelled_{false};
  ManagedPublicationOperations* operations_;
  std::shared_ptr<const Publication> confirmed_,uncertain_;
  std::shared_ptr<Publication> terminal_publication_;
  std::atomic<std::shared_ptr<const Publication>> publication_;
  std::mutex thread_mutex_;
  std::thread coordinator_;
  std::promise<void> exit_promise_;
  std::future<void> exited_=exit_promise_.get_future();
  bool attempted_=false,joined_=false,joining_=false;
};
}
