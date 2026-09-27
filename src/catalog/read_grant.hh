// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "catalog/read_lease.hh"
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
namespace capmgr {
struct CatalogGrantReceipt {
  std::string nonce, descriptor;
};
// Fixed CMG1:<64 lowercase hex nonce>:<165-byte CMR1 descriptor>.
CatalogGrantReceipt ParseCatalogGrant(std::string_view);
class CatalogGrantBudget {
 public:
  static constexpr unsigned kLimit = 64;
  unsigned Outstanding() const noexcept { return count_.load(); }

 private:
  friend class CatalogReadGrant;
  bool Reserve() noexcept;
  void Release() noexcept { count_.fetch_sub(1); }
  std::atomic<unsigned> count_{0};
};
// Explicit private test seam, never selected from an IPC request.
struct CatalogGrantOperations {
  using Time = std::chrono::steady_clock::time_point;
  virtual ~CatalogGrantOperations() = default;
  virtual Time Now() noexcept;
  virtual std::array<unsigned char, 32> Random();
};
// One object per already-authorized ServiceBase, NOT a global nonce lookup.
// Budget/operations must outlive it. Generated MAIN/callback policy checks must
// precede Issue/Confirm. Caller must schedule Expire at least once per service
// tick, including idle connections. Five seconds is a CONFIRMATION deadline,
// not hard wall-clock reclamation: stalled dispatch delays timer cleanup and
// retains budget/lease. Remove the timer and quiesce calls before destruction.
// No transport is enabled here.
class CatalogReadGrant {
 public:
  static constexpr auto kLifetime = std::chrono::seconds(5);
  explicit CatalogReadGrant(CatalogGrantBudget&,
                            CatalogGrantOperations* = nullptr);
  ~CatalogReadGrant();
  CatalogReadGrant(const CatalogReadGrant&) = delete;
  CatalogReadGrant& operator=(const CatalogReadGrant&) = delete;
  std::string Issue(std::unique_ptr<CatalogReadLease>);
  // SAME instance only. Exact nonce, deadline and lease/descriptor are rechecked
  // while serialized. Success consumes once and releases the issuer lease; the
  // protocol requires the client independent lease BEFORE this RPC. Any failure
  // retires this instance too. A lost reply never permits client publication.
  void Confirm(std::string_view nonce);
  void Expire();
  void Revoke();

 private:
  enum class State { kFresh, kPending, kRetired };
  void Clear() noexcept;
  CatalogGrantBudget& budget_;
  CatalogGrantOperations defaults_, *operations_;
  std::mutex mutex_;
  State state_ = State::kFresh;
  std::unique_ptr<CatalogReadLease> lease_;
  std::string nonce_, descriptor_;
  CatalogGrantOperations::Time expires_{};
  bool reserved_ = false;
};
}
