// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <memory>
#include <string>
#include "api/capmgr.h"
namespace capmgr {
// Private injection seam. Never installed or exported in libcapmgr.
class AccessGate {
 public:
  virtual ~AccessGate() = default;
  virtual std::string AuthorizeAndGetDatabase() = 0;
};
int CreateClient(AccessGate& gate, capmgr_client_h* client) noexcept;
}
