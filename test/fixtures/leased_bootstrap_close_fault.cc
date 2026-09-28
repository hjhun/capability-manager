// SPDX-License-Identifier: Apache-2.0
// Linked ONLY into the separate fixed root fault image, never a product target.
#include <sqlite3.h>

#include <fcntl.h>

namespace {
bool armed = false;
bool fired = false;
}  // namespace
void ArmLeasedBootstrapCloseFault() { armed = true; }
bool LeasedBootstrapCloseFaultFired() { return fired; }
extern "C" int __real_sqlite3_close(sqlite3*);
extern "C" int __wrap_sqlite3_close(sqlite3* db) {
  const int result = __real_sqlite3_close(db);
  // This image opens exactly one loader connection after explicit arming. Never
  // force a SQLite close result or open an ordinary DB/WAL/SHM data descriptor.
  if (db && result == SQLITE_OK && armed && !fired) {
    armed = false;
    const int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    fired = fd >= 9;
    // Deliberately retained until kernel exit: the final typed table MUST reject
    // this extra. No successful startup can contain it and no allowlist changes.
  }
  return result;
}
