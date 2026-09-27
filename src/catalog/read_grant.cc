// SPDX-License-Identifier: Apache-2.0
#include "catalog/read_grant.hh"
#include <cerrno>
#include <sys/random.h>
namespace capmgr {
namespace {
bool Hex(std::string_view value) {
  return value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
}
CatalogGrantReceipt ParseCatalogGrant(std::string_view value) {
  if (value.size() != 235 || value.substr(0, 5) != "CMG1:" ||
      value[69] != ':' || !Hex(value.substr(5, 64)) ||
      value.substr(70, 5) != "CMR1:" || !Hex(value.substr(75)))
    throw Error(ErrorCode::kPermission, "Malformed catalog grant");
  return {std::string(value.substr(5, 64)), std::string(value.substr(70))};
}
bool CatalogGrantBudget::Reserve() noexcept {
  auto count = count_.load();
  while (count < kLimit) {
    if (count_.compare_exchange_weak(count, count + 1)) return true;
  }
  return false;
}
CatalogGrantOperations::Time CatalogGrantOperations::Now() noexcept {
  return std::chrono::steady_clock::now();
}
std::array<unsigned char, 32> CatalogGrantOperations::Random() {
  std::array<unsigned char, 32> bytes{};
  size_t offset = 0;
  while (offset < bytes.size()) {
    auto count =
        getrandom(bytes.data() + offset, bytes.size() - offset, GRND_NONBLOCK);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0)
      throw Error(ErrorCode::kIo, "Catalog grant entropy unavailable");
    offset += static_cast<size_t>(count);
  }
  return bytes;
}
CatalogReadGrant::CatalogReadGrant(CatalogGrantBudget& budget,
                                   CatalogGrantOperations* operations)
    : budget_(budget), operations_(operations ? operations : &defaults_) {}
CatalogReadGrant::~CatalogReadGrant() {
  Clear();
}  // caller must quiesce expiry/dispatch
void CatalogReadGrant::Clear() noexcept {
  state_ = State::kRetired;
  lease_.reset();
  nonce_.clear();
  descriptor_.clear();
  if (reserved_) {
    reserved_ = false;
    budget_.Release();
  }
}
std::string CatalogReadGrant::Issue(std::unique_ptr<CatalogReadLease> lease) {
  std::lock_guard lock(mutex_);
  try {
    if (state_ != State::kFresh || !lease)
      throw Error(ErrorCode::kPermission, "Catalog grant already issued");
    if (!budget_.Reserve())
      throw Error(ErrorCode::kLimit, "Catalog grant limit");
    reserved_ = true;
    lease_ = std::move(lease);
    descriptor_ = lease_->Descriptor();
    auto bytes = operations_->Random();
    nonce_.reserve(64);
    constexpr char digits[] = "0123456789abcdef";
    for (auto byte : bytes) {
      nonce_ += digits[byte >> 4];
      nonce_ += digits[byte & 15];
    }
    auto response = "CMG1:" + nonce_ + ":" + descriptor_;
    expires_ = operations_->Now() + kLifetime;
    state_ = State::kPending;
    return response;
  } catch (...) {
    Clear();
    throw;
  }
}
void CatalogReadGrant::Confirm(std::string_view nonce) {
  std::lock_guard lock(mutex_);
  try {
    if (state_ != State::kPending || operations_->Now() >= expires_ ||
        nonce != nonce_)
      throw Error(ErrorCode::kPermission, "Catalog confirmation denied");
    lease_->MatchDescriptor(descriptor_);
    if (operations_->Now() >= expires_)
      throw Error(ErrorCode::kPermission, "Catalog confirmation expired");
    Clear();
  } catch (...) {
    Clear();
    throw;
  }
}
void CatalogReadGrant::Expire() {
  std::lock_guard lock(mutex_);
  if (state_ == State::kPending && operations_->Now() >= expires_) Clear();
}
void CatalogReadGrant::Revoke() {
  std::lock_guard lock(mutex_);
  Clear();
}
}
