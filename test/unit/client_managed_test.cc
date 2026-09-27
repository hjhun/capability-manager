// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "api/client.hh"
#include "launcher/worker_result.hh"
#include <fcntl.h>
#include <fstream>
#include <future>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
const std::string request =
    R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:test","arguments":{"x":7}}})";
const std::string native =
    " \n{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"isError\":true,\"n\":1.234567890123456789}}\n";
class Gate : public AccessGate {
 public:
  explicit Gate(std::string path) : path_(std::move(path)) {}
  std::string AuthorizeAndGetDatabase() override { return path_; }

 private:
  std::string path_;
};
struct State : JournalOperations {
  std::string root;
  std::atomic<int> admissions = 0, preparations = 0, starts = 0;
  std::atomic<bool> block = false, cancel_seen = false, exited = false;
  bool fail = false, reject = false, allocation_failure = false;
  std::promise<void> syncing, release;
  std::shared_future<void> allowed = release.get_future().share();
  ssize_t Write(int fd, const void* b, size_t n) noexcept override {
    return LinuxJournalOperations().Write(fd, b, n);
  }
  int Replace(int fd) noexcept override {
    return LinuxJournalOperations().Replace(fd);
  }
  int Sync(int fd) noexcept override {
    if (block.exchange(false)) {
      syncing.set_value();
      allowed.wait();
      if (fail) {
        errno = EIO;
        return -1;
      }
    }
    return LinuxJournalOperations().Sync(fd);
  }
};
struct Pipe {
  int fd[2];
  Pipe() {
    if (pipe2(fd, O_NONBLOCK | O_CLOEXEC)) throw std::runtime_error("pipe");
  }
  ~Pipe() {
    close(fd[0]);
    close(fd[1]);
  }
};
std::vector<uint8_t> Frame(uint64_t token, uint64_t seq, WorkerReplyKind kind,
                           std::string body = {}) {
  std::vector<uint8_t> bytes(56 + body.size());
  std::copy_n("CWR1", 4, bytes.begin());
  auto put = [&](size_t at, uint64_t value, size_t n) {
    for (size_t i = 0; i < n; ++i)
      bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
  };
  put(4, 1, 2);
  put(6, static_cast<uint16_t>(kind), 2);
  put(8, 1, 8);
  put(16, seq, 8);
  put(24, token, 8);
  put(32, body.size(), 4);
  put(40, kind == WorkerReplyKind::Complete ? 7 : UINT32_MAX, 4);
  std::copy(body.begin(), body.end(), bytes.begin() + 56);
  return bytes;
}
class Operation final : public ManagedOperation {
 public:
  Operation(std::shared_ptr<State> state, const Entry& entry,
            const Request& parsed)
      : state_(std::move(state)), entry_(entry), request_(parsed.original) {
    if (entry_.kind != Kind::kCli || entry_.id != parsed.capability_id)
      throw Error(ErrorCode::kInvalid, "Binding mismatch");
  }

 private:
  void Coordinate() override {
    struct Exited {
      State& state;
      ~Exited() { state.exited = true; }
    } exited{*state_};
    try {
      Pipe commands, cancel, replies;
      struct Directory {
        int fd;
        ~Directory() {
          if (fd >= 0) close(fd);
        }
      } directory{
          open(state_->root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
      BrokerJournal journal(directory.fd, geteuid(), *state_);
      WorkerSession session(journal, commands.fd[1], cancel.fd[1],
                            replies.fd[0]);
      WorkerResult collector(ClientToken(), request_);
      auto token = session.Start(collector.Original());
      ++state_->starts;
      collector.Bind(token);
      session.Step();
      WorkerCommandReader reader(commands.fd[0], session.Generation());
      std::optional<WorkerCommand> command;
      for (int i = 0; i < 8 && !command; ++i) {
        session.Step();
        command = reader.ReadOne();
      }
      if (!command || command->request != request_ || command->token != token)
        throw std::runtime_error("request binding");
      auto send = [&](const auto& bytes) {
        if (write(replies.fd[1], bytes.data(), bytes.size()) !=
            static_cast<ssize_t>(bytes.size()))
          throw std::runtime_error("write frame");
      };
      auto receive = [&] {
        std::optional<WorkerEvent> event;
        for (int i = 0; i < 8 && !event; ++i) event = session.Step();
        return event;
      };
      send(Frame(token, 1, WorkerReplyKind::Accepted));
      auto event = receive();
      if (!event) throw std::runtime_error("accepted");
      collector.Accept(*event);
      send(Frame(token, 2, WorkerReplyKind::Stdout, native));
      event = receive();
      if (!event) throw std::runtime_error("stdout");
      collector.Accept(*event);
      send(Frame(token, 3, WorkerReplyKind::Complete));
      state_->block = true;
      event =
          receive();  // Only this coordinator may block in ConfirmJobGone fsync.
      if (!event) throw std::runtime_error("complete");
      state_->cancel_seen = CancellationRequested();
      std::optional<RunResult> result;
      try {
        result = collector.Accept(*event);
      } catch (const std::bad_alloc&) {
        if (!collector.TerminalPending()) throw;
      } catch (const std::length_error&) {
        if (!collector.TerminalPending()) throw;
      }
      if (!collector.Complete() && !collector.TerminalPending())
        throw std::runtime_error("proof mismatch");
      Publish(Cleanup::kConfirmedComplete);
      while (!result) {
        try {
          result = collector.RetryTerminal();
        } catch (const std::bad_alloc&) {
          std::this_thread::sleep_for(1ms);
        } catch (const std::length_error&) {
          std::this_thread::sleep_for(1ms);
        }
      }
      std::shared_ptr<const std::string> bytes;
      while (!bytes) {
        try {
          bytes = std::make_shared<const std::string>(result->response);
        } catch (const std::bad_alloc&) {
          std::this_thread::sleep_for(1ms);
        } catch (const std::length_error&) {
          std::this_thread::sleep_for(1ms);
        }
      }
      Publish(Cleanup::kConfirmedComplete, std::move(bytes));
      // Session/Journal and their possible destructor fsync are scoped HERE.
    } catch (const std::exception& error) {
      fprintf(stderr, "MANAGED_FIXTURE_ERROR: %s\n", error.what());
      state_->cancel_seen = CancellationRequested();
      Publish(Cleanup::kUncertain);
    } catch (...) {
      state_->cancel_seen = CancellationRequested();
      Publish(Cleanup::kUncertain);
    }
  }
  std::shared_ptr<State> state_;
  Entry entry_;
  std::string request_;
};
class Backend final : public ExecutionBackend {
 public:
  explicit Backend(std::shared_ptr<State> state) : state_(std::move(state)) {}
  void Admit(const Entry&, const Request&) override { ++state_->admissions; }
  std::shared_ptr<ManagedOperation> PrepareManagedCli(
      const Entry& entry, const Request& parsed) override {
    ++state_->preparations;
    if (state_->allocation_failure) throw std::bad_alloc();
    if (state_->reject)
      throw Error(ErrorCode::kPermission, "Preparation rejected");
    return std::make_shared<Operation>(state_, entry, parsed);
  }

 private:
  std::shared_ptr<State> state_;
};
void Seed(const std::string& path) {
  std::filesystem::create_directory(path);
  chmod(path.c_str(), 0700);
  std::ofstream(path + "/state.json")
      << R"({"version":1,"generation":0,"next":1,"state":"clean","jobs":[]})";
  chmod((path + "/state.json").c_str(), 0600);
}
void Count(uint64_t, const char*, bool, void* p) {
  ++*static_cast<std::atomic<int>*>(p);
}
}
TEST_F(CatalogTest,
       ManagedPublicDestroyRetainsHandleAcrossBlockedAndFailedFsync) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg", {Make("test", "pkg", Kind::kCli)});
  for (bool fail : {false, true}) {
    ASSERT_EXIT(
        ([&] {
          alarm(8);
          signal(SIGPIPE, SIG_IGN);
          auto state = std::make_shared<State>();
          state->root = root_ + "/journal";
          state->fail = fail;
          Seed(state->root);
          Gate gate(path_);
          capmgr_client_h client = nullptr;
          if (CreateClient(gate, std::make_shared<Backend>(state), &client))
            _exit(2);
          std::atomic<int> calls = 0;
          uint64_t token = 0;
          if (capmgr_client_execute(client, request.c_str(), Count, &calls,
                                    &token) ||
              token != 1)
            _exit(3);
          if (state->syncing.get_future().wait_for(2s) !=
                  std::future_status::ready ||
              calls != 0)
            _exit(4);
          auto start = std::chrono::steady_clock::now();
          if (capmgr_client_destroy(client) != CAPMGR_ERROR_IO ||
              std::chrono::steady_clock::now() - start > 500ms)
            _exit(5);
          uint64_t rejected = 77;
          if (capmgr_client_execute(client, request.c_str(), Count, &calls,
                                    &rejected) != CAPMGR_ERROR_BUSY ||
              rejected || state->admissions != 1 || state->preparations != 1 ||
              state->starts != 1)
            _exit(6);
          if (capmgr_client_cancel(client, token) != 0 ||
              capmgr_client_destroy(client) != CAPMGR_ERROR_IO)
            _exit(7);
          state->release.set_value();
          for (int i = 0; i < 2000 && !state->exited; ++i)
            std::this_thread::sleep_for(1ms);
          if (!state->exited || !state->cancel_seen) _exit(8);
          if (fail) {
            if (capmgr_client_destroy(client) != CAPMGR_ERROR_IO ||
                capmgr_client_cancel(client, token) != CAPMGR_ERROR_IO)
              _exit(9);
            std::ifstream input(state->root + "/state.json");
            Json json;
            input >> json;
            if (json["jobs"].size() != 1 ||
                !std::filesystem::exists(state->root + "/state.next"))
              _exit(10);
          } else if (capmgr_client_destroy(client) != 0)
            _exit(11);
          if (calls != 0) _exit(12);
          _exit(0);
        }()),
        testing::ExitedWithCode(0), "");
    std::filesystem::remove_all(root_ + "/journal");
  }
}
TEST_F(CatalogTest,
       ManagedPublicNativeResultWaitsForDurableCompleteAndPreservesBytes) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg", {Make("test", "pkg", Kind::kCli)});
  ASSERT_EXIT(
      ([&] {
        alarm(8);
        signal(SIGPIPE, SIG_IGN);
        auto state = std::make_shared<State>();
        state->root = root_ + "/journal";
        Seed(state->root);
        Gate gate(path_);
        capmgr_client_h client = nullptr;
        if (CreateClient(gate, std::make_shared<Backend>(state), &client))
          _exit(2);
        struct Callback {
          capmgr_client_h client;
          std::promise<std::string> result;
          std::atomic<int> calls = 0;
        } callback{client, {}};
        auto receive = +[](uint64_t token, const char* text, bool event,
                           void* p) {
          auto& cb = *static_cast<Callback*>(p);
          if (event || ++cb.calls != 1 ||
              capmgr_client_cancel(cb.client, token) != CAPMGR_ERROR_NOT_FOUND)
            _exit(3);
          cb.result.set_value(text);
        };
        uint64_t token = 0;
        if (capmgr_client_execute(client, request.c_str(), receive, &callback,
                                  &token))
          _exit(4);
        if (state->syncing.get_future().wait_for(2s) !=
                std::future_status::ready ||
            callback.calls != 0)
          _exit(5);
        state->release.set_value();
        auto result = callback.result.get_future();
        if (result.wait_for(2s) != std::future_status::ready ||
            result.get() != native)
          _exit(6);
        int status;
        do {
          status = capmgr_client_destroy(client);
        } while (status == CAPMGR_ERROR_BUSY || status == CAPMGR_ERROR_IO);
        if (status || callback.calls != 1) _exit(7);
        _exit(0);
      }()),
      testing::ExitedWithCode(0), "");
}
TEST_F(CatalogTest,
       ManagedPreparationFailureHasNoReservationAndClearsPublicToken) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg", {Make("test", "pkg", Kind::kCli)});
  auto state = std::make_shared<State>();
  state->root = root_ + "/never-created";
  state->reject = true;
  Gate gate(path_);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, std::make_shared<Backend>(state), &client), 0);
  std::atomic<int> calls = 0;
  uint64_t token = 77;
  EXPECT_EQ(
      capmgr_client_execute(client, request.c_str(), Count, &calls, &token),
      CAPMGR_ERROR_PERMISSION_DENIED);
  EXPECT_EQ(token, 0u);
  EXPECT_EQ(state->starts, 0);
  EXPECT_FALSE(std::filesystem::exists(state->root));
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  EXPECT_EQ(calls, 0);
}
TEST_F(CatalogTest, ActionNeverCallsManagedCliPreparation) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg", {Make("test", "pkg", Kind::kAction)});
  auto state = std::make_shared<State>();
  state->root = root_ + "/never-created";
  Gate gate(path_);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, std::make_shared<Backend>(state), &client), 0);
  auto action_request = request;
  action_request.replace(action_request.find("cli:test"), 8, "action:test");
  std::promise<void> callback;
  uint64_t token = 0;
  ASSERT_EQ(capmgr_client_execute(
                client, action_request.c_str(),
                +[](uint64_t, const char*, bool, void* p) {
                  static_cast<std::promise<void>*>(p)->set_value();
                },
                &callback, &token),
            0);
  EXPECT_EQ(callback.get_future().wait_for(2s), std::future_status::ready);
  int status;
  do {
    status = capmgr_client_destroy(client);
  } while (status == CAPMGR_ERROR_BUSY || status == CAPMGR_ERROR_IO);
  EXPECT_EQ(status, 0);
  EXPECT_EQ(state->admissions, 1);
  EXPECT_EQ(state->preparations, 0);
  EXPECT_EQ(state->starts, 0);
  EXPECT_FALSE(std::filesystem::exists(state->root));
}
TEST_F(CatalogTest, ManagedPreparationAllocationFailureRemainsSynchronous) {
  Catalog writer(path_, Database::Access::kWriter);
  Publish(writer, "pkg", {Make("test", "pkg", Kind::kCli)});
  auto state = std::make_shared<State>();
  state->root = root_ + "/never-created";
  state->allocation_failure = true;
  Gate gate(path_);
  capmgr_client_h client = nullptr;
  ASSERT_EQ(CreateClient(gate, std::make_shared<Backend>(state), &client), 0);
  std::atomic<int> calls = 0;
  uint64_t token = 77;
  EXPECT_EQ(
      capmgr_client_execute(client, request.c_str(), Count, &calls, &token),
      CAPMGR_ERROR_OUT_OF_MEMORY);
  EXPECT_EQ(token, 0u);
  EXPECT_EQ(state->starts, 0);
  EXPECT_FALSE(std::filesystem::exists(state->root));
  EXPECT_EQ(capmgr_client_destroy(client), 0);
  EXPECT_EQ(calls, 0);
}
