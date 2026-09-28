// SPDX-License-Identifier: Apache-2.0
#include "platform/authorization.hh"
#include "common/error.hh"
#include <gtest/gtest.h>
#include <cynara-creds-socket.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {
enum class Failure { kNone, kPid, kUid, kGid, kSmack, kUser, kClient };
thread_local Failure failure = Failure::kNone;
std::atomic<bool> track{false};
std::atomic<int> active{0}, peak{0};
struct Call {
  bool tracked = track.load();
  Call() {
    if (!tracked) return;
    int now = active.fetch_add(1) + 1;
    int old = peak.load();
    while (old < now && !peak.compare_exchange_weak(old, now)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ~Call() {
    if (tracked) active.fetch_sub(1);
  }
};
struct Inject {
  explicit Inject(Failure value) { failure = value; }
  ~Inject() { failure = Failure::kNone; }
};
struct Pair {
  int fd[2]{-1, -1};
  Pair() {
    // The target 4.4 Smack image labels accepted Unix connections; a socketpair
    // does not provide the same SO_PEERSEC surface. No filesystem object needed.
    static std::atomic<unsigned> sequence{0};
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    auto name = "capmgr-cynara-" + std::to_string(getpid()) + "-" +
                std::to_string(sequence.fetch_add(1));
    std::memcpy(address.sun_path + 1, name.data(), name.size());
    socklen_t length = offsetof(sockaddr_un, sun_path) + 1 + name.size();
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    fd[1] = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener >= 0 && fd[1] >= 0 &&
        bind(listener, reinterpret_cast<sockaddr*>(&address), length) == 0 &&
        listen(listener, 1) == 0 &&
        connect(fd[1], reinterpret_cast<sockaddr*>(&address), length) == 0)
      fd[0] = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (listener >= 0) close(listener);
    if (fd[0] < 0) {
      if (fd[1] >= 0) close(fd[1]);
      throw std::runtime_error("Unix test connection");
    }
  }
  ~Pair() {
    for (int value : fd)
      if (value >= 0) close(value);
  }
};
}
extern "C" int __real_cynara_creds_socket_get_pid(int, pid_t*);
extern "C" int __real_cynara_creds_socket_get_user(int, cynara_user_creds,
                                                   char**);
extern "C" int __real_cynara_creds_socket_get_client(int, cynara_client_creds,
                                                     char**);
extern "C" int __wrap_cynara_creds_socket_get_pid(int fd, pid_t* pid) {
  Call call;
  if (failure == Failure::kPid) return CYNARA_API_UNKNOWN_ERROR;
  return __real_cynara_creds_socket_get_pid(fd, pid);
}
extern "C" int __wrap_cynara_creds_socket_get_user(int fd,
                                                   cynara_user_creds method,
                                                   char** user) {
  Call call;
  if ((failure == Failure::kUid && method == USER_METHOD_UID) ||
      (failure == Failure::kGid && method == USER_METHOD_GID) ||
      (failure == Failure::kUser && method == USER_METHOD_DEFAULT))
    return CYNARA_API_UNKNOWN_ERROR;
  return __real_cynara_creds_socket_get_user(fd, method, user);
}
extern "C" int __wrap_cynara_creds_socket_get_client(int fd,
                                                     cynara_client_creds method,
                                                     char** client) {
  Call call;
  if ((failure == Failure::kSmack && method == CLIENT_METHOD_SMACK) ||
      (failure == Failure::kClient && method == CLIENT_METHOD_DEFAULT))
    return CYNARA_API_UNKNOWN_ERROR;
  return __real_cynara_creds_socket_get_client(fd, method, client);
}
TEST(CynaraSocket, EachHelperFailureRejectsWithoutKernelFallback) {
  Pair pair;
  for (auto point :
       {Failure::kPid, Failure::kUid, Failure::kGid, Failure::kSmack}) {
    Inject fault(point);
    EXPECT_THROW(capmgr::Peer::FromSocket(pair.fd[0]), capmgr::Error);
  }
  auto peer = capmgr::Peer::FromSocket(pair.fd[0]);
  for (auto point : {Failure::kUser, Failure::kClient}) {
    Inject fault(point);
    EXPECT_THROW(capmgr::RequirePlatformPrivilege(*peer), capmgr::Error);
  }
  EXPECT_TRUE(peer->Connected());
}
TEST(CynaraSocket, ConcurrentInstancesSerializeEveryHelper) {
  Pair pair;
  peak = 0;
  track = true;
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back([&] {
      for (int j = 0; j < 4; ++j) {
        try {
          auto peer = capmgr::Peer::FromSocket(pair.fd[0]);
          // Exercise DEFAULT helpers in authorization without a real policy query.
          Inject fault(Failure::kClient);
          try {
            capmgr::RequirePlatformPrivilege(*peer);
            ++failures;
          } catch (const capmgr::Error&) {
          }
        } catch (...) {
          ++failures;
        }
      }
    });
  for (auto& thread : threads) thread.join();
  track = false;
  EXPECT_EQ(failures, 0);
  EXPECT_EQ(active, 0);
  EXPECT_EQ(peak, 1);
}
TEST(CynaraSocket, ExplicitIdentityAndDefaultPolicyMappingsAreDistinct) {
  Pair pair;
  auto peer = capmgr::Peer::FromSocket(pair.fd[0]);
  EXPECT_EQ(peer->pid(), getpid());
  EXPECT_EQ(peer->uid(), getuid());
  EXPECT_EQ(peer->gid(), getgid());
  char* user = nullptr;
  ASSERT_EQ(
      cynara_creds_socket_get_user(pair.fd[0], USER_METHOD_DEFAULT, &user),
      CYNARA_API_SUCCESS);
  std::unique_ptr<char, decltype(&std::free)> user_guard(user, &std::free);
  char* client = nullptr;
  ASSERT_EQ(cynara_creds_socket_get_client(pair.fd[0], CLIENT_METHOD_DEFAULT,
                                           &client),
            CYNARA_API_SUCCESS);
  std::unique_ptr<char, decltype(&std::free)> client_guard(client, &std::free);
  ASSERT_NE(user, nullptr);
  ASSERT_NE(client, nullptr);
  std::cout << "SOCKET_RAW_UID=" << peer->uid()
            << " SOCKET_RAW_GID=" << peer->gid()
            << " SOCKET_LABEL=" << peer->security_label()
            << " POLICY_USER=" << user << " POLICY_CLIENT=" << client << '\n';
}
