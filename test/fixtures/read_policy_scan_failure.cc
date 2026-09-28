// SPDX-License-Identifier: Apache-2.0
// Test-only GNU ld readdir interposition; fixed images, never installed.
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include <cstdlib>
#include <set>

extern "C" dirent* __real_readdir(DIR* directory);
extern "C" dirent* __wrap_readdir(DIR* directory) {
  static size_t tasks = 0;
  static std::set<int> seen;
  struct stat current{}, target{};
#ifdef CAPMGR_FAIL_TASK_SCAN
  constexpr auto path = "/proc/self/task";
#else
  constexpr auto path = "/proc/self/fd";
#endif
  bool selected = fstat(dirfd(directory), &current) == 0 &&
                  stat(path, &target) == 0 && current.st_dev == target.st_dev &&
                  current.st_ino == target.st_ino;
  if (selected) {
#ifdef CAPMGR_FAIL_TASK_SCAN
    bool fail = tasks == 1;
#else
    bool fail = seen.size() == 6;
#endif
    if (fail) {
      errno = EIO;
      return nullptr;
    }
  }
  auto* item = __real_readdir(directory);
  if (item && selected && item->d_name[0] != '.') {
    ++tasks;
    char* end = nullptr;
    long number = strtol(item->d_name, &end, 10);
    if (end && *end == '\0' && number >= 0 && number <= 5)
      seen.insert(static_cast<int>(number));
  }
  return item;
}
