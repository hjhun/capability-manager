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

#ifndef CAPABILITY_MANAGER_PLATFORM_TIDL_READ_CHANNEL_HH_
#define CAPABILITY_MANAGER_PLATFORM_TIDL_READ_CHANNEL_HH_

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

}  // namespace capmgr

#endif  // CAPABILITY_MANAGER_PLATFORM_TIDL_READ_CHANNEL_HH_
