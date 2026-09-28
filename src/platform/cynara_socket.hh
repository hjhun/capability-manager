// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <mutex>
namespace capmgr::internal {
// Cynara's socket credential helpers require external serialization. Share this
// lock between credential extraction and policy identity extraction.
inline std::mutex& CynaraSocketMutex() {
  static std::mutex mutex;
  return mutex;
}
}
