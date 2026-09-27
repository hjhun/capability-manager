// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "api/read_admission.hh"
namespace capmgr {
// Private create-only transport. endpoint is trusted image/fixture configuration,
// never a C-client argument. Own/use/destroy on one creating thread. No context
// iteration, reconnect, callbacks to application code, or production selection.
class TidlReadChannel final : public CatalogAdmissionChannel {
 public:
  explicit TidlReadChannel(const std::string& endpoint);
  ~TidlReadChannel() override;
  std::string AuthorizeCatalog() override;
  void CheckSameLive() override;  // known loss only, never split-socket proof
  void ConfirmCatalog(std::string_view descriptor) override;
  void Finish() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
