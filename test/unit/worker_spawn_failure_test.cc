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

#include "launcher/worker_spawn.hh"
#include "common/error.hh"

#include <gtest/gtest.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>

using namespace capmgr;
TEST(WorkerSpawnFailure, ExecFormatErrorAbandonsUnspawnedSlotAndBurnsToken) {
  int pipes[4][2];
  for (auto& pipe : pipes) ASSERT_EQ(pipe2(pipe, O_CLOEXEC), 0);
  int parent = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC),
      directory = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  struct sigaction previous{}, normal{};
  normal.sa_handler = SIG_DFL;
  sigaction(SIGCHLD, &normal, &previous);
  OwnedChildren children(1);
  WorkerInheritedFds fds{pipes[0][0], pipes[1][0], pipes[2][1],
                         parent,      directory,   pipes[3][1]};
  // The dedicated test build's fixed image is an executable-mode text file with
  // no valid executable format. posix_spawn, not preflight, returns ENOEXEC.
  for (int i = 0; i < 3; ++i) {
    errno = 0;
    EXPECT_THROW(SpawnFixedWorker(children, 1, fds), Error);
    EXPECT_EQ(errno, ENOEXEC);
    EXPECT_EQ(children.Size(), 0u);
  }

  auto next = children.Reserve();
  EXPECT_EQ(next, 4u);
  children.AbandonUnspawned(next);
  sigaction(SIGCHLD, &previous, nullptr);
  for (auto& pipe : pipes)
    for (int fd : pipe) close(fd);
  close(parent);
  close(directory);
}
