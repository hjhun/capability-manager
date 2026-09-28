// SPDX-License-Identifier: Apache-2.0
// Fixed test subprocess: fresh SQLite state, no inherited connection or service.
#include "catalog/database.hh"
#include "catalog/read_lease.hh"

#include <filesystem>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
struct Labels : capmgr::ReadLeaseOperations {
  std::string Label(int) override { return "Fixture"; }
};
}
int main(int argc, char** argv) {
  using namespace capmgr;
  if (argc != 5) return 2;
  const std::string path = argv[1], lock = argv[2], mode = argv[3];
  const bool want_busy = std::string(argv[4]) == "BUSY";
  if (mode == "generation") {
    // Fresh, independent description: never inherits/duplicates the reader SH.
    const int fd = open(lock.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 5;
    struct flock requested{};
    requested.l_type = F_WRLCK;
    requested.l_whence = SEEK_SET;
    const int rc = fcntl(fd, F_OFD_SETLK, &requested);
    const int error = errno;
    if (close(fd)) return 5;
    const bool busy = rc < 0 && (error == EAGAIN || error == EACCES);
    std::cout << "GENERATION_EX="
              << (busy      ? "BUSY"
                  : rc == 0 ? "OK"
                            : "ERROR")
              << " EXPECTED=" << argv[4] << std::endl;
    return (want_busy ? busy : rc == 0) ? 0 : 4;
  }
  try {
    Labels labels;
    ReadLeasePolicy policy{std::filesystem::path(path).parent_path().string(),
                           lock,
                           getuid(),
                           getuid(),
                           getgid(),
                           getgid(),
                           0700,
                           0600,
                           0600,
                           "Fixture",
                           "Fixture",
                           "Fixture"};
    CatalogReadLease lease(policy, &labels);
    Database db(path,
                Database::Access::kWriter);  // Explicit isolated test owner.
    sqlite3_busy_timeout(db.handle(), 0);
    int rc;
    if (mode == "checkpoint")
      rc = sqlite3_wal_checkpoint_v2(
          db.handle(), "main", SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    else if (mode == "begin")
      rc = sqlite3_exec(db.handle(), "BEGIN IMMEDIATE", nullptr, nullptr,
                        nullptr);
    else if (mode == "exclusive")
      rc = sqlite3_exec(db.handle(),
                        "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE",
                        nullptr, nullptr, nullptr);
    else
      return 2;
    std::cout << "SQLITE_VERSION=" << sqlite3_libversion() << " MODE=" << mode
              << " RETURN=" << rc << " EXPECTED=" << argv[4] << std::endl;
    return ((rc & 255) == SQLITE_BUSY && want_busy) ||
                   (rc == SQLITE_OK && !want_busy)
               ? 0
               : 4;
  } catch (const Error& error) {
    std::cerr << "PROBE_ERROR=" << static_cast<int>(error.code()) << " "
              << error.what() << std::endl;
    return 5;
  }
}
