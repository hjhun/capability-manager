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

// Fixed, explicitly injected root/UID301 fixture roles. Not a product endpoint.
#include "api/read_admission.hh"
#include "catalog/coordinated_writer.hh"

#include <dirent.h>

#include <filesystem>

#include <signal.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <grp.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>

using namespace capmgr;

namespace {

using Clock = std::chrono::steady_clock;
void Check(bool okay, const char* cause) {
  if (!okay) throw std::runtime_error(cause);
}

void Await(int fd, short events, Clock::time_point end) {
  for (;;) {
    int remaining =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                             end - Clock::now())
                             .count());
    Check(remaining > 0, "control deadline");
    pollfd item{fd, events, 0};
    int result = poll(&item, 1, remaining);
    if (result < 0 && errno == EINTR) continue;
    Check(result == 1 && (item.revents & events), "control pipe loss/deadline");
    return;
  }
}

void Send(Json value) {
  auto text = value.dump() + "\n";
  Check(text.size() <= 4096, "status size");
  auto end = Clock::now() + std::chrono::seconds(3);
  size_t offset = 0;
  while (offset != text.size()) {
    Await(4, POLLOUT, end);
    ssize_t n = write(4, text.data() + offset, text.size() - offset);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    Check(n > 0, "status write");
    offset += static_cast<size_t>(n);
  }
}

Json Receive() {
  auto end = Clock::now() + std::chrono::seconds(20);
  std::string bytes;
  for (;;) {
    Await(3, POLLIN, end);
    char byte;
    ssize_t count = read(3, &byte, 1);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    Check(count == 1, "control EOF");
    if (byte == '\n') break;
    Check(bytes.size() < 4095, "control byte limit");
    bytes += byte;
  }
  // All configuration is from the fixed privileged fixture coordinator. Even
  // that source cannot request executable, UID, arbitrary path or policy rules.
  std::vector<std::set<std::string>> keys;
  auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
    Check(depth <= 16, "control nesting");
    if (event == Json::parse_event_t::object_start) {
      Check(keys.size() < 16, "control nesting");
      keys.emplace_back();
    } else if (event == Json::parse_event_t::key) {
      Check(
          !keys.empty() && keys.back().insert(value.get<std::string>()).second,
          "duplicate control key");
    } else if (event == Json::parse_event_t::object_end) {
      keys.pop_back();
    }
    return true;
  };
  return Json::parse(bytes, callback);
}

std::string TaskLabel() {
  std::ifstream file("/proc/self/attr/current", std::ios::binary);
  Check(static_cast<bool>(file), "own task label open");
  std::string text((std::istreambuf_iterator<char>(file)), {});
  while (!text.empty() && (text.back() == '\0' || text.back() == '\n'))
    text.pop_back();
  Check(!text.empty() && text.size() <= 255, "own task label bytes");
  return text;
}

void Table() {
  DIR* tasks = opendir("/proc/self/task");
  Check(tasks, "own task scan");
  size_t task_count = 0;
  int scan_error = 0;
  for (;;) {
    errno = 0;
    auto* item = readdir(tasks);
    if (!item) {
      scan_error = errno;
      break;
    }
    if (item->d_name[0] != '.') ++task_count;
  }

  int closed = closedir(tasks);
  Check(scan_error == 0 && closed == 0, "incomplete own task scan");
  Check(task_count == 1, "fixture initial single thread");
  DIR* scan = opendir("/proc/self/fd");
  Check(scan, "own FD scan");
  int scan_fd = dirfd(scan);
  bool okay = true;
  size_t count = 0;
  for (;;) {
    errno = 0;
    auto* item = readdir(scan);
    if (!item) {
      scan_error = errno;
      break;
    }
    char* end = nullptr;
    long number = strtol(item->d_name, &end, 10);
    if (!end || *end || end == item->d_name || number == scan_fd) continue;
    okay &= number >= 0 && number <= 5;
    ++count;
  }

  closed = closedir(scan);
  Check(scan_error == 0 && closed == 0, "incomplete own FD scan");
  Check(okay && count == 6, "unexpected inherited descriptor");
  struct stat control_identity{};
  for (int fd = 0; fd != 6; ++fd) {
    struct stat info{};
    Check(fstat(fd, &info) == 0, "required descriptor absent");
    int flags = fcntl(fd, F_GETFL);
    Check(flags >= 0, "descriptor flags");
    if (fd <= 2)
      Check(S_ISCHR(info.st_mode) && info.st_rdev == makedev(1, 3) &&
                (flags & O_ACCMODE) == (fd == 0 ? O_RDONLY : O_WRONLY),
            "stdio is not null");
    else if (fd == 3 || fd == 4)
      Check(S_ISFIFO(info.st_mode) &&
                (flags & O_ACCMODE) == (fd == 3 ? O_RDONLY : O_WRONLY),
            "control type/direction");
    else
      Check((flags & O_ACCMODE) == O_RDWR && S_ISREG(info.st_mode) &&
                info.st_uid == 0 && info.st_gid == 0 && info.st_nlink == 1 &&
                (info.st_mode & 07777) == 0600,
            "recovery reference");
    if (fd == 3) control_identity = info;
    if (fd == 4)
      Check(info.st_dev != control_identity.st_dev ||
                info.st_ino != control_identity.st_ino,
            "aliased control pipes");
    Check(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "descriptor CLOEXEC");
    if (fd == 3 || fd == 4)
      Check(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, "control nonblocking");
  }
}

void Drop(const std::string& label, bool group) {
  // Own-task fixture context only, never peer credential authority.
  int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
  Check(fd >= 0, "own task label writer");
  ssize_t written = write(fd, label.data(), label.size());
  int closed = close(fd);
  Check(written == static_cast<ssize_t>(label.size()) && closed == 0,
        "own task label change");
  for (int cap = 0; cap != 64; ++cap) {
    errno = 0;
    int present = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (present < 0 && errno == EINVAL) break;
    Check(present >= 0 && prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) == 0,
          "bounding capability drop");
  }

  Check(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) == 0,
        "ambient capability clear");
  gid_t platform = 10212;
  Check(setgroups(group ? 1 : 0, group ? &platform : nullptr) == 0,
        "supplementary groups");
  Check(setresgid(301, 301, 301) == 0 && setresuid(301, 301, 301) == 0,
        "role IDs drop");
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  std::array<__user_cap_data_struct, 2> data{};
  Check(syscall(SYS_capset, &header, data.data()) == 0,
        "all capability sets clear");
  Check(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0, "no new privileges");
  uid_t real, effective, saved;
  gid_t rgid, egid, sgid;
  Check(getresuid(&real, &effective, &saved) == 0 && real == 301 &&
            effective == 301 && saved == 301 &&
            getresgid(&rgid, &egid, &sgid) == 0 && rgid == 301 && egid == 301 &&
            sgid == 301,
        "all role IDs");
  std::array<gid_t, 2> groups{};
  int count = getgroups(static_cast<int>(groups.size()), groups.data());
  Check(count == (group ? 1 : 0) && (!group || groups[0] == 10212),
        "exact role groups");
  Check(syscall(SYS_capget, &header, data.data()) == 0, "capability verify");
  for (const auto& item : data)
    Check(!(item.effective | item.permitted | item.inheritable),
          "retained capability set");
  for (int cap = 0; cap != 64; ++cap) {
    errno = 0;
    int value = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
    if (value < 0 && errno == EINVAL) break;
    Check(value == 0 &&
              prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0) == 0,
          "retained bounding/ambient capability");
  }

  Check(prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1 && TaskLabel() == label,
        "context label/NNP");
}

class Channel final : public CatalogAdmissionChannel {
 public:
  std::string descriptor;
  int authorize = 0, confirm = 0, finish = 0;
  std::string AuthorizeCatalog() override {
    ++authorize;
    return descriptor;
  }

  void CheckSameLive() override {}  // explicitly modeled, not socket authority
  void ConfirmCatalog(std::string_view value) override {
    ++confirm;
    Check(value == descriptor, "modeled descriptor confirmation");
    Send({{"stage", "confirm"}});
    auto reply = Receive();
    Check(reply == Json{{"command", "confirmed"}}, "modeled confirm reply");
  }

  void Finish() override { ++finish; }
};

}  // namespace

namespace {

ReadLeasePolicy Policy(const std::string& key) {
  Check(key.size() == 32 &&
            key.find_first_not_of("0123456789abcdef") == std::string::npos,
        "fixture key");
  auto root = "/opt/usr/capmgr-read-policy-" + key;
  auto label = "CapMgrReadPolicy::" + key + "::";
  return {root + "/catalog",
          root + "/generation/generation.lock",
          0,
          0,
          10212,
          10212,
          02750,
          0640,
          0640,
          label + "Catalog",
          label + "Catalog",
          label + "Lock"};
}

void DeniedOpen(const std::string& path, int flags) {
  errno = 0;
  int fd = open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd >= 0) {
    close(fd);
    throw std::runtime_error("forbidden fixture open succeeded");
  }
  int error = errno;
  Check(error == EACCES || error == EPERM, "denial was not access veto");
}

void Probe(const ReadLeasePolicy& policy, const std::string& role) {
  bool mac = role == "denied-mac", dac = role == "denied-dac";
  auto root = std::filesystem::path(policy.directory).parent_path().string();
  // Equal existing/platform ancestor traversal must be independently observed.
  for (const auto& path : {std::string("/"), std::string("/opt"),
                           std::string("/opt/usr"), root, root + "/generation"})
    Check(access(path.c_str(), X_OK) == 0, "ancestor traversal setup");
  if (dac) {
    DeniedOpen(policy.lock_path, O_RDONLY);
    Send({{"stage", "probe"},
          {"lock", "denied-DAC"},
          {"later_stages", "NOT_RUN"}});
    return;
  }

  int fd = open(policy.lock_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  Check(fd >= 0, "lock read setup");
  struct flock shared{};
  shared.l_type = F_RDLCK;
  shared.l_whence = SEEK_SET;
  int acquired = fcntl(fd, F_OFD_SETLK, &shared);
  struct flock exclusive = shared;
  exclusive.l_type = F_WRLCK;
  errno = 0;
  int upgraded = fcntl(fd, F_OFD_SETLK, &exclusive);
  int error = errno;
  close(fd);  // close only, never an explicit unlock or conversion of SH
  Check(acquired == 0 && upgraded == -1 && error == EBADF,
        "reader lock authority setup");
  DeniedOpen(policy.lock_path, O_RDWR);
  DeniedOpen(root + "/generation/forbidden", O_WRONLY | O_CREAT | O_EXCL);
  if (mac) {
    DeniedOpen(policy.directory, O_RDONLY | O_DIRECTORY);
    DeniedOpen(policy.directory + "/catalog.db", O_RDONLY);
    Send({{"stage", "probe"},
          {"lock", "shared-acquired"},
          {"directory", "denied-MAC"},
          {"direct_file", "denied-MAC"},
          {"SQLite", "NOT_RUN"}});
    return;
  }

  fd = open(policy.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  Check(fd >= 0, "allowed directory baseline");
  close(fd);
  for (const auto& suffix : {"", "-wal", "-shm"}) {
    auto path = policy.directory + "/catalog.db" + suffix;
    fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    Check(fd >= 0, "allowed file baseline");
    close(fd);  // no SQLite connection has been opened in THIS process yet
    DeniedOpen(path, O_RDWR);
  }

  DeniedOpen(policy.directory + "/forbidden", O_WRONLY | O_CREAT | O_EXCL);
  DeniedOpen(policy.directory + "/mac-write-probe", O_WRONLY);
  DeniedOpen(policy.directory + "/mac-directory/forbidden",
             O_WRONLY | O_CREAT | O_EXCL);
  Send({{"stage", "probe"},
        {"lock", "shared-acquired"},
        {"directory", "read-baseline"},
        {"direct_files", "read-baseline"},
        {"MAC_write", "denied"}});
}

class MetadataObservation final : public ReadLeaseOperations {
 public:
  explicit MetadataObservation(ReadLeasePolicy policy)
      : policy_(std::move(policy)) {}
  std::string Label(int fd) override {
    // Observe the real default metadata operation; never substitute a label.
    auto actual = ReadLeaseOperations::Label(fd);
    struct stat info{};
    Check(fstat(fd, &info) == 0, "metadata observation FD");
    if (actual == policy_.lock_label) lock_seen = true;
    if (actual == policy_.directory_label && S_ISDIR(info.st_mode))
      directory_seen = true;
    if (actual == policy_.file_label && S_ISREG(info.st_mode)) ++data_calls;
    return actual;
  }
  bool lock_seen = false, directory_seen = false;
  size_t data_calls = 0;

 private:
  ReadLeasePolicy policy_;
};

class AdmissionObservation final : public AccessGate {
 public:
  explicit AdmissionObservation(LeasedCatalogGate& gate) : gate_(gate) {}
  std::string AuthorizeAndGetDatabase() override {
    return gate_.AuthorizeAndGetDatabase();  // forbidden path-only fallback
  }

  std::unique_ptr<ReadAccess> AuthorizeReadAccess() override {
    auto access = gate_.AuthorizeReadAccess();
    returned = true;
    return access;
  }
  bool returned = false;

 private:
  LeasedCatalogGate& gate_;
};

void Reader(const Json& config) {
  Check(config.is_object() && config.size() == 2 && config.contains("key") &&
            config.contains("role"),
        "reader config fields");
  auto key = config.at("key").get<std::string>();
  auto role = config.at("role").get<std::string>();
  Check(role == "allowed" || role == "denied-mac" || role == "denied-dac",
        "fixed reader role");
  auto policy = Policy(key);
  Drop("CapMgrReadPolicy::" + key +
           "::" + (role == "denied-mac" ? "Denied" : "Allowed"),
       role != "denied-dac");
  Send({{"stage", "context"},
        {"role", role},
        {"uid", 301},
        {"gid", 301},
        {"platform_group", role != "denied-dac"},
        {"caps", "all-zero"},
        {"NNP", 1},
        {"label", TaskLabel()}});
  capmgr_client_h client = nullptr;
  Channel channel;
  MetadataObservation observation(policy);
  try {
    for (int commands = 0; commands != 20; ++commands) {
      auto command = Receive();
      Check(command.is_object() && command.contains("command"),
            "reader command");
      auto name = command.at("command").get<std::string>();
      if (name == "probe") {
        Check(!client && !channel.authorize, "probe before SQLite only");
        Probe(policy, role);
      } else if (name == "create") {
        Check(!client && !channel.authorize && command.size() == 2,
              "single create only");
        channel.descriptor = command.at("descriptor").get<std::string>();
        LeasedCatalogGate gate(policy, channel, &observation);
        AdmissionObservation admission(gate);
        int code = CreateClient(admission, &client);
        Check(role == "allowed"
                  ? code == CAPMGR_OK && client
                  : code == CAPMGR_ERROR_PERMISSION_DENIED && !client,
              "C admission mismatch");
        Check(role != "denied-mac" || observation.lock_seen,
              "MAC role failed lock metadata setup before catalog veto");
        Check(admission.returned == (role == "allowed"),
              "unexpected lease admission phase");
        if (client)
          Check(observation.lock_seen && observation.directory_seen &&
                    observation.data_calls >= 3,
                "real admitted metadata was not observed");
        Check(channel.authorize == 1 && channel.confirm == (client ? 1 : 0) &&
                  channel.finish == (client ? 1 : 0),
              "C modeled counters");
        Send({{"stage", "create"},
              {"code", code},
              {"handle", !!client},
              {"Authorize", channel.authorize},
              {"Confirm", channel.confirm},
              {"Finish", channel.finish},
              {"lease_admission_returned", admission.returned},
              {"SQLite",
               admission.returned ? "opened-and-validated" : "NOT_RUN"},
              {"transport", "explicitly modeled"}});
      } else if (name == "query") {
        Check(client && command.size() == 1, "live reader required");
        char* bytes = nullptr;
        int code = capmgr_client_get_capability(client, "cli:fixture", &bytes);
        Check(code == CAPMGR_OK && bytes, "public local query");
        std::string detail(bytes);
        free(bytes);
        Send({{"stage", "query"},
              {"detail", Json::parse(detail)},
              {"query_IPC", false}});
      } else if (name == "destroy") {
        Check(client && command.size() == 1, "live destroy required");
        int code = capmgr_client_destroy(client);
        Check(code == CAPMGR_OK, "public destroy did not physically close");
        client = nullptr;
        Send({{"stage", "destroy"}, {"code", code}});
      } else if (name == "exit") {
        Check(!client && command.size() == 1, "no live client on exit");
        Send({{"stage", "exit"}});
        return;
      } else {
        throw std::runtime_error("unsupported fixed reader command");
      }
    }
    throw std::runtime_error("reader command count limit");
  } catch (...) {
    // A failed physical close never loses the owned handle/lease. Exit releases
    // the entire process and recovery SH; parent still verifies that actual exit.
    if (client && capmgr_client_destroy(client) != CAPMGR_OK) _exit(4);
    throw;
  }
}
}  // namespace

namespace {

Entry FixtureEntry(int generation) {
  Entry entry;
  entry.id = "cli:fixture";
  entry.key = entry.name = "fixture";
  entry.owner = "fixture-pkg";
  entry.kind = Kind::kCli;
  entry.desc = "generation" + std::to_string(generation);
  entry.executable = "/usr/bin/true";
  entry.detail = Json::object();
  return entry;
}

void Writer(const Json& config) {
  Check(
      config.is_object() && config.size() == 2 && config.at("role") == "writer",
      "fixed writer config");
  Check(getuid() == 0 && geteuid() == 0, "root fixture writer only");
  auto policy = Policy(config.at("key").get<std::string>());
  std::unique_ptr<CoordinatedCatalogWriter> writer;
  std::array<std::unique_ptr<CatalogReadLease>, 4> grants;
  Send({{"stage", "writer-context"},
        {"label", TaskLabel()},
        {"scope", "root-writer-only"}});
  for (int commands = 0; commands != 40; ++commands) {
    auto command = Receive();
    Check(command.is_object() && command.contains("command"), "writer command");
    auto name = command.at("command").get<std::string>();
    if (name == "bootstrap" || name == "open") {
      Check(!writer && command.size() == 1, "one writer owner");
      writer = std::make_unique<CoordinatedCatalogWriter>(
          policy, name == "bootstrap"
                      ? CatalogGenerationLease::Mode::kMaintenance
                      : CatalogGenerationLease::Mode::kExisting);
      if (name == "bootstrap") {
        writer->Stage("fixture-op1", "fixture-pkg", {FixtureEntry(1)});
        writer->Finalize("fixture-op1",
                         true);  // explicit harness, no installer
        writer->Close();
        writer.reset();
      }
      Send({{"stage", name}});
    } else if (name == "commit") {
      Check(writer && command.size() == 1, "existing writer needed");
      writer->Stage("fixture-op2", "fixture-pkg", {FixtureEntry(2)});
      writer->Finalize("fixture-op2", true);
      Send({{"stage", "commit"}, {"revision", writer->Revision()}});
    } else if (name == "close") {
      Check(writer && command.size() == 1, "writer close needed");
      writer->Close();
      writer.reset();
      Send({{"stage", "close"}});
    } else if (name == "issue") {
      Check(command.size() == 2 && command.at("slot").is_number_unsigned(),
            "fixed grant index");
      auto slot = command.at("slot").get<size_t>();
      Check(slot < grants.size() && !grants[slot], "grant slot occupied");
      grants[slot] = std::make_unique<CatalogReadLease>(policy);
      Send({{"stage", "issue"},
            {"slot", slot},
            {"descriptor", grants[slot]->Descriptor()},
            {"transport", "modeled identity; no nonce or TIDL"}});
    } else if (name == "release") {
      Check(command.size() == 2 && command.at("slot").is_number_unsigned(),
            "fixed release index");
      auto slot = command.at("slot").get<size_t>();
      Check(slot < grants.size() && grants[slot], "grant absent");
      grants[slot]->Check();
      grants[slot].reset();
      Send({{"stage", "release"}, {"slot", slot}});
    } else if (name == "exclusive") {
      Check(command.size() == 1 && !writer,
            "exclusive measured without writer");
      int result = CAPMGR_OK;
      try {
        auto lease = CatalogGenerationLease::Acquire(
            policy, CatalogGenerationLease::Mode::kMaintenance,
            std::chrono::milliseconds(20));
        lease->Check();
      } catch (const Error& error) {
        result = static_cast<int>(error.code());
      }
      Check(result == CAPMGR_OK || result == CAPMGR_ERROR_BUSY,
            "exclusive setup error");
      Send({{"stage", "exclusive"}, {"code", result}});
    } else if (name == "hold-reference") {
      Check(command.size() == 1 && !writer, "hold without writer only");
      for (const auto& grant : grants)
        Check(!grant, "hold without issuer lease");
      // Only a private recovery-lifetime experiment. No command is read again:
      // coordinator loss/HUP cannot close FD5 before this fixed interval ends.
      // The interval begins before acknowledgement, so backpressure consumes it.
      auto end = Clock::now() + std::chrono::seconds(20);
      Send({{"stage", "hold-reference"},
            {"seconds", 20},
            {"authority", "reference lifetime only"}});
      for (;;) {
        auto now = Clock::now();
        if (now >= end) break;
        // One sample proves a positive bounded timeout. A second sample could
        // cross end and produce negative poll timeout (an infinite wait).
        auto remaining =
            std::chrono::ceil<std::chrono::milliseconds>(end - now).count();
        Check(remaining > 0 && remaining <= 20000, "hold positive timeout");
        int result = poll(nullptr, 0, static_cast<int>(remaining));
        Check(result == 0 || (result < 0 && errno == EINTR), "hold timed wait");
      }
      return;  // FD5 is retained until main/kernel process exit, never LOCK_UN.
    } else if (name == "exit") {
      Check(command.size() == 1 && !writer, "no writer on exit");
      for (const auto& grant : grants) Check(!grant, "no issuer lease on exit");
      Send({{"stage", "exit"}});
      return;
    } else {
      throw std::runtime_error("unsupported fixed writer command");
    }
  }

  throw std::runtime_error("writer command count limit");
}
}  // namespace

int main(int argc, char** argv) {
  (void)argv;
  bool table_valid = false;
  try {
    Check(argc == 1, "no runtime image arguments");
    Check(signal(SIGPIPE, SIG_IGN) != SIG_ERR, "ignore control SIGPIPE");
    Table();
    table_valid = true;
    auto config = Receive();
    if (config.value("role", std::string()) == "writer")
      Writer(config);
    else
      Reader(config);
    // Kernel close of the recovery reference at process exit, never LOCK_UN.
    return 0;
  } catch (const std::exception& error) {
    // Before the complete table check, FD4 could be an unexpected regular file
    // or an aliased endpoint. Do not write even diagnostics to that descriptor.
    if (table_valid) {
      try {
        Send({{"stage", "FAIL"}, {"cause", error.what()}});
      } catch (...) {
      }
    }
    return 1;
  }
}
