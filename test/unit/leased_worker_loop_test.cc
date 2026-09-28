// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <exception>
#include <functional>
#include <thread>
#include <type_traits>

#include "catalog/coordinated_writer.hh"
#include "launcher/leased_worker_loop.hh"

extern char** environ;
namespace capmgr {
// Trusted test startup bypass ONLY. No implementation of this friend is linked
// into the product. Ready() here is NOT successful root bootstrap/table evidence.
class LeasedWorkerLoopTestAccess {
 public:
  static std::unique_ptr<LeasedWorkerLoop> Construct(
      LeasedWorkerCatalogSnapshot&& snapshot, const WorkerContext& context,
      WorkerRuntime& runtime) {
    return std::unique_ptr<LeasedWorkerLoop>(
        new LeasedWorkerLoop(std::move(snapshot), context, runtime, {}));
  }
  static void Ready(LeasedWorkerLoop& owner) {
    owner.BeginStartup();
    owner.CompleteStartup();
  }
  static std::array<int, 5> Descriptors(LeasedWorkerLoop& owner) {
    return owner.CatalogDescriptors();
  }
  static WorkerInitialNamespaces InvalidNamespaces() {
    return WorkerInitialNamespaces(WorkerInitialNamespaces::Uncaptured{});
  }
};
}  // namespace capmgr
namespace {
using namespace capmgr;
using namespace std::chrono_literals;
struct Labels : GenerationLeaseOperations {
  std::string Label(int) override { return "Fixture"; }
};
struct Pipe {
  int fds[2]{-1, -1};
  Pipe() {
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK)) throw std::runtime_error("pipe");
  }
  ~Pipe() {
    for (auto fd : fds)
      if (fd >= 0) close(fd);
  }
};
struct Signals {
  struct sigaction pipe{}, child{};
  Signals() {
    sigaction(SIGPIPE, nullptr, &pipe);
    sigaction(SIGCHLD, nullptr, &child);
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &action, nullptr);
    action.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &action, nullptr);
  }
  ~Signals() {
    sigaction(SIGPIPE, &pipe, nullptr);
    sigaction(SIGCHLD, &child, nullptr);
  }
};
struct Runtime : WorkerRuntime {
  int spawns = 0;
  pid_t Spawn(NamespaceInitConfig& config, void*) noexcept override {
    ++spawns;
    const pid_t child = fork();
    if (child != 0) return child;
    // Ordinary direct-child test, not NamespaceInit containment/nondelegation.
    // No SQLite connection exists at fork. Close inherited lease references only.
    for (int fd = 3; fd < 1024; ++fd)
      if (fd != config.status_write && fd != config.control_read) close(fd);
    InitMessage ready{InitMessageKind::Ready, InitStage::Go, 0, 0, 0};
    if (write(config.status_write, &ready, sizeof(ready)) != sizeof(ready))
      _exit(88);
    for (;;)
      pause();  // Parent loop cancels/reaps through its real owner table.
  }
};
class LeasedWorkerLoopTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    const auto directory = root_ + "/catalog";
    ASSERT_EQ(mkdir(directory.c_str(), 0700), 0);
    const auto lock = root_ + "/generation.lock";
    int fd = open(lock.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fchmod(fd, 0600), 0);
    ASSERT_EQ(close(fd), 0);
    policy_ = {directory, lock, getuid(), getuid(),  getgid(),  getgid(),
               0700,      0600, 0600,     "Fixture", "Fixture", "Fixture"};
    path_ = directory + "/catalog.db";
    {
      CoordinatedCatalogWriter writer(
          policy_, CatalogGenerationLease::Mode::kMaintenance, 0ms, &labels_);
      auto cli = Make("fixture", "pkg.one", Kind::kCli);
      cli.executable = "/fixture";
      writer.Stage("fixture", cli.owner, {cli});
      writer.Finalize("fixture", true);
      writer.Close();
    }
    directory_ = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    parent_ = open("/proc/self", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ASSERT_GE(directory_, 0);
    ASSERT_GE(parent_, 0);
  }
  void TearDown() override {
    if (owner_) {
      owner_->Shutdown();
      const auto end = WorkerLoop::Clock::now() + 3s;
      while (!owner_->Quiescent() && WorkerLoop::Clock::now() < end) Tick();
      if (!owner_->Quiescent()) Retain();
      if (owner_->DeliveryLost())
        owner_->RetireAfterDeliveryLoss();
      else {
        while (!owner_->CanExitCleanly() && WorkerLoop::Clock::now() < end)
          Tick();
        if (!owner_->CanExitCleanly()) Retain();
        owner_->RetireCleanly();
      }
      owner_.reset();
    }
    bool complete = true;
    for (auto& record : records_) {
      if (!record.id) continue;
      auto result = children_.StopAndWait(record.id, 1000ms);
      if (result.state == ChildState::Uncertain) ownership_uncertain_ = true;
      if (result.state != ChildState::Complete) {
        complete = false;
        continue;  // Keep this record while attempting all other known cleanup.
      }
      children_.Release(record.id);
      record = {};
    }
    if (!complete || !CanDeleteScope()) Retain();
    close(directory_);
    close(parent_);
    CatalogTest::TearDown();
  }
  [[noreturn]] void Retain() {
    std::cerr << "RETAINED_WORKER_OWNER_SCOPE=" << root_ << std::endl;
    std::terminate();
  }
  WorkerContext Context() {
    return {7,   command_.fds[0], cancel_.fds[0], reply_.fds[1], parent_, 301,
            301, "System"};
  }
  LeasedWorkerCatalogSnapshot Snapshot() {
    WorkerCatalogReader reader(directory_, policy_, &labels_);
    return reader.Finish();  // Physical SQL close BEFORE every later fork.
  }
  void Owner() {
    owner_ =
        LeasedWorkerLoopTestAccess::Construct(Snapshot(), Context(), runtime_);
    LeasedWorkerLoopTestAccess::Ready(*owner_);
  }
  void Tick(bool drain = true) {
    owner_->Step();
    if (drain && reply_.fds[0] >= 0) {
      std::array<char, 8192> data{};
      while (read(reply_.fds[0], data.data(), data.size()) > 0) {
      }
    }
    std::this_thread::sleep_for(1ms);
  }
  void SendStart(bool valid = true) {
    auto bytes = EncodeWorkerCommand(
        {WorkerCommandKind::Start, 7, 1, 1,
         valid
             ? R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:fixture","arguments":{}}})"
             : R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cli:missing","arguments":{}}})"});
    ASSERT_EQ(write(command_.fds[1], bytes.data(), bytes.size()),
              static_cast<ssize_t>(bytes.size()));
  }
  struct Record {
    pid_t pid = -1;
    uint64_t id = 0;
  };
  Record& Reserve() {
    for (auto& record : records_) {
      if (!record.id) {
        record.id = children_.Reserve();
        return record;
      }
    }
    Retain();
  }
  bool CanDeleteScope() const {
    if (ownership_uncertain_) return false;
    for (const auto& record : records_)
      if (record.id) return false;
    return children_.Size() == 0;
  }
  Record& LaunchChild(const std::function<void()>& action) {
    auto& record = Reserve();
    const pid_t child = fork();
    if (child > 0) {
      children_.AttachReserved(record.id, child);
      record.pid = child;
    } else if (child < 0) {
      children_.AbandonUnspawned(record.id);
      record = {};
    } else {
      std::set_terminate([] { _exit(77); });
      try {
        action();
      } catch (...) {
        _exit(89);
      }
      _exit(0);
    }
    return record;
  }
  int Child(const std::function<void()>& action) {
    auto& record = LaunchChild(action);
    return record.id ? WaitChild(record) : -1;
  }
  int WaitChild(Record& record, std::chrono::milliseconds budget = 3s) {
    const auto end = WorkerLoop::Clock::now() + budget;
    ChildStatus result;
    try {
      result = children_.Inspect(record.id);
      while (result.state == ChildState::Running &&
             WorkerLoop::Clock::now() < end) {
        std::this_thread::sleep_for(1ms);
        result = children_.Inspect(record.id);
      }
    } catch (...) {
      ownership_uncertain_ = true;
      throw;
    }
    if (result.state == ChildState::Uncertain) ownership_uncertain_ = true;
    if (result.state != ChildState::Complete) return -1;
    children_.Release(record.id);
    record = {};
    return result.signal ? -result.signal : result.exit_code;
  }
  void Probe(bool busy) {
    const auto executable =
        std::filesystem::read_symlink("/proc/self/exe").parent_path().string() +
        "/capmgr-sqlite-lock-probe";
    std::string expected = busy ? "BUSY" : "OK";
    char* args[] = {const_cast<char*>(executable.c_str()),
                    path_.data(),
                    policy_.lock_path.data(),
                    const_cast<char*>("generation"),
                    expected.data(),
                    nullptr};
    pid_t child = -1;
    auto& record = Reserve();
    const int rc = posix_spawn(&child, executable.c_str(), nullptr, nullptr,
                               args, environ);
    if (rc == 0) {
      children_.AttachReserved(record.id, child);
      record.pid = child;
    } else {
      children_.AbandonUnspawned(record.id);
      record = {};
    }
    ASSERT_EQ(rc, 0);
    EXPECT_EQ(WaitChild(record), 0);
  }
  void FillReply() {
    std::array<char, 4096> data{};
    while (write(reply_.fds[1], data.data(), data.size()) > 0) {
    }
    ASSERT_EQ(errno, EAGAIN);
  }
  Signals signals_;
  Pipe command_, cancel_, reply_;
  Runtime runtime_;
  Labels labels_;
  ReadLeasePolicy policy_{};
  int directory_ = -1, parent_ = -1;
  std::unique_ptr<LeasedWorkerLoop> owner_;
  OwnedChildren children_;
  std::array<Record, 4> records_{};
  bool ownership_uncertain_ = false;
};
}  // namespace

TEST_F(LeasedWorkerLoopTest, NormalRetirementIsTerminalAndReleasesSameLease) {
  Owner();
  Probe(true);
  EXPECT_TRUE(owner_->AdmissionOpen());
  EXPECT_FALSE(owner_->CanExitCleanly());
  EXPECT_THROW(owner_->RetireCleanly(), Error);
  EXPECT_THROW(owner_->RetireAfterDeliveryLoss(), Error);
  owner_->Shutdown();
  ASSERT_TRUE(owner_->CanExitCleanly());
  Probe(true);
  owner_->RetireCleanly();
  Probe(false);
  EXPECT_THROW(owner_->Step(), Error);
  EXPECT_THROW(owner_->Shutdown(), Error);
  EXPECT_THROW(owner_->Jobs(), Error);
  EXPECT_THROW(owner_->Revision(), Error);
  owner_.reset();
}
TEST_F(LeasedWorkerLoopTest, NoStepOrReadyBeforeOwnerBoundOneShotStartup) {
  auto owner =
      LeasedWorkerLoopTestAccess::Construct(Snapshot(), Context(), runtime_);
  EXPECT_THROW(owner->Step(), Error);
  EXPECT_THROW(owner->Shutdown(), Error);
  Probe(true);
  auto namespaces = LeasedWorkerLoopTestAccess::InvalidNamespaces();
  WorkerBootstrapPolicy policy{0, {}, "System", namespaces};
  EXPECT_THROW(FinishWorkerBootstrap(policy, *owner), std::system_error);
  EXPECT_THROW(FinishWorkerBootstrap(policy, *owner), Error);
  EXPECT_THROW(owner->Step(), Error);
  Probe(true);
  owner.reset();  // Pre-admission failure; no job could have been admitted.
  Probe(false);
}
TEST_F(LeasedWorkerLoopTest, FactoryValidationRejectsBeforeLeaseOrSQLite) {
  auto namespaces = LeasedWorkerLoopTestAccess::InvalidNamespaces();
  WorkerBootstrapPolicy policy{0, {}, "System", namespaces};
  auto missing = policy_;
  missing.directory += "/absent";
  EXPECT_THROW(
      LeasedWorkerLoop::LoadAndFinish(policy, Context(), missing, runtime_),
      std::system_error);
  EXPECT_FALSE(std::filesystem::exists(missing.directory));
  EXPECT_EQ(runtime_.spawns, 0);
  Probe(false);
}
TEST_F(LeasedWorkerLoopTest, ConcreteReportRejectsFlagsBeforePublication) {
  auto owner =
      LeasedWorkerLoopTestAccess::Construct(Snapshot(), Context(), runtime_);
  const auto report = LeasedWorkerLoopTestAccess::Descriptors(*owner);
  for (size_t i = 0; i < report.size(); ++i) {
    struct stat st{};
    ASSERT_EQ(fstat(report[i], &st), 0);
    EXPECT_TRUE(i == 1 ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
    EXPECT_EQ(fcntl(report[i], F_GETFD), FD_CLOEXEC);
    EXPECT_EQ(bool(fcntl(report[i], F_GETFL) & O_PATH), i >= 2);
    for (size_t j = 0; j < i; ++j) EXPECT_NE(report[i], report[j]);
  }
  struct stat borrowed{}, held{};
  ASSERT_EQ(fstat(directory_, &borrowed), 0);
  ASSERT_EQ(fstat(report[1], &held), 0);
  EXPECT_NE(directory_, report[1]);
  EXPECT_EQ(borrowed.st_dev, held.st_dev);
  EXPECT_EQ(borrowed.st_ino, held.st_ino);
  ASSERT_EQ(fcntl(report[4], F_SETFD, 0), 0);
  EXPECT_THROW(LeasedWorkerLoopTestAccess::Descriptors(*owner), Error);
  ASSERT_EQ(fcntl(report[4], F_SETFD, FD_CLOEXEC), 0);
  EXPECT_THROW(LeasedWorkerLoopTestAccess::Descriptors(*owner),
               Error);  // Poisoned.
  Probe(true);
  owner.reset();
  Probe(false);
}
TEST_F(LeasedWorkerLoopTest, MovedFromAndConstructorFailureCannotPublish) {
  auto snapshot = Snapshot();
  auto moved = std::move(snapshot);
  EXPECT_THROW(LeasedWorkerLoopTestAccess::Construct(std::move(snapshot),
                                                     Context(), runtime_),
               Error);
  Probe(true);
  auto context = Context();
  context.generation = 0;
  EXPECT_THROW(LeasedWorkerLoopTestAccess::Construct(std::move(moved), context,
                                                     runtime_),
               std::runtime_error);
  Probe(false);
}
TEST_F(LeasedWorkerLoopTest,
       ChildFreeQueuedCompleteMustDrainBeforeNormalRelease) {
  Owner();
  FillReply();
  SendStart(false);
  for (int i = 0; i < 10; ++i) Tick(false);
  ASSERT_EQ(runtime_.spawns, 0);
  owner_->Shutdown();
  ASSERT_TRUE(owner_->Quiescent());
  EXPECT_FALSE(owner_->CanExitCleanly());
  EXPECT_THROW(owner_->RetireCleanly(), Error);
  Probe(true);
  for (int i = 0; i < 100 && !owner_->CanExitCleanly(); ++i) Tick();
  ASSERT_TRUE(owner_->CanExitCleanly());
  owner_->RetireCleanly();
  Probe(false);
  owner_.reset();
}
TEST_F(LeasedWorkerLoopTest, LossCannotRetireUntilActualChildCleanup) {
  Owner();
  SendStart();
  for (int i = 0; i < 10; ++i) Tick();
  ASSERT_EQ(runtime_.spawns, 1);
  ASSERT_FALSE(owner_->Quiescent());
  ASSERT_EQ(close(reply_.fds[0]), 0);
  reply_.fds[0] = -1;
  owner_->Step();  // Closed reply endpoint closes admission and starts cleanup.
  ASSERT_TRUE(owner_->DeliveryLost());
  ASSERT_FALSE(owner_->Quiescent());
  EXPECT_THROW(owner_->RetireAfterDeliveryLoss(), Error);
  Probe(true);
  const auto end = WorkerLoop::Clock::now() + 3s;
  while (!owner_->Quiescent() && WorkerLoop::Clock::now() < end) Tick(false);
  ASSERT_TRUE(owner_->Quiescent());
  EXPECT_FALSE(owner_->CanExitCleanly());
  // Current loop clears these tokens after confirmed cleanup even when lost.
  // Retirement relies on lost+Quiescent, without treating token count as proof.
  owner_->RetireAfterDeliveryLoss();
  Probe(false);
  EXPECT_THROW(owner_->CanExitCleanly(), Error);
  owner_.reset();
}
TEST_F(LeasedWorkerLoopTest, InheritedOperationsRejectBeforeLoopAndDestructor) {
  Owner();
  EXPECT_EQ(Child([&] {
              int rejected = 0;
              auto refuse = [&](const std::function<void()>& operation) {
                try {
                  operation();
                } catch (const Error&) {
                  ++rejected;
                }
              };
              refuse([&] { owner_->Step(); });
              refuse([&] { owner_->Shutdown(); });
              refuse([&] { owner_->AdmissionOpen(); });
              refuse([&] { owner_->Quiescent(); });
              refuse([&] { owner_->CanExitCleanly(); });
              refuse([&] { owner_->DeliveryLost(); });
              refuse([&] { owner_->Jobs(); });
              refuse([&] { owner_->Revision(); });
              refuse([&] { owner_->RetireCleanly(); });
              refuse([&] { owner_->RetireAfterDeliveryLoss(); });
              _exit(rejected == 10 ? 0 : 90);
            }),
            0);
  Probe(true);
  EXPECT_EQ(Child([&] { owner_.reset(); }), 77);  // BEFORE inherited loop dtor.
  Probe(true);
}
TEST_F(LeasedWorkerLoopTest, OpenAdmissionImplicitDestructionFailStops) {
  // Construct inside the owned child, after all parent SQLite has closed.
  EXPECT_EQ(Child([&] {
              auto owner = LeasedWorkerLoopTestAccess::Construct(
                  Snapshot(), Context(), runtime_);
              LeasedWorkerLoopTestAccess::Ready(*owner);
              owner.reset();
            }),
            77);
  Probe(false);
}
TEST_F(LeasedWorkerLoopTest, QueuedChildFreeCompleteDestructionFailStops) {
  EXPECT_EQ(Child([&] {
              Owner();
              FillReply();
              SendStart(false);
              for (int i = 0; i < 10; ++i) Tick(false);
              owner_->Shutdown();
              if (!owner_->Quiescent() || owner_->CanExitCleanly()) _exit(91);
              owner_.reset();
            }),
            77);
  Probe(false);
}

TEST_F(LeasedWorkerLoopTest, InheritedCompletedSnapshotCannotConstructOwner) {
  auto snapshot = Snapshot();
  EXPECT_EQ(Child([&] {
              try {
                auto owner = LeasedWorkerLoopTestAccess::Construct(
                    std::move(snapshot), Context(), runtime_);
                _exit(91);
              } catch (const Error&) {
                _exit(0);
              }
            }),
            0);
  Probe(
      true);  // Child close-only references cannot unlock the parent description.
}

TEST_F(LeasedWorkerLoopTest, LaterProbeCannotLosePendingChildOrEnableDeletion) {
  auto& first = LaunchChild([] {
    for (;;) pause();
  });
  ASSERT_GT(first.pid, 0);
  const auto held_id = first.id;
  EXPECT_EQ(WaitChild(first, 0ms),
            -1);  // Live child, intentionally no wait budget.
  EXPECT_FALSE(CanDeleteScope());
  Probe(
      false);  // A separate record, never overwrites the unconfirmed first one.
  EXPECT_EQ(first.id, held_id);
  EXPECT_EQ(children_.Size(), 1u);
  EXPECT_FALSE(CanDeleteScope());
  EXPECT_TRUE(std::filesystem::exists(root_));
  const auto status = children_.StopAndWait(first.id, 1000ms);
  ASSERT_EQ(status.state, ChildState::Complete);
  children_.Release(first.id);
  first = {};
  EXPECT_TRUE(CanDeleteScope());  // Only actual known reap/release clears it.
}

TEST_F(LeasedWorkerLoopTest, UnknownOwnershipCannotBeClearedBySuccessfulProbe) {
  // Pure fault injection: NO actual child ownership has been lost. This tests
  // the final deletion latch; actual Uncertain table records cannot be reset.
  ownership_uncertain_ = true;
  Probe(false);
  EXPECT_EQ(children_.Size(), 0u);
  EXPECT_FALSE(CanDeleteScope());
  EXPECT_TRUE(std::filesystem::exists(root_));
  ownership_uncertain_ =
      false;  // TEST-only reset of the no-launch injected fault.
}
TEST_F(LeasedWorkerLoopTest, ReportPreservesDeclaredNonblockingLockFlag) {
  auto owner =
      LeasedWorkerLoopTestAccess::Construct(Snapshot(), Context(), runtime_);
  const auto report = LeasedWorkerLoopTestAccess::Descriptors(*owner);
  const int flags = fcntl(report[0], F_GETFL);
  ASSERT_NE(flags & O_NONBLOCK, 0);
  ASSERT_EQ(fcntl(report[0], F_SETFL, flags & ~O_NONBLOCK), 0);
  EXPECT_THROW(LeasedWorkerLoopTestAccess::Descriptors(*owner), Error);
  ASSERT_EQ(fcntl(report[0], F_SETFL, flags), 0);
  EXPECT_THROW(LeasedWorkerLoopTestAccess::Descriptors(*owner),
               Error);  // Poisoned.
  Probe(true);
  owner.reset();
  Probe(false);
}
