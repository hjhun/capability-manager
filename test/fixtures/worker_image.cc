// SPDX-License-Identifier: Apache-2.0
// No root setup or workload exec. Independent process for fixed FD/argv checks.
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <cstdint>
int main(int argc,char** argv) {
  if(argc!=3 || std::strcmp(argv[1],"--generation"))return 31;
  uint64_t generation=0;auto end=argv[2]+std::strlen(argv[2]);auto parsed=std::from_chars(argv[2],end,generation);
  if(parsed.ec!=std::errc{} || parsed.ptr!=end || !generation)return 32;
  if(getenv("LD_PRELOAD") || getenv("CAPMGR_TEST_LEAK"))return 33;
  if(!getenv("LANG") || std::strcmp(getenv("LANG"),"C"))return 34;
  sigset_t mask;sigprocmask(SIG_SETMASK,nullptr,&mask);
  for(int signal:{SIGTERM,SIGCHLD,SIGINT,SIGHUP,SIGPIPE})if(sigismember(&mask,signal))return 35;
  for(int i=0;i<3;++i){struct stat actual{},expected{};if(fstat(i,&actual) || stat("/dev/null",&expected) || actual.st_rdev!=expected.st_rdev || !S_ISCHR(actual.st_mode))return 36;}
  for(int i=3;i<8;++i){struct stat st{};if(fstat(i,&st))return 37;
    if(i<=5){if(!S_ISFIFO(st.st_mode) || (fcntl(i,F_GETFL)&O_ACCMODE)!=(i==5?O_WRONLY:O_RDONLY))return 38;}
    else if(!S_ISDIR(st.st_mode))return 39;
    if(fcntl(i,F_GETFD)&FD_CLOEXEC)return 40;
  }
  struct statfs proc{};if(fstatfs(6,&proc) || proc.f_type!=0x9fa0)return 41;
  DIR* directory=opendir("/proc/self/fd");if(!directory)return 42;int own=dirfd(directory);
  while(auto* item=readdir(directory)){char* tail=nullptr;long fd=strtol(item->d_name,&tail,10);if(*tail)continue;if(fd>=8 && fd!=own){closedir(directory);return 43;}}
  closedir(directory);
  char mode=0;if(read(3,&mode,1)!=1)return 44;
  std::array<uint64_t,2> result{generation,static_cast<uint64_t>(getpid())};
  if(write(5,result.data(),sizeof(result))!=static_cast<ssize_t>(sizeof(result)))return 45;
  if(mode=='L')for(;;)pause();
  return mode=='E'?7:mode=='X'?127:0;
}
