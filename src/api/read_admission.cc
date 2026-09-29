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

#include "api/read_admission.hh"

namespace capmgr {

namespace {

class HandoffAccess final : public ReadAccess {
 public:
  HandoffAccess(std::unique_ptr<CatalogReadLease> lease, std::string descriptor,
                CatalogAdmissionChannel& channel)
      : lease_(std::move(lease)),
        descriptor_(std::move(descriptor)),
        channel_(&channel) {}
  const std::string& Path() const noexcept override { return lease_->Path(); }
  void Check() override {
    lease_->Check();
    if (channel_) channel_->CheckSameLive();
  }

  void Opened(Database& db) override {
    lease_->Opened(db);
    lease_->MatchDescriptor(descriptor_);
    channel_
        ->CheckSameLive();  // Early loss check, not split-socket handoff proof.
    channel_->ConfirmCatalog(
        descriptor_);  // Same-instance RPC after local open.
    channel_
        ->Finish();  // May block/throw only DURING create; no handle published.
    channel_ = nullptr;
    lease_->Check();
  }

 private:
  // Lease remains owned until after SQLite closes. Channel is borrowed only
  // during create; failure destruction never calls transport or native callbacks.
  std::unique_ptr<CatalogReadLease> lease_;
  std::string descriptor_;
  CatalogAdmissionChannel* channel_;
};

}  // namespace

LeasedCatalogGate::LeasedCatalogGate(ReadLeasePolicy policy,
                                     CatalogAdmissionChannel& channel,
                                     ReadLeaseOperations* operations)
    : policy_(std::move(policy)), channel_(channel), operations_(operations) {}
std::string LeasedCatalogGate::AuthorizeAndGetDatabase() {
  throw Error(ErrorCode::kPermission, "Owned catalog admission required");
}

std::unique_ptr<ReadAccess> LeasedCatalogGate::AuthorizeReadAccess() {
  channel_.CheckSameLive();
  auto descriptor = channel_.AuthorizeCatalog();
  channel_.CheckSameLive();
  if (descriptor.size() != 165)
    throw Error(ErrorCode::kPermission, "Invalid catalog descriptor length");
  auto lease = std::make_unique<CatalogReadLease>(policy_, operations_);
  lease->MatchDescriptor(descriptor);
  channel_.CheckSameLive();
  return std::make_unique<HandoffAccess>(std::move(lease),
                                         std::move(descriptor), channel_);
}
}  // namespace capmgr
