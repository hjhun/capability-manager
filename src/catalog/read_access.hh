// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "catalog/database.hh"
namespace capmgr {
// Private admission lifetime. Must outlive SQLite, including failed destroy
// retries. Destruction only closes owned descriptors; no IPC/join/fsync.
class ReadAccess {
 public:
  virtual ~ReadAccess() = default;
  virtual const std::string& Path() const noexcept = 0;
  virtual void Check() = 0;
  virtual void Opened(Database&) = 0;
};
}
