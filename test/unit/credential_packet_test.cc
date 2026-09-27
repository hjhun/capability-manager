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
    CatalogTest::SetUp();listener_=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);ASSERT_GE(listener_,0);
    int one=1;
    ASSERT_EQ(setsockopt(listener_,SOL_SOCKET,SO_PASSCRED,&one,sizeof(one)),0);
    ASSERT_EQ(setsockopt(listener_,SOL_SOCKET,SO_PASSSEC,&one,sizeof(one)),0);
    sockaddr_un address{};address.sun_family=AF_UNIX;auto path=root_+"/packet";
    ASSERT_LT(path.size(),sizeof(address.sun_path));std::memcpy(address.sun_path,path.c_str(),path.size()+1);
    ASSERT_EQ(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address)),0);ASSERT_EQ(listen(listener_,1),0);
    client_=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);ASSERT_GE(client_,0);
    ASSERT_EQ(connect(client_,reinterpret_cast<sockaddr*>(&address),sizeof(address)),0);
    accepted_=accept4(listener_,nullptr,nullptr,SOCK_CLOEXEC);ASSERT_GE(accepted_,0);
    peer_=Peer::FromSocket(accepted_);
  }
  void TearDown() override {
    peer_.reset();for(int fd:{client_,accepted_,listener_})if(fd>=0)close(fd);
    CatalogTest::TearDown();
  }
  void Send(const std::string& data) {ASSERT_EQ(send(client_,data.data(),data.size(),MSG_NOSIGNAL),static_cast<ssize_t>(data.size()));}
  size_t FdCount() {return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});}
  int listener_=-1,client_=-1,accepted_=-1;
  std::shared_ptr<Peer> peer_;
};
TEST_F(PacketTest, ExactPacketAcceptedAndEmptyQueueDoesNotBlock) {
  Send("single packet");EXPECT_EQ(ReceiveCredentialPacket(*peer_),"single packet");
  try {ReceiveCredentialPacket(*peer_);FAIL();}catch(const Error& e){EXPECT_EQ(e.code(),ErrorCode::kBusy);}
}
TEST_F(PacketTest, TruncatedAndEmptyPacketsAreRejectedAndConsumed) {
  Send(std::string(1025,'a'));EXPECT_THROW(ReceiveCredentialPacket(*peer_),Error);
  Send("");EXPECT_THROW(ReceiveCredentialPacket(*peer_),Error);
  Send("next");EXPECT_EQ(ReceiveCredentialPacket(*peer_),"next");
}
TEST_F(PacketTest, InheritedSenderRejectedWhileConnectorStillAlive) {
  pid_t child=fork();ASSERT_GE(child,0);
  if(child==0) {
    if(getuid()==0 && setuid(65534))_exit(2);
    _exit(send(client_,"delegated",9,MSG_NOSIGNAL)==9?0:1);
  }
  int status;ASSERT_EQ(waitpid(child,&status,0),child);ASSERT_TRUE(WIFEXITED(status));ASSERT_EQ(WEXITSTATUS(status),0);
  EXPECT_TRUE(peer_->Alive());EXPECT_THROW(ReceiveCredentialPacket(*peer_),Error);
  Send("original");EXPECT_EQ(ReceiveCredentialPacket(*peer_),"original");
}
TEST_F(PacketTest, UnexpectedRightsAreClosedOnRejection) {
  int file=open("/dev/null",O_RDONLY|O_CLOEXEC);ASSERT_GE(file,0);auto before=FdCount();
  char byte='x';iovec io{&byte,1};alignas(cmsghdr) char control[CMSG_SPACE(sizeof(file))]{};
  msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);
  auto* header=CMSG_FIRSTHDR(&message);header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(sizeof(file));
  std::memcpy(CMSG_DATA(header),&file,sizeof(file));ASSERT_EQ(sendmsg(client_,&message,0),1);
  EXPECT_THROW(ReceiveCredentialPacket(*peer_),Error);EXPECT_EQ(FdCount(),before);close(file);
}
TEST_F(PacketTest, DisabledSecurityAncillaryIsRejected) {
  int zero=0;ASSERT_EQ(setsockopt(accepted_,SOL_SOCKET,SO_PASSSEC,&zero,sizeof(zero)),0);
  Send("no label");EXPECT_THROW(ReceiveCredentialPacket(*peer_),Error);
}
TEST_F(PacketTest, TicketBindsDestinationAndIsConsumedOnce) {
  RemountTickets tickets;auto ticket=tickets.Issue(peer_,"/opt/usr/capmgr-test");
  ASSERT_EQ(ticket.size(),64U);Send(ticket);
  auto request=tickets.Consume(*peer_);EXPECT_EQ(request.destination,"/opt/usr/capmgr-test");
  EXPECT_EQ(request.principal,peer_);Send(ticket);EXPECT_THROW(tickets.Consume(*peer_),Error);
}
TEST_F(PacketTest, TicketsExpireRevokeAndBoundCapacity) {
  RemountTickets::Time now{};RemountTickets tickets([&] {return now;});
  auto expired=tickets.Issue(peer_,"/test");now+=std::chrono::seconds(5);
  Send(expired);EXPECT_THROW(tickets.Consume(*peer_),Error);
  auto revoked=tickets.Issue(peer_,"/test");tickets.Revoke(*peer_);
  Send(revoked);EXPECT_THROW(tickets.Consume(*peer_),Error);
  std::set<std::string> tokens;
  for(int i=0;i<64;++i)tokens.insert(tickets.Issue(peer_,"/test"));
  EXPECT_EQ(tokens.size(),64U);EXPECT_THROW(tickets.Issue(peer_,"/test"),Error);
  now+=std::chrono::seconds(5);EXPECT_NO_THROW(tickets.Issue(peer_,"/test"));
}
TEST_F(PacketTest, TicketRejectsDelegatedSenderWithoutBurningOriginalTicket) {
  RemountTickets tickets;auto ticket=tickets.Issue(peer_,"/test");
  pid_t child=fork();ASSERT_GE(child,0);
  if(child==0)_exit(send(client_,ticket.data(),ticket.size(),MSG_NOSIGNAL)==64?0:1);
  int status;ASSERT_EQ(waitpid(child,&status,0),child);ASSERT_EQ(status,0);
  EXPECT_THROW(tickets.Consume(*peer_),Error);
  Send(ticket);EXPECT_NO_THROW(tickets.Consume(*peer_));
}
TEST_F(PacketTest, TicketCannotAuthorizeAnotherAuthenticatedConnectionPrincipal) {
  RemountTickets tickets;auto ticket=tickets.Issue(peer_,"/test");
  pid_t child=fork();ASSERT_GE(child,0);
  if(child==0) {
    int client=socket(AF_UNIX,SOCK_SEQPACKET,0);sockaddr_un address{};address.sun_family=AF_UNIX;
    auto path=root_+"/packet";std::memcpy(address.sun_path,path.c_str(),path.size()+1);
    if(client<0 || connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address)))_exit(1);
    if(send(client,"probe",5,0)!=5 || send(client,ticket.data(),ticket.size(),0)!=64)_exit(2);
    char ack;_exit(read(client,&ack,1)==1?0:3);
  }
  int accepted=accept4(listener_,nullptr,nullptr,SOCK_CLOEXEC);ASSERT_GE(accepted,0);
  auto other=Peer::FromSocket(accepted);
  EXPECT_EQ(ReceiveCredentialPacket(*other),"probe"); // sender matches its own connection
  EXPECT_THROW(tickets.Consume(*other),Error); // but not the ticket's MAIN principal
  EXPECT_EQ(send(accepted,"x",1,MSG_NOSIGNAL),1);other.reset();close(accepted);
  int status;ASSERT_EQ(waitpid(child,&status,0),child);EXPECT_EQ(status,0);
  Send(ticket);EXPECT_NO_THROW(tickets.Consume(*peer_));
}
TEST_F(PacketTest, TicketRejectsNoncanonicalDestinationsAndMalformedTokens) {
  RemountTickets tickets;
  for(const auto& path:{"/","relative","/x/../y","/x//y","/x/./y","/x/"})
    EXPECT_THROW(tickets.Issue(peer_,path),Error);
  EXPECT_THROW(tickets.Issue(peer_,std::string("/x\0y",4)),Error);
  EXPECT_THROW(tickets.Issue(nullptr,"/test"),Error);
  Send(std::string(64,'g'));EXPECT_THROW(tickets.Consume(*peer_),Error);
}
