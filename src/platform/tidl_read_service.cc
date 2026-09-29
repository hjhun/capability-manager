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
// SPDX-License-Identifier: Apache-2.0

#include "platform/tidl_read_service.hh"

namespace capmgr {

namespace {

CatalogGrantBudget& RequireBudget(
    const std::shared_ptr<CatalogGrantBudget>& budget) {
  if (!budget) throw Error(ErrorCode::kInvalid, "Missing catalog grant budget");
  return *budget;
}
}  // namespace

TidlReadService::TidlReadService(std::string sender, std::string instance,
                                 ReadLeasePolicy policy,
                                 CatalogReaderPrincipal principal,
                                 std::shared_ptr<CatalogGrantBudget> budget)
    : ServiceBase(std::move(sender), std::move(instance)),
      policy_(std::move(policy)),
      principal_(std::move(principal)),
      budget_(std::move(budget)),
      grant_(RequireBudget(budget_)) {}
TidlReadService::~TidlReadService() { OnTerminate(); }
void TidlReadService::Owner() const noexcept {
  if (std::this_thread::get_id() != owner_) std::terminate();
}

void TidlReadService::OnCreate() noexcept {
  Owner();
  if (terminated_ || expiry_) return;
  expiry_ = g_timeout_source_new(100);
  if (!expiry_) {
    terminated_ = true;
    return;
  }

  g_source_set_callback(
      expiry_,
      [](gpointer data) -> gboolean {
        auto* service = static_cast<TidlReadService*>(data);
        service->Owner();
        service->grant_.Expire();
        return G_SOURCE_CONTINUE;
      },
      this, nullptr);
  // Stub dispatch and this source share the creator's context. No worker thread
  // iterates it; blocked service work delays expiry and retains the budget slot.
  if (!g_source_attach(expiry_, g_main_context_get_thread_default()))
    OnTerminate();
}

void TidlReadService::OnTerminate() noexcept {
  Owner();
  terminated_ = true;
  if (expiry_) {
    g_source_destroy(expiry_);
    g_source_unref(expiry_);
    expiry_ = nullptr;
  }
  grant_
      .Revoke();  // no I/O beyond closing lease FDs, no concurrent grant operation
}

void TidlReadService::Principal() {
  Owner();
  auto peer = MainPrincipal();
  if (terminated_ || !expiry_ || !peer || !peer->Connected() ||
      peer->uid() != principal_.uid || peer->gid() != principal_.gid ||
      peer->security_label() != principal_.socket_label)
    throw Error(ErrorCode::kPermission, "Catalog principal denied");
}

std::string TidlReadService::AuthorizeCatalog() {
  try {
    Principal();
    return grant_.Issue(std::make_unique<CatalogReadLease>(policy_));
  } catch (...) {
    grant_.Revoke();
    return {};
  }
}

int TidlReadService::ConfirmCatalog(std::string nonce) {
  try {
    Principal();
    grant_.Confirm(nonce);
    return 0;
  } catch (...) {
    grant_.Revoke();
    return static_cast<int>(ErrorCode::kPermission);
  }
}
}  // namespace capmgr
