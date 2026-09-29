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

#include "launcher/worker_command.hh"
#include "common/error.hh"

#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

#include <array>

using namespace capmgr;

namespace {

struct Pipe {
  int descriptors[2]{-1, -1};
  Pipe() {
    if (pipe2(descriptors, O_CLOEXEC | O_NONBLOCK))
      throw std::runtime_error("pipe");
  }

  ~Pipe() {
    for (auto fd : descriptors)
      if (fd >= 0) close(fd);
  }

  void Send(const std::vector<uint8_t>& bytes) {
    ASSERT_EQ(write(descriptors[1], bytes.data(), bytes.size()),
              static_cast<ssize_t>(bytes.size()));
  }
};

WorkerCommand Start(uint64_t sequence = 1) {
  return {
      WorkerCommandKind::Start, 7, sequence, 9,
      R"({"jsonrpc":"2.0","id":"exact","method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})"};
}
}  // namespace

TEST(WorkerCommand, FragmentedStartPreservesExactRequest) {
  Pipe pipe;
  WorkerCommandReader reader(pipe.descriptors[0], 7);
  auto command = Start();
  auto encoded = EncodeWorkerCommand(command);
  std::optional<WorkerCommand> received;
  for (auto byte : encoded) {
    ASSERT_EQ(write(pipe.descriptors[1], &byte, 1), 1);
    auto next = reader.ReadOne();
    if (next) {
      ASSERT_FALSE(received);
      received = std::move(next);
    }
  }

  ASSERT_TRUE(received);
  EXPECT_EQ(received->request, command.request);
  EXPECT_EQ(received->token, 9u);
  EXPECT_EQ(received->sequence, 1u);
  EXPECT_EQ(received->generation, 7u);
  EXPECT_FALSE(reader.ReadOne());
}

TEST(WorkerCommand, PartialStartCannotBlockIndependentPriorityCancel) {
  Pipe regular, priority;
  WorkerCommandReader input(regular.descriptors[0], 7),
      cancel(priority.descriptors[0], 7, true);
  auto bytes = EncodeWorkerCommand(Start());
  bytes.resize(41);
  regular.Send(bytes);
  EXPECT_FALSE(input.ReadOne());
  EXPECT_FALSE(input.ReadOne());
  priority.Send(EncodeWorkerCommand({WorkerCommandKind::Cancel, 7, 1, 9, {}}));
  auto stopped = cancel.ReadOne();
  ASSERT_TRUE(stopped);
  EXPECT_EQ(stopped->kind, WorkerCommandKind::Cancel);
  EXPECT_EQ(stopped->token, 9u);
  EXPECT_FALSE(input.ReadOne());
}

TEST(WorkerCommand, BufferedCommandAfterParentEndpointCloseIsRejected) {
  Pipe pipe;
  WorkerCommandReader reader(pipe.descriptors[0], 7);
  pipe.Send(EncodeWorkerCommand({WorkerCommandKind::Status, 7, 1, 9, {}}));
  close(pipe.descriptors[1]);
  pipe.descriptors[1] = -1;
  EXPECT_THROW(reader.ReadOne(), Error);
  EXPECT_THROW(reader.ReadOne(), Error);
}

TEST(WorkerCommand, PartialFrameDeadlinePoisonsChannel) {
  Pipe pipe;
  WorkerCommandReader reader(pipe.descriptors[0], 7);
  uint8_t byte = 'C';
  auto now = WorkerCommandReader::Clock::now();
  ASSERT_EQ(write(pipe.descriptors[1], &byte, 1), 1);
  EXPECT_FALSE(reader.ReadOne(now));
  EXPECT_FALSE(reader.ReadOne(now + std::chrono::seconds(4)));
  EXPECT_THROW(reader.ReadOne(now + std::chrono::seconds(5)), Error);
}

TEST(WorkerCommand,
     HeaderCorruptionGenerationReplayAndChannelMisuseAreRejected) {
  for (auto offset : {0u, 4u, 6u, 8u, 16u, 24u, 36u}) {
    Pipe pipe;
    WorkerCommandReader reader(pipe.descriptors[0], 7);
    auto bytes = EncodeWorkerCommand({WorkerCommandKind::Status, 7, 1, 9, {}});
    bytes[offset] = offset == 24 ? 0 : 0xff;
    pipe.Send(bytes);
    EXPECT_THROW(reader.ReadOne(), Error);
  }
  Pipe pipe;
  WorkerCommandReader reader(pipe.descriptors[0], 7);
  auto bytes = EncodeWorkerCommand({WorkerCommandKind::Status, 7, 1, 9, {}});
  pipe.Send(bytes);
  ASSERT_TRUE(reader.ReadOne());
  pipe.Send(bytes);
  EXPECT_THROW(reader.ReadOne(), Error);
  Pipe wrong;
  WorkerCommandReader priority(wrong.descriptors[0], 7, true);
  wrong.Send(bytes);
  EXPECT_THROW(priority.ReadOne(), Error);
}

TEST(WorkerCommand, DeclaredOversizeIsRejectedBeforeReadingBody) {
  Pipe pipe;
  WorkerCommandReader reader(pipe.descriptors[0], 7);
  auto bytes = EncodeWorkerCommand(Start());
  bytes.resize(40);
  bytes[32] = 1;
  bytes[33] = 0;
  bytes[34] = 1;
  pipe.Send(bytes);
  EXPECT_THROW(reader.ReadOne(), Error);
}

TEST(WorkerCommand, SequenceExhaustionNeverWraps) {
  Pipe pipe;
  WorkerCommandReader reader(pipe.descriptors[0], 7, false, UINT64_MAX);
  pipe.Send(
      EncodeWorkerCommand({WorkerCommandKind::Status, 7, UINT64_MAX, 9, {}}));
  ASSERT_TRUE(reader.ReadOne());
  pipe.Send(EncodeWorkerCommand({WorkerCommandKind::Status, 7, 1, 9, {}}));
  EXPECT_THROW(reader.ReadOne(), Error);
}

TEST(WorkerCommand, InvalidStartPayloadPoisonsReadChannel) {
  for (
      const std::string body :
      {"not-json",
       R"({"jsonrpc":"2.0","id":1,"id":2,"method":"tools/call","params":{"name":"cli:x","arguments":{}}})",
       R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"action:x","arguments":{}}})"}) {
    Pipe pipe;
    WorkerCommandReader reader(pipe.descriptors[0], 7);
    auto bytes = EncodeWorkerCommand({WorkerCommandKind::Status, 7, 1, 9, {}});
    bytes[6] = 1;
    bytes[32] = static_cast<uint8_t>(body.size());
    bytes.insert(bytes.end(), body.begin(), body.end());
    pipe.Send(bytes);
    EXPECT_FALSE(reader.ReadOne());
    EXPECT_THROW(reader.ReadOne(), Error);
    EXPECT_THROW(reader.ReadOne(), Error);
  }
}

TEST(WorkerCommand, EncoderRejectsNestedDuplicateAndAuthorityPayloads) {
  auto command = Start();
  command.request =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:x","arguments":{"key":1,"key":2}}})";
  EXPECT_THROW(EncodeWorkerCommand(command), Error);
  command.request =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:x","arguments":{"nested":)" +
      std::string(80, '[') + "0" + std::string(80, ']') + "}}}";
  EXPECT_THROW(EncodeWorkerCommand(command), Error);
  EXPECT_THROW(EncodeWorkerCommand((WorkerCommand{WorkerCommandKind::Cancel, 7,
                                                  1, 9, "/arbitrary/path"})),
               Error);
  command = Start();
  command.token = 0;
  EXPECT_THROW(EncodeWorkerCommand(command), Error);
}

TEST(WorkerCommand, PipeTypeAndDirectionAreRequired) {
  Pipe pipe;
  EXPECT_THROW(WorkerCommandReader(pipe.descriptors[1], 7), Error);
  int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  EXPECT_THROW(WorkerCommandReader(fd, 7), Error);
  close(fd);
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  EXPECT_THROW(WorkerCommandReader(pair[0], 7), Error);
  close(pair[0]);
  close(pair[1]);
}

TEST(WorkerCommand, AllocationFailureAfterHeaderPermanentlyPoisonsReader) {
  for (auto resize : {+[](std::string&, size_t) { throw std::bad_alloc(); },
                      +[](std::string&, size_t) {
                        throw std::length_error("injected allocation length");
                      }}) {
    Pipe pipe;
    WorkerCommandReader reader(pipe.descriptors[0], 7, false, 1, resize);
    pipe.Send(EncodeWorkerCommand(Start()));
    EXPECT_THROW(reader.ReadOne(), std::exception);
    // Body bytes are still buffered, but retry must never resume this frame.
    EXPECT_THROW(reader.ReadOne(), Error);
  }
}
