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

#ifndef CAPABILITY_MANAGER_API_READ_ADMISSION_HH_
#define CAPABILITY_MANAGER_API_READ_ADMISSION_HH_

#include "api/client.hh"
#include "catalog/read_lease.hh"

namespace capmgr {

// Trusted private transport seam; no production implementation is selected here.
// Authorize retains an independent issuer lease and a bounded, expiring, one-use
// grant on the SAME service instance. The concrete channel owns its nonce.
// CheckSameLive detects known loss/reuse but is NOT handoff proof: rpc-port1.21.17
// has split write/read sockets and exposes only read FDs. ConfirmCatalog must
// round-trip on the SAME proxy after local validation, validate its retained
// grant/descriptor and both bound channels, and consume once while holding the
// issuer lease. Reconnect, rejection, expiry or lost reply must throw. Only a
// valid confirmation proves overlapping leases; Finish then completes native
// callback/proxy teardown during create, never during bounded destroy.
class CatalogAdmissionChannel {
 public:
  virtual ~CatalogAdmissionChannel() = default;
  virtual std::string AuthorizeCatalog() = 0;
  virtual void CheckSameLive() = 0;
  virtual void ConfirmCatalog(std::string_view descriptor) = 0;
  virtual void Finish() = 0;
};

// Caller owns the channel across the whole CreateClient call (and failure cleanup).
// Gate and channel are serialized, no concurrent reuse. Fixed policy and ancestor/
// mount provisioning are caller-owned; no request-selected path is introduced.
class LeasedCatalogGate final : public AccessGate {
 public:
  LeasedCatalogGate(ReadLeasePolicy, CatalogAdmissionChannel&,
                    ReadLeaseOperations* = nullptr);
  std::string AuthorizeAndGetDatabase()
      override;  // forbidden path-only fallback
  std::unique_ptr<ReadAccess> AuthorizeReadAccess() override;

 private:
  ReadLeasePolicy policy_;
  CatalogAdmissionChannel& channel_;
  ReadLeaseOperations* operations_;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_API_READ_ADMISSION_HH_
