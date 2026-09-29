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

#ifndef CAPABILITY_MANAGER_PLATFORM_TIDL_READ_SERVICE_HH_
#define CAPABILITY_MANAGER_PLATFORM_TIDL_READ_SERVICE_HH_

#include "capability_manager_stub.h"
#include "catalog/read_grant.hh"

#include <glib.h>

#include <thread>

namespace capmgr {

struct CatalogReaderPrincipal {
  uid_t uid;
  gid_t gid;
  std::string socket_label;
};

// Development adapter only. Construct/dispatch/expire/destroy in ONE service
// GMainContext owner thread. The generated stub validates bound MAIN/callback and
// real Cynara before each parcel; the fixed principal rule is additional policy.
// No production factory selects this class. Provisioning and direct-read policy
// subset remain independent gates. Shared budget must outlive every service.
class TidlReadService : public rpc_port::capability_manager_stub::stub::
                            CapabilityManager::ServiceBase {
 public:
  using Stub = rpc_port::capability_manager_stub::stub::CapabilityManager;
  TidlReadService(std::string sender, std::string instance, ReadLeasePolicy,
                  CatalogReaderPrincipal, std::shared_ptr<CatalogGrantBudget>);
  ~TidlReadService() override;
  void OnCreate() noexcept override;
  void OnTerminate() noexcept override;
  std::string AuthorizeCatalog() override;
  int ConfirmCatalog(std::string nonce) override;
  int Execute(std::string, std::string) override { return -6; }
  int Cancel(std::string) override { return -6; }
  int RemountResources(std::string) override { return -6; }
  int RegisterReply(std::unique_ptr<Stub::Reply>) override { return -6; }
  bool UnregisterReply(int) override { return false; }
  int RegisterChanged(std::unique_ptr<Stub::Changed>) override { return -6; }
  bool UnregisterChanged(int) override { return false; }

 private:
  void Owner() const noexcept;
  void Principal();
  ReadLeasePolicy policy_;
  CatalogReaderPrincipal principal_;
  std::shared_ptr<CatalogGrantBudget> budget_;
  CatalogReadGrant grant_;
  const std::thread::id owner_ = std::this_thread::get_id();
  GSource* expiry_ = nullptr;
  bool terminated_ = false;
};

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_TIDL_READ_SERVICE_HH_
