// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
#include <sys/types.h>
namespace capmgr {
// Private syscall seam for deterministic persistence failure tests. Implementations
// return the usual syscall result/errno and must not reenter BrokerJournal.
class JournalOperations {
 public:
  virtual ~JournalOperations()=default;
  virtual ssize_t Write(int fd,const void* data,size_t size) noexcept=0;
  virtual int Sync(int fd) noexcept=0;
  virtual int Replace(int directory) noexcept=0;
};
JournalOperations& LinuxJournalOperations();
// Private durable reservation store, not an IPC or reset interface. Production
// supplies a prevalidated root-owned directory FD and owner=0. A different owner
// is only a local test seam. The directory and clean seed must be provisioned by
// the image; missing/corrupt/unclean state never bootstraps a new generation.
// Parent path/mount provenance and external old-job absence proof are outside
// this store. Never pass an application-selected directory or owner.
class BrokerJournal {
 public:
  explicit BrokerJournal(int trusted_directory, uid_t owner=0,
                         JournalOperations& operations=LinuxJournalOperations());
  ~BrokerJournal();
  BrokerJournal(const BrokerJournal&)=delete;
  BrokerJournal& operator=(const BrokerJournal&)=delete;
  // All calls are serialized internally. Snapshots own their bytes. As with any
  // C++ object, destruction requires the caller to end all outstanding calls.
  bool Blocked() const;
  uint64_t Generation() const;
  std::vector<uint64_t> Reservations() const;
  uint64_t BeginGeneration(); // fsync before worker spawn
  uint64_t Reserve(); // fsync before job admission/clone, four global slots
  void ConfirmJobGone(uint64_t token); // trusted normal-cleanup path only
  void MarkUncertain(); // worker/status loss: no in-process reset path
  void ConfirmNormalWorkerExit(); // empty reservations + observed normal exit
 private:
  void Persist(const std::string& state, uint64_t generation, uint64_t next,
               const std::vector<uint64_t>& jobs);
  void RequireActive() const;
  JournalOperations& operations_;
  mutable std::mutex mutex_;
  int directory_=-1, lock_=-1;
  std::string state_;
  uint64_t generation_=0, next_=1;
  std::vector<uint64_t> jobs_;
  bool blocked_=true;
};
}
