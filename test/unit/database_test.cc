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
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fixture.hh"

#include <sys/stat.h>

#include <array>
#include <iostream>
#include <memory>
#include <utility>

namespace {

thread_local int persist_failure = SQLITE_OK;
class FailPersist {
 public:
  explicit FailPersist(int code) { persist_failure = code; }
  ~FailPersist() { persist_failure = SQLITE_OK; }
};

}  // namespace

// GNU ld wrapping is enabled only for capmgr-unit-tests. Calls from production
// objects reach the real SQLite routine except the one explicitly injected call.
extern "C" int __real_sqlite3_file_control(sqlite3*, const char*, int, void*);
extern "C" int __wrap_sqlite3_file_control(sqlite3* db, const char* name,
                                           int op, void* value) {
  if (op == SQLITE_FCNTL_PERSIST_WAL && value &&
      *static_cast<int*>(value) == 1 && persist_failure != SQLITE_OK)
    return std::exchange(persist_failure, SQLITE_OK);
  return __real_sqlite3_file_control(db, name, op, value);
}

namespace {

using namespace capmgr;
using Identity = std::pair<dev_t, ino_t>;
using Identities = std::array<Identity, 3>;
class WriterWalTest : public CatalogTest {
 protected:
  Identities Files() {
    Identities files{};
    size_t index = 0;
    for (const char* suffix : {"", "-wal", "-shm"}) {
      struct stat info{};
      if (stat((path_ + suffix).c_str(), &info) != 0) {
        ADD_FAILURE() << "Missing file " << path_ + suffix;
      } else {
        EXPECT_TRUE(S_ISREG(info.st_mode));
        files[index] = {info.st_dev, info.st_ino};
      }
      ++index;
    }
    return files;
  }

  void Persistent(Database& db) {
    int flag = -1;
    ASSERT_EQ(sqlite3_file_control(db.handle(), "main",
                                   SQLITE_FCNTL_PERSIST_WAL, &flag),
              SQLITE_OK);
    EXPECT_EQ(flag, 1);
    Statement synchronous(db.handle(), "PRAGMA synchronous");
    ASSERT_TRUE(synchronous.Step());
    EXPECT_EQ(synchronous.Integer(0), 2);  // FULL
  }

  void Read(uint64_t revision, const std::string& term) {
    Catalog reader(path_, Database::Access::kReadOnly);
    EXPECT_EQ(sqlite3_db_readonly(reader.database().handle(), "main"), 1);
    EXPECT_EQ(reader.Revision(), revision);
    EXPECT_EQ(reader.Search(term).size(), 1u);
    EXPECT_THROW(reader.database().Exec("DELETE FROM capability"), Error);
  }
};

TEST_F(WriterWalTest, LastWriterThenRepeatedFreshReadersRetainFiles) {
  std::cout << "SQLITE_VERSION=" << sqlite3_libversion()
            << " SQLITE_SOURCE_ID=" << sqlite3_sourceid() << '\n';
  Identities files;
  {
    Catalog writer(path_, Database::Access::kWriter);
    Persistent(writer.database());
    Publish(writer, "pkg.one", {Make()});
    files = Files();
  }  // No readers, keepers or statements survive the last writer.
  EXPECT_EQ(Files(), files);
  Read(1, "pictures");
  EXPECT_EQ(Files(), files);
  Read(1, "pictures");  // The preceding read-only connection also closed.
  EXPECT_EQ(Files(), files);
}

TEST_F(WriterWalTest, IndependentWritersPersistInBothCloseOrders) {
  for (bool first_closes_first : {true, false}) {
    SCOPED_TRACE(first_closes_first);
    auto first = std::make_unique<Catalog>(path_, Database::Access::kWriter);
    auto second = std::make_unique<Catalog>(path_, Database::Access::kWriter);
    Persistent(first->database());
    Persistent(second->database());
    Publish(*first, "pkg.one", {Make()});
    const auto revision = first->Revision();
    const auto files = Files();
    if (first_closes_first) {
      first.reset();
      EXPECT_EQ(Files(), files);
      second.reset();
    } else {
      second.reset();
      EXPECT_EQ(Files(), files);
      first.reset();
    }
    EXPECT_EQ(Files(), files);
    Read(revision, "pictures");
    EXPECT_EQ(Files(), files);
  }
}

TEST_F(WriterWalTest, ReopenedWriterCommitsAndCheckpointsWithLiveReader) {
  Identities files;
  {
    Catalog writer(path_, Database::Access::kWriter);
    Publish(writer, "pkg.one", {Make()});
    files = Files();
  }
  {
    Catalog reader(path_, Database::Access::kReadOnly);
    EXPECT_EQ(reader.Revision(), 1u);
    EXPECT_EQ(reader.Search("pictures").size(), 1u);
    {
      Catalog writer(path_, Database::Access::kWriter);
      Persistent(writer.database());
      auto entry = Make();
      entry.desc = "Compose messages";
      Publish(writer, "pkg.one", {entry});
      // Each Catalog query finishes its read transaction before the next call.
      EXPECT_EQ(reader.Revision(), 2u);
      EXPECT_TRUE(reader.Search("pictures").empty());
      EXPECT_EQ(reader.Search("messages").size(), 1u);
      ASSERT_EQ(sqlite3_wal_checkpoint_v2(writer.database().handle(), "main",
                                          SQLITE_CHECKPOINT_TRUNCATE, nullptr,
                                          nullptr),
                SQLITE_OK);
      EXPECT_EQ(Files(), files);
    }
    EXPECT_EQ(Files(), files);
    EXPECT_EQ(reader.Search("messages").size(), 1u);
  }

  EXPECT_EQ(Files(), files);
  Read(2, "messages");
  EXPECT_EQ(Files(), files);
}

TEST_F(WriterWalTest, FileControlFailureRejectsConstructionAndAllowsRetry) {
  for (int code : {SQLITE_NOTFOUND, SQLITE_IOERR}) {
    SCOPED_TRACE(code);
    {
      FailPersist failure(code);
      try {
        Catalog writer(path_, Database::Access::kWriter);
        FAIL() << "Constructor accepted failed PERSIST_WAL";
      } catch (const Error& error) {
        EXPECT_EQ(error.code(), ErrorCode::kDatabase);
        const std::string message = error.what();
        EXPECT_NE(message.find("SQLITE_FCNTL_PERSIST_WAL"), std::string::npos);
        EXPECT_NE(message.find("(" + std::to_string(code) + ")"),
                  std::string::npos);
        EXPECT_NE(message.find(sqlite3_errstr(code)), std::string::npos);
        EXPECT_EQ(message.find("not an error"), std::string::npos);
      }
      EXPECT_EQ(persist_failure, SQLITE_OK);  // The exact setter was exercised.
    }
    Catalog writer(path_, Database::Access::kWriter);
    Persistent(writer.database());
    Publish(writer, "pkg.one", {Make()});
  }
}
}  // namespace
