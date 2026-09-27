// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "platform/tidl_channels.hh"
#include <gmock/gmock.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cstring>
using namespace capmgr;
namespace {
class Policy : public ConnectionPolicy {
 public:
  MOCK_METHOD(PolicyDecision, CheckSocket, (int), (override));
};
struct Connection {
  int listener = -1, client = -1, accepted = -1;
  explicit Connection(const std::string& path) {
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
      throw std::runtime_error("path");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (listener < 0 ||
        bind(listener, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) ||
        listen(listener, 1))
      throw std::runtime_error("listen");
    client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0 ||
        connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
      throw std::runtime_error("connect");
    accepted = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (accepted < 0) throw std::runtime_error("accept");
  }
  ~Connection() {
    for (int fd : {client, accepted, listener})
      if (fd >= 0) close(fd);
  }
};
}
TEST_F(CatalogTest, BindingRejectsDefaultsDuplicateChannelsAndSwappedDispatch) {
  Connection main(root_ + "/main"), callback(root_ + "/callback");
  auto policy = std::make_shared<testing::StrictMock<Policy>>();
  TidlChannels binding(policy);
  EXPECT_FALSE(binding.ValidateChannels(main.accepted, callback.accepted));
  EXPECT_FALSE(binding.BindChannels(-1, callback.accepted));
  EXPECT_FALSE(binding.BindChannels(main.accepted, -1));
  EXPECT_FALSE(binding.BindChannels(main.accepted, main.accepted));
  EXPECT_EQ(binding.MainPrincipal(), nullptr);
  EXPECT_CALL(*policy, CheckSocket(testing::_))
      .WillOnce(testing::Invoke([&](int fd) {
        struct stat actual{}, expected{};
        EXPECT_EQ(fstat(fd, &actual), 0);
        EXPECT_EQ(fstat(main.accepted, &expected), 0);
        EXPECT_EQ(actual.st_ino, expected.st_ino);
        return PolicyDecision::kAllowed;
      }));
  EXPECT_TRUE(binding.BindChannels(main.accepted, callback.accepted));
  ASSERT_NE(binding.MainPrincipal(), nullptr);
  EXPECT_FALSE(binding.BindChannels(main.accepted, callback.accepted));
  EXPECT_FALSE(binding.ValidateChannels(callback.accepted, main.accepted));
  EXPECT_CALL(*policy, CheckSocket(testing::_))
      .WillOnce(testing::Return(PolicyDecision::kAllowed));
  EXPECT_TRUE(binding.ValidateChannels(main.accepted, callback.accepted));
}
TEST_F(CatalogTest, BindingRechecksPolicyAndDetectsCallbackDisconnect) {
  Connection main(root_ + "/main"), callback(root_ + "/callback");
  auto policy = std::make_shared<testing::StrictMock<Policy>>();
  TidlChannels binding(policy);
  EXPECT_CALL(*policy, CheckSocket(testing::_))
      .WillOnce(testing::Return(PolicyDecision::kDenied));
  EXPECT_FALSE(binding.BindChannels(main.accepted, callback.accepted));
  EXPECT_EQ(binding.MainPrincipal(), nullptr);
  EXPECT_CALL(*policy, CheckSocket(testing::_))
      .WillOnce(testing::Return(PolicyDecision::kAllowed));
  EXPECT_TRUE(binding.BindChannels(main.accepted, callback.accepted));
  EXPECT_CALL(*policy, CheckSocket(testing::_))
      .WillOnce(testing::Return(PolicyDecision::kUnresolved));
  EXPECT_FALSE(binding.ValidateChannels(main.accepted, callback.accepted));
  close(callback.client);
  callback.client = -1;
  EXPECT_FALSE(binding.ValidateChannels(main.accepted, callback.accepted));
}
TEST_F(CatalogTest, BindingRejectsCallbackFromAnotherLiveProcess) {
  Connection main(root_ + "/main");
  int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  ASSERT_GE(listener, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  auto path = root_ + "/foreign";
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  ASSERT_EQ(
      bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
      0);
  ASSERT_EQ(listen(listener, 1), 0);
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0 ||
        connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
      _exit(1);
    char byte;
    _exit(read(client, &byte, 1) == 1 ? 0 : 2);
  }
  int accepted = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
  ASSERT_GE(accepted, 0);
  auto callback = Peer::FromSocket(accepted);
  ASSERT_EQ(callback->pid(), child);
  EXPECT_TRUE(callback->Alive());
  auto policy = std::make_shared<testing::StrictMock<Policy>>();
  TidlChannels binding(policy);
  EXPECT_FALSE(binding.BindChannels(main.accepted, accepted));
  EXPECT_EQ(binding.MainPrincipal(), nullptr);
  EXPECT_EQ(write(accepted, "x", 1), 1);
  callback.reset();
  close(accepted);
  close(listener);
  int status;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_EQ(status, 0);
}
