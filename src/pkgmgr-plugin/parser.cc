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
#include <filesystem>
#include <set>
#include <sstream>

namespace capmgr {

namespace {

class File {
 public:
  explicit File(int fd) : fd_(fd) {}
  ~File() {
    if (fd_ >= 0) close(fd_);
  }

  int get() const { return fd_; }
  File(const File&) = delete;
  File& operator=(const File&) = delete;

 private:
  int fd_;
};

std::string Trim(const std::string& value) {
  auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

int OpenRelative(int root, const std::string& path, bool directory = false) {
  if (path.empty() || path[0] == '/' || path.back() == '/' ||
      path.find('\0') != std::string::npos)
    throw Error(ErrorCode::kInvalid, "Expected package-relative path");
  int fd = dup(root);
  if (fd < 0) throw Error(ErrorCode::kIo, "Cannot duplicate package root");
  size_t start = 0;
  for (;;) {
    size_t end = path.find('/', start);
    bool last = end == std::string::npos;
    std::string segment = path.substr(start, last ? end : end - start);
    if (segment.empty() || segment == "." || segment == "..") {
      close(fd);
      throw Error(ErrorCode::kInvalid, "Unsafe package path component");
    }
    int next = openat(fd, segment.c_str(),
                      O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
                          ((!last || directory) ? O_DIRECTORY : O_NONBLOCK));
    close(fd);
    if (next < 0)
      throw Error(ErrorCode::kInvalid, "Missing or unsafe package resource");
    fd = next;
    if (last) return fd;
    start = end + 1;
  }
}

Json ReadDescriptor(int root, const std::string& path, size_t& bytes) {
  File fd(OpenRelative(root, path));
  struct stat st{};
  if (fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      st.st_size > 256 * 1024)
    throw Error(ErrorCode::kInvalid,
                "Descriptor must be a bounded regular file");
  std::string text;
  std::array<char, 4096> buffer{};
  for (;;) {
    ssize_t count = read(fd.get(), buffer.data(), buffer.size());
    if (count == 0) break;
    if (count < 0) {
      if (errno == EINTR) continue;
      throw Error(ErrorCode::kIo, "Descriptor read failed");
    }
    if (static_cast<size_t>(count) > 256 * 1024 - text.size() ||
        static_cast<size_t>(count) > 4 * 1024 * 1024 - bytes)
      throw Error(ErrorCode::kLimit, "Descriptor budget exceeded");
    text.append(buffer.data(), static_cast<size_t>(count));
    bytes += static_cast<size_t>(count);
  }

  Json json = Json::parse(text, nullptr, false);
  if (json.is_discarded() || !json.is_object())
    throw Error(ErrorCode::kInvalid, "Invalid descriptor JSON");
  return json;
}

std::string Text(const Json& json, const char* field) {
  if (!json.contains(field) || !json[field].is_string())
    throw Error(ErrorCode::kInvalid,
                std::string("Missing string field: ") + field);
  auto value = json[field].get<std::string>();
  if (value.empty() || value.find('\0') != std::string::npos)
    throw Error(ErrorCode::kInvalid, "Empty or NUL descriptor field");
  return value;
}
}  // namespace

std::vector<Entry> ParsePackage(const std::string& root,
                                const std::string& owner,
                                const std::vector<Metadata>& metadata) {
  if (root.empty() || root[0] != '/' || owner.empty())
    throw Error(ErrorCode::kInvalid, "Invalid package context");
  File root_fd(
      open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (root_fd.get() < 0)
    throw Error(ErrorCode::kIo, "Cannot open package root");
  std::vector<Entry> entries;
  std::set<std::string> references, ids;
  size_t bytes = 0;
  for (const auto& md : metadata) {
    if (md.kind != Kind::kSkill && md.kind != Kind::kAppSkill &&
        md.kind != Kind::kCli)
      throw Error(ErrorCode::kInvalid, "Action metadata parsing is forbidden");
    size_t begin = 0;
    for (;;) {
      size_t end = md.value.find(';', begin);
      std::string path = Trim(
          md.value.substr(begin, end == std::string::npos ? end : end - begin));
      if (path.empty())
        throw Error(ErrorCode::kInvalid, "Empty metadata entry");
      // JSON serialization makes delimiters in each tuple segment unambiguous.
      std::string reference =
          Json::array({static_cast<int>(md.kind), md.app_id, path}).dump();
      if (references.insert(reference).second) {
        if (references.size() > 1024)
          throw Error(ErrorCode::kLimit, "Too many descriptors");
        Json j = ReadDescriptor(root_fd.get(), path, bytes);
        if (!j.contains("version") || !j["version"].is_number_integer() ||
            j["version"] != 1)
          throw Error(ErrorCode::kUnsupported,
                      "Unsupported descriptor version");
        Entry e;
        e.key = Text(j, "key");
        e.name = Text(j, "name");
        e.desc = Text(j, "desc");
        e.owner = owner;
        e.kind = md.kind;
        e.app_id = md.kind == Kind::kAppSkill ? md.app_id : "";
        e.id = CanonicalId(e.kind, e.key, e.app_id);
        e.detail = Json::object();
        if (!ids.insert(e.id).second)
          throw Error(ErrorCode::kConflict, "Duplicate capability identity");
        if (j.contains("keywords")) e.keywords = Text(j, "keywords");
        if (e.kind == Kind::kCli) {
          auto executable = Text(j, "executable");
          if (!executable.starts_with("bin/"))
            throw Error(ErrorCode::kInvalid, "CLI must reside under bin");
          File file(OpenRelative(root_fd.get(), executable));
          struct stat st{};
          if (fstat(file.get(), &st) != 0 || !S_ISREG(st.st_mode) ||
              !(st.st_mode & 0111))
            throw Error(ErrorCode::kInvalid,
                        "CLI is not an executable regular file");
          e.executable = (std::filesystem::path(root) / executable).string();
          for (const char* field : {"inputSchema", "outputSchema"}) {
            if (!j.contains(field) || !j[field].is_object())
              throw Error(ErrorCode::kInvalid, "Missing CLI schema");
            e.detail[field] = j[field];
          }
        } else {
          auto resource = Text(j, "resource");
          if (e.kind == Kind::kAppSkill && !resource.starts_with("res/skills/"))
            throw Error(ErrorCode::kInvalid,
                        "App Skill must reside under res/skills");
          File dir(OpenRelative(root_fd.get(), resource, true));
          File skill(OpenRelative(dir.get(), "SKILL.md"));
          struct stat st{};
          if (fstat(skill.get(), &st) != 0 || !S_ISREG(st.st_mode))
            throw Error(ErrorCode::kInvalid, "SKILL.md must be regular");
          e.resource = (std::filesystem::path(root) / resource).string();
        }
        entries.push_back(std::move(e));
      }
      if (end == std::string::npos) break;
      begin = end + 1;
    }
  }
  return entries;
}

void StagePackage(CatalogWriter& catalog, const std::string& operation,
                  const std::string& root, const std::string& owner,
                  const std::vector<Metadata>& metadata,
                  FinalizationAuthority authority) {
  if (authority != FinalizationAuthority::kOfflineHarness)
    throw Error(ErrorCode::kUnsupported,
                "Authoritative installer finalization is unavailable");
  catalog.Stage(operation, owner, ParsePackage(root, owner, metadata));
}
}  // namespace capmgr
