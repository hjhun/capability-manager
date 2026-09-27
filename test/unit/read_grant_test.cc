// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "catalog/read_grant.hh"
#include <fcntl.h>
#include <future>
#include <sys/stat.h>
#include <unistd.h>
using namespace capmgr;
namespace {
struct GrantOperations : CatalogGrantOperations {
  Time now{};
  unsigned char sequence = 0;
  bool fail = false;
  Time Now() noexcept override { return now; }
  std::array<unsigned char, 32> Random() override {
    if (fail) throw std::bad_alloc();
    std::array<unsigned char, 32> bytes{};
    bytes.fill(++sequence);
    return bytes;
  }
};
struct GrantLabels : ReadLeaseOperations {
  GrantOperations* operations = nullptr;
  bool advance = false;
  std::string label = "fixture";
  std::string Label(int) override {
    if (advance) operations->now += std::chrono::seconds(6);
    return label;
  }
};
class GrantTest : public CatalogTest {
 protected:
  CatalogGrantBudget budget;
  GrantOperations ops;
  GrantLabels labels;
  std::string directory, lock_path;
  std::unique_ptr<Catalog> writer;
  void SetUp() override {
    CatalogTest::SetUp();
    labels.operations = &ops;
    directory = root_ + "/catalog";
    lock_path = root_ + "/lease";
    ASSERT_EQ(mkdir(directory.c_str(), 0700), 0);
    int fd =
        open(lock_path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    close(fd);
    path_ = directory + "/catalog.db";
    writer = std::make_unique<Catalog>(path_, Database::Access::kWriter);
    Publish(*writer, "pkg", {Make("fixture", "pkg", Kind::kCli)});
    for (auto suffix : {"", "-wal", "-shm"})
      ASSERT_EQ(chmod((path_ + suffix).c_str(), 0600), 0);
  }
  void TearDown() override {
    writer.reset();
    CatalogTest::TearDown();
  }
  auto Lease() {
    ReadLeasePolicy policy{directory, lock_path, geteuid(), geteuid(),
                           getegid(), getegid(), 0700,      0600,
                           0600,      "fixture", "fixture", "fixture"};
    return std::make_unique<CatalogReadLease>(policy, &labels);
  }
  bool Exclusive() {
    int fd = open(lock_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    int result = fcntl(fd, F_OFD_SETLK, &lock);
    close(fd);
    return result == 0;
  }
};
}
TEST_F(GrantTest, ReceiptIsCanonicalBoundedAndSeparateFromNonce) {
  CatalogReadGrant grant(budget, &ops);
  auto text = grant.Issue(Lease());
  auto receipt = ParseCatalogGrant(text);
  EXPECT_EQ(text.size(), 235u);
  EXPECT_EQ(receipt.nonce.size(), 64u);
  EXPECT_EQ(receipt.descriptor.size(), 165u);
  for (auto bad : {text + "x", text.substr(1), std::string(235, '0'),
                   std::string("CMG2:") + text.substr(5),
                   text.substr(0, 5) + "A" + text.substr(6),
                   text.substr(0, 69) + ";" + text.substr(70)})
    EXPECT_THROW(ParseCatalogGrant(bad), Error);
  EXPECT_THROW(grant.Confirm(receipt.descriptor), Error);
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest, IndependentClientLeaseOverlapsConsumeThenDuplicateDenies) {
  CatalogReadGrant grant(budget, &ops);
  auto receipt = ParseCatalogGrant(grant.Issue(Lease()));
  auto client = Lease();
  client->MatchDescriptor(receipt.descriptor);
  EXPECT_EQ(budget.Outstanding(), 1u);
  EXPECT_NO_THROW(grant.Confirm(receipt.nonce));
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_FALSE(Exclusive());
  EXPECT_THROW(grant.Confirm(receipt.nonce), Error);
  EXPECT_THROW(grant.Issue(Lease()), Error);
  client.reset();
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest, MatchingGenerationDoesNotAuthorizeAnotherInstanceOrOldNonce) {
  CatalogReadGrant a(budget, &ops), b(budget, &ops);
  auto first = ParseCatalogGrant(a.Issue(Lease())),
       second = ParseCatalogGrant(b.Issue(Lease()));
  ASSERT_EQ(first.descriptor, second.descriptor);
  ASSERT_NE(first.nonce, second.nonce);
  EXPECT_THROW(b.Confirm(first.nonce), Error);
  EXPECT_EQ(budget.Outstanding(), 1u);
  EXPECT_NO_THROW(a.Confirm(first.nonce));
  CatalogReadGrant replacement(budget, &ops);
  auto next = ParseCatalogGrant(replacement.Issue(Lease()));
  EXPECT_EQ(next.descriptor, first.descriptor);
  EXPECT_THROW(replacement.Confirm(first.nonce), Error);
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest,
       ConfirmationDeadlineAndDelayedExpiryHaveExplicitAvailabilityLimit) {
  CatalogReadGrant grant(budget, &ops);
  auto receipt = ParseCatalogGrant(grant.Issue(Lease()));
  ops.now += std::chrono::seconds(5);
  EXPECT_EQ(budget.Outstanding(), 1u);
  EXPECT_FALSE(Exclusive());
  EXPECT_THROW(grant.Confirm(receipt.nonce), Error);
  EXPECT_EQ(budget.Outstanding(), 0u);
  CatalogReadGrant idle(budget, &ops);
  idle.Issue(Lease());
  ops.now += std::chrono::hours(1);
  EXPECT_EQ(budget.Outstanding(), 1u);
  EXPECT_FALSE(Exclusive());  // stalled service timer
  idle.Expire();
  idle.Expire();
  idle.Revoke();
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest, DeadlineIsRecheckedAfterPotentiallySlowMetadataValidation) {
  CatalogReadGrant grant(budget, &ops);
  auto receipt = ParseCatalogGrant(grant.Issue(Lease()));
  labels.advance = true;
  EXPECT_THROW(grant.Confirm(receipt.nonce), Error);
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest, MetadataFailureRevokesInsteadOfConfirmingStaleGeneration) {
  CatalogReadGrant grant(budget, &ops);
  auto receipt = ParseCatalogGrant(grant.Issue(Lease()));
  labels.label = "changed";
  EXPECT_THROW(grant.Confirm(receipt.nonce), Error);
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest, BudgetBoundsAllInstancesAndEveryFailureReleasesOnce) {
  std::vector<std::unique_ptr<CatalogReadGrant>> grants;
  for (unsigned i = 0; i < CatalogGrantBudget::kLimit; ++i) {
    auto grant = std::make_unique<CatalogReadGrant>(budget, &ops);
    grant->Issue(Lease());
    grants.push_back(std::move(grant));
  }
  EXPECT_EQ(budget.Outstanding(), 64u);
  CatalogReadGrant excess(budget, &ops);
  EXPECT_THROW(excess.Issue(Lease()), Error);
  EXPECT_EQ(budget.Outstanding(), 64u);
  grants.pop_back();
  EXPECT_EQ(budget.Outstanding(), 63u);
  CatalogReadGrant failed(budget, &ops);
  ops.fail = true;
  EXPECT_THROW(failed.Issue(Lease()), std::bad_alloc);
  EXPECT_EQ(budget.Outstanding(), 63u);
  grants.clear();
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}
TEST_F(GrantTest, RealEntropyGeneratesDistinctGrantsForSameDescriptor) {
  CatalogReadGrant a(budget), b(budget);
  auto x = ParseCatalogGrant(a.Issue(Lease())),
       y = ParseCatalogGrant(b.Issue(Lease()));
  EXPECT_NE(x.nonce, y.nonce);
  EXPECT_EQ(x.descriptor, y.descriptor);
  a.Revoke();
  b.Revoke();
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
}

TEST_F(GrantTest, ConcurrentConfirmAndRevocationReleaseOneBudgetSlot) {
  CatalogReadGrant grant(budget, &ops);
  auto receipt = ParseCatalogGrant(grant.Issue(Lease()));
  auto confirmation = std::async(std::launch::async, [&] {
    try {
      grant.Confirm(receipt.nonce);
    } catch (const Error&) {
    }
  });
  auto revoke = std::async(std::launch::async, [&] { grant.Revoke(); });
  confirmation.get();
  revoke.get();
  grant.Expire();
  EXPECT_EQ(budget.Outstanding(), 0u);
  EXPECT_TRUE(Exclusive());
  EXPECT_THROW(grant.Confirm(receipt.nonce), Error);
}
