// SPDX-License-Identifier: Apache-2.0
#include "fixture.hh"
#include "platform/credential_packet.hh"
#include "platform/remount_ticket.hh"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <set>
using namespace capmgr;
class PacketTest : public CatalogTest {
 protected:
  void SetUp() override {
    CatalogTest::SetUp();
    listener_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    ASSERT_GE(listener_, 0);
    int one = 1;
    ASSERT_EQ(setsockopt(listener_, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)),
              0);
    ASSERT_EQ(setsockopt(listener_, SOL_SOCKET, SO_PASSSEC, &one, sizeof(one)),
              0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    auto path = root_ + "/packet";
    ASSERT_LT(path.size(), sizeof(address.sun_path));
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    ASSERT_EQ(
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
        0);
    ASSERT_EQ(listen(listener_, 1), 0);
    client_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    ASSERT_GE(client_, 0);
    ASSERT_EQ(connect(client_, reinterpret_cast<sockaddr*>(&address),
                      sizeof(address)),
              0);
    accepted_ = accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
    ASSERT_GE(accepted_, 0);
    peer_ = Peer::FromSocket(accepted_);
  }
  void TearDown() override {
    peer_.reset();
    for (int fd : {client_, accepted_, listener_})
      if (fd >= 0) close(fd);
    CatalogTest::TearDown();
  }
  void Send(const std::string& data) {
    ASSERT_EQ(send(client_, data.data(), data.size(), MSG_NOSIGNAL),
              static_cast<ssize_t>(data.size()));
  }
  int listener_ = -1, client_ = -1, accepted_ = -1;
  std::shared_ptr<Peer> peer_;
};
// The prior positive packet/ticket tests belong to the historical proc-backed
// experiment. No current API provides its live-task proof; do not replay those
// tests under a weaker Connected() guard and call the result equivalent.
TEST_F(PacketTest, PacketRequiresUnavailableTaskProofBeforeReceiving) {
  Send("untouched");
  try {
    ReceiveCredentialPacket(*peer_);
    FAIL();
  } catch (const Error& error) {
    EXPECT_EQ(error.code(), ErrorCode::kUnsupported);
  }
  char byte;
  EXPECT_EQ(recv(accepted_, &byte, 1, MSG_PEEK | MSG_DONTWAIT), 1);
  EXPECT_EQ(byte, 'u');
}
TEST_F(PacketTest, IssueCannotCreateTicketWithoutTaskProof) {
  RemountTickets tickets;
  try {
    tickets.Issue(peer_, "/opt/usr/capmgr-test");
    FAIL();
  } catch (const Error& error) {
    EXPECT_EQ(error.code(), ErrorCode::kUnsupported);
  }
}
TEST_F(PacketTest, ConsumeCannotAcceptTicketWithoutTaskProof) {
  RemountTickets tickets;
  Send(std::string(64, 'a'));
  try {
    tickets.Consume(*peer_);
    FAIL();
  } catch (const Error& error) {
    EXPECT_EQ(error.code(), ErrorCode::kUnsupported);
  }
}
