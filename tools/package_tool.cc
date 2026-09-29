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

#include "pkgmgr-plugin/parser.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <iostream>
#include <set>

namespace {

using namespace capmgr;
struct File {
  int fd;
  ~File() {
    if (fd >= 0) close(fd);
  }
};

void Require(bool condition, const char* message) {
  if (!condition) throw Error(ErrorCode::kInvalid, message);
}

std::string Text(const Json& value, const char* field) {
  Require(value.contains(field) && value[field].is_string(),
          "Missing string field");
  auto result = value[field].get<std::string>();
  Require(!result.empty() && result.size() <= 4096 &&
              result.find('\0') == std::string::npos,
          "Empty, oversized or NUL string");
  return result;
}

void Fields(const Json& value, std::initializer_list<const char*> allowed) {
  Require(value.is_object(), "Expected object");
  for (const auto& item : value.items()) {
    bool found = false;
    for (const char* key : allowed)
      if (item.key() == key) found = true;
    Require(found, "Unknown manifest field");
  }
}

Json ReadManifest(const char* path) {
  File file{open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  struct stat st{};
  if (file.fd < 0 || fstat(file.fd, &st) != 0)
    throw Error(ErrorCode::kIo, "Cannot open manifest");
  Require(S_ISREG(st.st_mode) && st.st_size >= 0 && st.st_size <= 1024 * 1024,
          "Manifest must be a regular file of at most 1 MiB");
  std::string bytes;
  std::array<char, 4096> buffer{};
  for (;;) {
    auto count = read(file.fd, buffer.data(), buffer.size());
    if (count == 0) break;
    if (count < 0) {
      if (errno == EINTR) continue;
      throw Error(ErrorCode::kIo, "Cannot read manifest");
    }
    Require(static_cast<size_t>(count) <= 1024 * 1024 - bytes.size(),
            "Manifest exceeds 1 MiB");
    bytes.append(buffer.data(), static_cast<size_t>(count));
  }
  std::vector<std::set<std::string>> keys;
  auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
    Require(depth <= 32, "Manifest nesting exceeds 32");
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key)
      Require(keys.back().insert(parsed.get<std::string>()).second,
              "Duplicate manifest key");
    if (event == Json::parse_event_t::object_end) keys.pop_back();
    return true;
  };
  auto manifest = Json::parse(bytes, callback, false);
  Require(!manifest.is_discarded(), "Invalid manifest JSON");
  return manifest;
}

Json Stage(const std::string& db, const char* path) {
  auto manifest = ReadManifest(path);
  Fields(manifest,
         {"version", "operation", "owner", "mode", "root", "metadata"});
  Require(manifest.contains("version") &&
              manifest["version"].is_number_integer() &&
              manifest["version"] == 1,
          "Unsupported manifest version");
  auto operation = Text(manifest, "operation"), owner = Text(manifest, "owner");
  Require(owner.front() != '@', "Reserved internal owner");
  auto mode = Text(manifest, "mode");
  std::vector<Entry> entries;
  if (mode == "remove") {
    Require(!manifest.contains("root") && !manifest.contains("metadata"),
            "Removal must not contain root or metadata");
  } else {
    Require(mode == "replace", "Mode must be replace or remove");
    auto root = Text(manifest, "root");
    Require(manifest.contains("metadata") && manifest["metadata"].is_array() &&
                manifest["metadata"].size() <= 1024,
            "Expected bounded metadata array");
    std::vector<Metadata> metadata;
    for (const auto& item : manifest["metadata"]) {
      Fields(item, {"key", "value", "appId"});
      auto key = Text(item, "key");
      Kind kind;
      if (key == "http://tizen.org/metadata/capability/skill")
        kind = Kind::kSkill;
      else if (key == "http://tizen.org/metadata/capability/app-skill")
        kind = Kind::kAppSkill;
      else if (key == "http://tizen.org/metadata/capability/cli")
        kind = Kind::kCli;
      else
        throw Error(ErrorCode::kInvalid, "Unknown capability metadata key");
      std::string app;
      if (kind == Kind::kAppSkill)
        app = Text(item, "appId");
      else
        Require(!item.contains("appId"), "appId is only valid for App Skill");
      metadata.push_back({kind, Text(item, "value"), app});
    }
    // Validate every descriptor before bootstrapping or opening a writer DB.
    entries = ParsePackage(root, owner, metadata);
  }

  Catalog catalog(db, Database::Access::kWriter);
  catalog.Stage(operation, owner, entries);
  return {{"operation", operation},
          {"state", "pending"},
          {"revision", catalog.Revision()}};
}

Json Status(Catalog& catalog, const std::string& operation) {
  Statement pending(catalog.database().handle(),
                    "SELECT owner FROM pending WHERE operation=?");
  pending.Bind(1, operation);
  if (pending.Step()) {
    if (pending.Type(0) != SQLITE_TEXT)
      throw Error(ErrorCode::kDatabase, "Corrupt pending owner");
    return {{"operation", operation},
            {"state", "pending"},
            {"owner", pending.Text(0)},
            {"revision", catalog.Revision()}};
  }

  Statement done(catalog.database().handle(),
                 "SELECT success FROM completed WHERE operation=?");
  done.Bind(1, operation);
  if (!done.Step()) throw Error(ErrorCode::kNotFound, "Unknown operation");
  if (done.Type(0) != SQLITE_INTEGER ||
      (done.Integer(0) != 0 && done.Integer(0) != 1))
    throw Error(ErrorCode::kDatabase, "Corrupt final outcome");
  return {{"operation", operation},
          {"state", done.Integer(0) ? "success" : "failure"},
          {"revision", catalog.Revision()}};
}
}  // namespace

int main(int argc, char** argv) {
  // No default DB, environment override, service connection or production mode.
  if (argc < 5 || std::string(argv[1]) != "--offline" ||
      !((std::string(argv[3]) == "stage" && argc == 5) ||
        (std::string(argv[3]) == "status" && argc == 5) ||
        (std::string(argv[3]) == "finalize" && argc == 6))) {
    std::cerr
        << "Usage: capmgr-package-tool --offline ABSOLUTE_DB stage MANIFEST\n"
           "       capmgr-package-tool --offline ABSOLUTE_DB status OPERATION\n"
           "       capmgr-package-tool --offline ABSOLUTE_DB finalize OPERATION success|failure\n";
    return 2;
  }
  try {
    std::string db = argv[2], command = argv[3], operation = argv[4];
    Require(!db.empty() && db.front() == '/', "DB path must be absolute");
    Require(!operation.empty() && operation.size() <= 4096,
            "Invalid operation or manifest path");
    struct stat st{};
    int stat_result = lstat(db.c_str(), &st);
    if (stat_result == 0)
      Require(S_ISREG(st.st_mode), "DB must be a regular file, not a symlink");
    else if (errno != ENOENT || command != "stage")
      throw Error(ErrorCode::kIo, "DB must already exist");
    Json result;
    if (command == "stage")
      result = Stage(db, argv[4]);
    else {
      bool success = false;
      if (command == "finalize") {
        std::string outcome = argv[5];
        Require(outcome == "success" || outcome == "failure",
                "Explicit success or failure required");
        success = outcome == "success";
      }
      Catalog catalog(db, command == "status" ? Database::Access::kReadOnly
                                              : Database::Access::kWriter);
      if (command == "finalize") catalog.Finalize(operation, success);
      result = Status(catalog, operation);
    }
    std::cout << result.dump() << '\n';
    return 0;
  } catch (const Error& error) {
    std::cerr << Json({{"error",
                        {{"code", static_cast<int>(error.code())},
                         {"message", error.what()}}}})
                     .dump()
              << '\n';
  } catch (const std::exception&) {
    std::cerr
        << "{\"error\":{\"code\":-9,\"message\":\"Offline tool failed\"}}\n";
  }
  return 1;
}
