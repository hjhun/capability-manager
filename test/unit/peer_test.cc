// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "platform/peer.hh"
#include "platform/authorization.hh"
#include <gmock/gmock.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
using namespace capmgr;
namespace {
struct Fd {
  int value = -1;
  explicit Fd(int fd = -1) : value(fd) {}
  ~Fd() {
    if (value >= 0) close(value);
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
struct LocalConnection {
  Fd listener{socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)}, client, accepted;
  explicit LocalConnection(const std::string& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
      throw std::runtime_error("socket path");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (listener.value < 0 ||
        bind(listener.value, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) ||
        listen(listener.value, 1))
      throw std::runtime_error("listen");
    client.value = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client.value < 0 ||
        connect(client.value, reinterpret_cast<sockaddr*>(&address),
                sizeof(address)))
      throw std::runtime_error("connect");
    accepted.value = accept4(listener.value, nullptr, nullptr, SOCK_CLOEXEC);
    if (accepted.value < 0) throw std::runtime_error("accept");
  }
};
bool SecurityLabelsAvailable(int fd) {
  char label[4096];
  socklen_t size = sizeof(label);
  return getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &size) == 0 &&
         size > 0 && label[0];
}
// Skip only a detected missing OS facility. Implementation exceptions fail tests.
#define REQUIRE_SOCKET_LABEL(fd)                                       \
  if (!SecurityLabelsAvailable(fd)) {                                  \
    ASSERT_EQ(std::getenv("CAPMGR_REQUIRE_PEER_TESTS"), nullptr)       \
        << "Required socket label unavailable";                        \
    GTEST_SKIP() << "OS does not provide a connection security label"; \
  }
class MockPolicy : public ConnectionPolicy {
 public:
  MOCK_METHOD(PolicyDecision, CheckSocket, (int), (override));
};
}
TEST(Peer, RejectsInvalidRegularAndNonUnixDescriptors) {
  EXPECT_THROW(Peer::FromSocket(-1),
               Error);  // also the generated -e default FD
  Fd file(open("/dev/null", O_RDONLY));
  ASSERT_GE(file.value, 0);
  EXPECT_THROW(Peer::FromSocket(file.value), Error);
  Fd network(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_GE(network.value, 0);
  EXPECT_THROW(Peer::FromSocket(network.value), Error);
}
TEST_F(CatalogTest, UnixConnectorPinsNamespaceAndDetectsDisconnect) {
  LocalConnection connection(root_ + "/peer.sock");
  REQUIRE_SOCKET_LABEL(connection.accepted.value);
  auto peer = Peer::FromSocket(connection.accepted.value);
  EXPECT_EQ(peer->pid(), getpid());
  EXPECT_EQ(peer->uid(), getuid());
  EXPECT_EQ(peer->gid(), getgid());
  EXPECT_FALSE(peer->security_label().empty());
  EXPECT_TRUE(peer->Alive());
  struct stat pinned{}, actual{};
  ASSERT_EQ(fstat(peer->namespace_fd(), &pinned), 0);
  ASSERT_EQ(stat("/proc/self/ns/mnt", &actual), 0);
  EXPECT_EQ(pinned.st_ino, actual.st_ino);
  EXPECT_EQ(pinned.st_dev, actual.st_dev);
  close(connection.client.value);
  connection.client.value = -1;
  EXPECT_FALSE(peer->Alive());
  EXPECT_EQ(fstat(peer->namespace_fd(), &pinned), 0);
}
TEST_F(CatalogTest, InheritedEndpointRetainsConnectorIdentityNotCurrentSender) {
  LocalConnection connection(root_ + "/peer.sock");
  REQUIRE_SOCKET_LABEL(connection.accepted.value);
  auto peer = Peer::FromSocket(connection.accepted.value);
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    // On the native root test, demonstrate a different UID can write an inherited
    // endpoint. It must never become a namespace identity through SO_PEERCRED.
    if (getuid() == 0 && setuid(65534)) _exit(91);
    _exit(write(connection.client.value, "x", 1) == 1 ? 0 : 92);
  }
  int status;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
  char byte;
  ASSERT_EQ(read(connection.accepted.value, &byte, 1), 1);
  EXPECT_EQ(peer->pid(), getpid());
  EXPECT_TRUE(peer->Alive());
}
TEST_F(CatalogTest, RightsRecipientDoesNotKeepZombieConnectorAlive) {
  Fd listener(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_GE(listener.value, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  auto path = root_ + "/peer.sock";
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  ASSERT_EQ(bind(listener.value, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)),
            0);
  ASSERT_EQ(listen(listener.value, 1), 0);
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), 0);
  Fd control(pair[0]), child_control(pair[1]);
  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(control.value);
    close(listener.value);
    int endpoint = socket(AF_UNIX, SOCK_STREAM, 0);
    if (endpoint < 0 || connect(endpoint, reinterpret_cast<sockaddr*>(&address),
                                sizeof(address)))
      _exit(93);
    char byte = 'f';
    iovec io{&byte, 1};
    alignas(cmsghdr) char ancillary[CMSG_SPACE(sizeof(int))]{};
    msghdr msg{};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = ancillary;
    msg.msg_controllen = sizeof(ancillary);
    auto* header = CMSG_FIRSTHDR(&msg);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(header), &endpoint, sizeof(endpoint));
    if (sendmsg(child_control.value, &msg, 0) != 1) _exit(94);
    if (read(child_control.value, &byte, 1) != 1) _exit(95);
    _exit(0);
  }
  close(child_control.value);
  child_control.value = -1;
  Fd accepted(accept4(listener.value, nullptr, nullptr, SOCK_CLOEXEC));
  ASSERT_GE(accepted.value, 0);
  char byte;
  iovec io{&byte, 1};
  alignas(cmsghdr) char ancillary[CMSG_SPACE(sizeof(int))]{};
  msghdr msg{};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = ancillary;
  msg.msg_controllen = sizeof(ancillary);
  ASSERT_EQ(recvmsg(control.value, &msg, MSG_CMSG_CLOEXEC), 1);
  auto* header = CMSG_FIRSTHDR(&msg);
  ASSERT_NE(header, nullptr);
  ASSERT_EQ(header->cmsg_type, SCM_RIGHTS);
  int fd;
  std::memcpy(&fd, CMSG_DATA(header), sizeof(fd));
  Fd recipient(fd);
  // Ensure child cleanup even when this OS lacks labels.
  std::shared_ptr<Peer> peer;
  if (SecurityLabelsAvailable(accepted.value))
    peer = Peer::FromSocket(accepted.value);
  if (peer) {
    EXPECT_EQ(peer->pid(), child);
    EXPECT_TRUE(peer->Alive());
  }
  ASSERT_EQ(write(recipient.value, "r", 1), 1);
  ASSERT_EQ(read(accepted.value, &byte, 1), 1);
  ASSERT_EQ(write(control.value, "x", 1), 1);
  siginfo_t info{};
  ASSERT_EQ(waitid(P_PID, child, &info, WEXITED | WNOWAIT), 0);
  EXPECT_EQ(info.si_status, 0);
  if (peer) {
    EXPECT_FALSE(peer->Alive());
  }  // endpoint remains open but connector is Z
  int status;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  if (peer) {
    EXPECT_FALSE(peer->Alive());
  }  // the retained procdir now refers to a reaped task
  REQUIRE_SOCKET_LABEL(accepted.value);
}
TEST_F(CatalogTest, PolicyNeverBypassesSystemUidAndOnlyAllowsConfirmedGrant) {
  LocalConnection connection(root_ + "/peer.sock");
  REQUIRE_SOCKET_LABEL(connection.accepted.value);
  auto peer = Peer::FromSocket(connection.accepted.value);
  testing::StrictMock<MockPolicy> policy;
  EXPECT_CALL(policy, CheckSocket(peer->socket_fd()))
      .WillOnce(testing::Return(PolicyDecision::kAllowed));
  EXPECT_NO_THROW(RequirePlatformPrivilege(*peer, policy));
  for (auto decision : {PolicyDecision::kDenied, PolicyDecision::kUnresolved,
                        PolicyDecision::kUnavailable}) {
    EXPECT_CALL(policy, CheckSocket(peer->socket_fd()))
        .WillOnce(testing::Return(decision));
    EXPECT_THROW(RequirePlatformPrivilege(*peer, policy), Error);
  }
  close(connection.client.value);
  connection.client.value = -1;
  EXPECT_THROW(RequirePlatformPrivilege(*peer, policy),
               Error);  // no policy call on disconnected peer
}
