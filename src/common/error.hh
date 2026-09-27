// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdexcept>
#include <string>
namespace capmgr {
enum class ErrorCode : int {
  kInvalid = -1,
  kPermission = -2,
  kDatabase = -3,
  kNotFound = -4,
  kConflict = -5,
  kUnsupported = -6,
  kBusy = -7,
  kLimit = -8,
  kIo = -9,
  kNoMemory = -10
};
class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}
  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};
}
