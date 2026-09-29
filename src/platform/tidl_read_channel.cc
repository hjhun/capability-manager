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
 * SPDX-License-Identifier: Apache-2.0
 */

#include "platform/tidl_read_channel.hh"
#include "catalog/read_grant.hh"
#include "capability_manager_proxy.h"

#include <glib.h>

#include <exception>
#include <thread>

namespace capmgr {

namespace {

using Proxy = rpc_port::capability_manager_proxy::proxy::CapabilityManager;
class Context {
 public:
  Context()
      : owner_(std::this_thread::get_id()), context_(g_main_context_new()) {
    if (!context_) throw std::bad_alloc();
    g_main_context_push_thread_default(context_);
  }

  ~Context() { Close(); }
  void Check() const {
    if (std::this_thread::get_id() != owner_)
      throw Error(ErrorCode::kPermission, "Read channel thread mismatch");
  }

  void Close() noexcept {
    if (!context_) return;
    if (std::this_thread::get_id() != owner_ ||
        g_main_context_get_thread_default() != context_)
      std::terminate();
    g_main_context_pop_thread_default(context_);
    g_main_context_unref(context_);
    context_ = nullptr;
  }

 private:
  const std::thread::id owner_;
  GMainContext* context_;
};

class Listener final : public Proxy::IEventListener {
 public:
  explicit Listener(Context& context) : context_(context) {}
  void OnConnected() override {
    context_.Check();
    connected = true;
  }

  void OnDisconnected() override {
    context_.Check();
    connected = false;
    lost = true;
  }

  void OnRejected() override {
    context_.Check();
    connected = false;
    lost = true;
  }
  bool connected = false, lost = false;

 private:
  Context& context_;
};

}  // namespace

struct TidlReadChannel::Impl {
  enum class State { kConnected, kIssued, kConfirmed, kClosed };
  Context context;  // constructed/pushed BEFORE proxy creation and sync connect
  Listener listener{context};  // outlives proxy on success and every exception
  std::unique_ptr<Proxy> proxy;
  CatalogGrantReceipt receipt;
  State state = State::kConnected;
  explicit Impl(const std::string& endpoint) {
    proxy = std::make_unique<Proxy>(&listener, endpoint);
    proxy->Connect(true);
    Check();
  }

  ~Impl() {
    try {
      Close();
    } catch (...) {
    }
  }

  void Check() {
    context.Check();
    if (state == State::kClosed || !proxy || !listener.connected ||
        listener.lost)
      throw Error(ErrorCode::kPermission, "Read channel is not reusable");
  }

  void Close() {
    context.Check();
    state = State::kClosed;
    std::exception_ptr failure;
    if (proxy && listener.connected) {
      try {
        proxy->Disconnect();
      } catch (...) {
        failure = std::current_exception();
      }
    }
    proxy.reset();
    listener.connected = false;
    receipt = {};
    context.Close();
    if (failure) std::rethrow_exception(failure);
  }
};

TidlReadChannel::TidlReadChannel(const std::string& endpoint)
    : impl_(std::make_unique<Impl>(endpoint)) {}
TidlReadChannel::~TidlReadChannel() = default;
void TidlReadChannel::CheckSameLive() { impl_->Check(); }
std::string TidlReadChannel::AuthorizeCatalog() {
  try {
    impl_->Check();
    if (impl_->state != Impl::State::kConnected)
      throw Error(ErrorCode::kPermission, "Grant already requested");
    impl_->receipt = ParseCatalogGrant(impl_->proxy->AuthorizeCatalog());
    impl_->state = Impl::State::kIssued;
    return impl_->receipt.descriptor;
  } catch (...) {
    try {
      impl_->Close();
    } catch (...) {
    }
    throw;
  }
}

void TidlReadChannel::ConfirmCatalog(std::string_view descriptor) {
  try {
    impl_->Check();
    if (impl_->state != Impl::State::kIssued ||
        descriptor != impl_->receipt.descriptor ||
        impl_->proxy->ConfirmCatalog(impl_->receipt.nonce) != 0)
      throw Error(ErrorCode::kPermission, "Catalog confirmation failed");
    impl_->state = Impl::State::kConfirmed;
  } catch (...) {
    try {
      impl_->Close();
    } catch (...) {
    }
    throw;
  }
}

void TidlReadChannel::Finish() {
  try {
    impl_->Check();
    if (impl_->state != Impl::State::kConfirmed)
      throw Error(ErrorCode::kPermission, "Unconfirmed handoff");
    impl_->Close();
  } catch (...) {
    try {
      impl_->Close();
    } catch (...) {
    }
    throw;
  }
}
}  // namespace capmgr
