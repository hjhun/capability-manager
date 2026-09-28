// SPDX-License-Identifier: Apache-2.0
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
 */

#ifndef CAPABILITY_MANAGER_LAUNCHER_WORKER_SPAWN_HH_
#define CAPABILITY_MANAGER_LAUNCHER_WORKER_SPAWN_HH_

#include "launcher/owned_children.hh"
namespace capmgr {
// Private fixed-image exec primitive. No executable, argv, environment, UID or
// namespace is selected through a request. Production image path is compiled in;
// this checkpoint does not install/activate that image. Catalog provenance,
// worker bootstrap checks, root unit/policy and authenticated admission are gates.
// The image-owned parent path/ancestors and these FDs must already be trusted.
struct WorkerInheritedFds {
  int command_read, cancel_read, reply_write;
  int parent_process;  // frontend's preopened proc directory; worker validates live
  int catalog_directory;  // trusted catalog parent, NOT a DB FD (WAL needs names)
  int ready_write;  // dedicated bootstrap pipe, closed by worker before admission
};
// Sources are duplicated above fixed targets BEFORE file actions. Child receives
// only null stdio and 3=command,4=cancel,5=reply,6=parent proc,7=catalog directory,
// 8=bootstrap READY write;
// addclosefrom_np(9) excludes all other FDs including non-CLOEXEC TIDL handles.
// SIGCHLD default/no competing reaper is required; child ownership outlives call.
// Uses posix_spawn, not post-fork C++ in a multithreaded listener. The call itself
// is not a hard deadline operation. After successful spawn the exact direct child
// is attached immediately without allocation/Observe; returned value is its owned
// token, never a PID from IPC. This is ownership only: exec/worker READY and
// bootstrap failure/exit127 must be observed independently before START. Failure creates no adopted child; caller retains
// frontend journal uncertainty and must not automatically restart a generation.
uint64_t SpawnFixedWorker(OwnedChildren&, uint64_t generation,
                          const WorkerInheritedFds&);
}

#endif  // CAPABILITY_MANAGER_LAUNCHER_WORKER_SPAWN_HH_
