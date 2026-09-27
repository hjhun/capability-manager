// SPDX-License-Identifier: Apache-2.0
#include "launcher/worker_spawn.hh"
#include "common/error.hh"
#include <gtest/gtest.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
struct Pipe {
  int fds[2]{-1,-1};Pipe(){if(pipe2(fds,O_CLOEXEC))throw std::runtime_error("pipe");}
  ~Pipe(){for(int fd:fds)if(fd>=0)close(fd);}
};
struct Fixture {
  Pipe command,cancel,reply;int parent=open("/proc/self",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  int catalog=open("/tmp",O_RDONLY|O_DIRECTORY|O_CLOEXEC);OwnedChildren children{1};struct sigaction saved{};
  Fixture(){sigaction(SIGCHLD,nullptr,&saved);struct sigaction a{};a.sa_handler=SIG_DFL;sigaction(SIGCHLD,&a,nullptr);}
  ~Fixture(){close(parent);close(catalog);sigaction(SIGCHLD,&saved,nullptr);}
  WorkerInheritedFds Fds(){return {command.fds[0],cancel.fds[0],reply.fds[1],parent,catalog};}
  void Mode(char mode){ASSERT_EQ(write(command.fds[1],&mode,1),1);}
  ChildStatus Wait(uint64_t token) {
    ChildStatus status;for(int i=0;i<2000;++i){status=children.Inspect(token);if(status.state==ChildState::Complete)return status;usleep(1000);}
    status=children.StopAndWait(token,2s);ADD_FAILURE()<<"worker fixture exceeded normal-exit budget";return status;
  }
};
}
TEST(WorkerSpawn, FixedImageGetsOnlyMappedDescriptorsAndFixedEnvironment) {
  Fixture f;int leak=open("/dev/null",O_RDONLY);ASSERT_GE(leak,0);
  int high=fcntl(leak,F_DUPFD,300);ASSERT_GE(high,300);int sockets[2];ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  setenv("CAPMGR_TEST_LEAK","must-not-inherit",1);sigset_t block,prior;sigemptyset(&block);sigaddset(&block,SIGTERM);sigprocmask(SIG_BLOCK,&block,&prior);
  f.Mode('N');auto token=SpawnFixedWorker(f.children,UINT64_MAX,f.Fds());sigprocmask(SIG_SETMASK,&prior,nullptr);unsetenv("CAPMGR_TEST_LEAK");
  auto status=f.Wait(token);EXPECT_EQ(status.state,ChildState::Complete);EXPECT_EQ(status.exit_code,0);f.children.Release(token);
  uint64_t report[2]{};EXPECT_EQ(read(f.reply.fds[0],report,sizeof(report)),static_cast<ssize_t>(sizeof(report)));EXPECT_EQ(report[0],UINT64_MAX);EXPECT_GT(report[1],0u);
  close(leak);close(high);close(sockets[0]);close(sockets[1]);
}
TEST(WorkerSpawn, PositiveSpawnIsOwnedImmediatelyAndCleanupIsObserved) {
  Fixture f;f.Mode('L');auto token=SpawnFixedWorker(f.children,1,f.Fds());EXPECT_EQ(f.children.Size(),1u);
  // Stop may precede fixture main; direct child is already owned, never Adopted.
  auto status=f.children.StopAndWait(token,2s);EXPECT_EQ(status.state,ChildState::Complete);EXPECT_EQ(status.signal,SIGKILL);
  f.children.Release(token);EXPECT_EQ(f.children.Size(),0u);
}
TEST(WorkerSpawn, NonzeroWorkerExitIsNotSuccessfulExitProof) {
  Fixture f;f.Mode('E');auto token=SpawnFixedWorker(f.children,2,f.Fds());auto status=f.Wait(token);
  EXPECT_EQ(status.state,ChildState::Complete);EXPECT_EQ(status.exit_code,7);f.children.Release(token);
}
TEST(WorkerSpawn, InvalidGenerationDirectionAndDirectoriesCreateNoChild) {
  Fixture f;EXPECT_THROW(SpawnFixedWorker(f.children,0,f.Fds()),Error);
  auto input=f.Fds();input.reply_write=f.reply.fds[0];EXPECT_THROW(SpawnFixedWorker(f.children,1,input),Error);
  input=f.Fds();input.cancel_read=input.command_read;EXPECT_THROW(SpawnFixedWorker(f.children,1,input),Error);
  input=f.Fds();input.catalog_directory=f.command.fds[0];EXPECT_THROW(SpawnFixedWorker(f.children,1,input),Error);
  EXPECT_EQ(f.children.Size(),0u);
}
TEST(WorkerSpawn, ClosedStdioDoesNotCollideWithSourcesOrFixedTargets) {
  pid_t child=fork();ASSERT_GE(child,0);
  if(!child) {
    close(0);close(1);close(2);
    try {Fixture f;f.Mode('N');auto token=SpawnFixedWorker(f.children,7,f.Fds());auto status=f.Wait(token);f.children.Release(token);_exit(status.exit_code==0?0:71);}
    catch(...){_exit(72);}
  }
  int status=0;ASSERT_EQ(waitpid(child,&status,0),child);ASSERT_TRUE(WIFEXITED(status));EXPECT_EQ(WEXITSTATUS(status),0);
}

TEST(WorkerSpawn, Exit127RequiresFailureHandlingDespitePositiveSpawn) {
  Fixture f;f.Mode('X');auto token=SpawnFixedWorker(f.children,2,f.Fds());auto status=f.Wait(token);
  EXPECT_EQ(status.state,ChildState::Complete);EXPECT_EQ(status.exit_code,127);f.children.Release(token);
}
