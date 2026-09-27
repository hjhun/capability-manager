// SPDX-License-Identifier: Apache-2.0
#include "catalog/read_lease.hh"
#include <array>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
namespace capmgr {
namespace {
[[noreturn]] void Deny(){throw Error(ErrorCode::kPermission,"Catalog generation lease rejected");}
void Require(bool ok){if(!ok)Deny();}
struct Fd {int value=-1;~Fd(){if(value>=0)close(value);}};
bool Same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev && a.st_ino==b.st_ino;}
void NoAcl(int fd,bool directory=false) {
  for(const char* name:{"system.posix_acl_access","system.posix_acl_default"}) {
    if(!directory && std::string_view(name)=="system.posix_acl_default")continue;
    errno=0;Require(fgetxattr(fd,name,nullptr,0)<0 && (errno==ENODATA || errno==ENOTSUP));
  }
}
bool Normal(const std::string& path) {
  return !path.empty() && path.find('\0')==std::string::npos && path[0]=='/' && path.back()!='/' &&
    std::filesystem::path(path).lexically_normal()==std::filesystem::path(path);
}
ReadLeaseOperations real_operations;
}
std::string ReadLeaseOperations::Label(int fd) {
  std::array<char,256> bytes{};auto size=fgetxattr(fd,"security.SMACK64",bytes.data(),bytes.size());
  Require(size>0 && static_cast<size_t>(size)<bytes.size());
  std::string result(bytes.data(),static_cast<size_t>(size));
  if(result.back()=='\0')result.pop_back();
  Require(!result.empty() && result.find('\0')==std::string::npos);return result;
}
struct CatalogReadLease::Impl {
  ReadLeasePolicy policy;ReadLeaseOperations& operations;
  std::string path;pid_t creator=getpid();bool poisoned=false;
  Fd lock,directory;std::array<Fd,3> files;
  struct stat lock_identity{},directory_identity{};std::array<struct stat,3> identities{};
  static constexpr std::array<const char*,3> names{"catalog.db","catalog.db-wal","catalog.db-shm"};
  Impl(ReadLeasePolicy p,ReadLeaseOperations* o):policy(std::move(p)),operations(o?*o:real_operations),path(policy.directory+"/catalog.db") {
    Require(Normal(policy.directory) && Normal(policy.lock_path) &&
      !policy.lock_path.starts_with(policy.directory+"/") && policy.lock_path!=policy.directory);
    Require((policy.directory_mode==0700 || policy.directory_mode==0750 || policy.directory_mode==02750) &&
      (policy.file_mode==0600 || policy.file_mode==0640) &&
      (policy.lock_mode==0600 || policy.lock_mode==0640) &&
      !policy.directory_label.empty() && !policy.file_label.empty() && !policy.lock_label.empty());
    lock.value=open(policy.lock_path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    Require(lock.value>=0 && !fstat(lock.value,&lock_identity));
    Verify(lock.value,lock_identity,policy.maintainer,policy.lock_group,policy.lock_mode,policy.lock_label,false);
    struct flock lease{};lease.l_type=F_RDLCK;lease.l_whence=SEEK_SET; // start0,len0,pid0
    if(fcntl(lock.value,F_OFD_SETLK,&lease)<0) {
      if(errno==EAGAIN || errno==EACCES)throw Error(ErrorCode::kBusy,"Catalog maintenance owns generation lease");
      Deny();
    }
    directory.value=open(policy.directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    Require(directory.value>=0 && !fstat(directory.value,&directory_identity));
    Verify(directory.value,directory_identity,policy.writer,policy.group,policy.directory_mode,policy.directory_label,true);
    for(size_t i=0;i<names.size();++i) {
      files[i].value=openat(directory.value,names[i],O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
      Require(files[i].value>=0 && !fstat(files[i].value,&identities[i]));
      Verify(files[i].value,identities[i],policy.writer,policy.group,policy.file_mode,policy.file_label,false);
    }
    Check();
  }
  void Verify(int fd,const struct stat& info,uid_t uid,gid_t gid,mode_t mode,const std::string& label,bool dir) {
    Require((dir?S_ISDIR(info.st_mode):S_ISREG(info.st_mode)) && (dir || info.st_nlink==1) &&
      info.st_uid==uid && info.st_gid==gid && (info.st_mode&07777)==mode);
    NoAcl(fd,dir);Require(operations.Label(fd)==label);
  }
  void CheckOne(int fd,const struct stat& identity,const std::string& name,uid_t uid,gid_t gid,mode_t mode,const std::string& label,bool dir=false) {
    struct stat held{},named{};Require(!fstat(fd,&held) && !lstat(name.c_str(),&named) && Same(identity,held) && Same(identity,named));
    Verify(fd,held,uid,gid,mode,label,dir);
    Require((dir?S_ISDIR(named.st_mode):S_ISREG(named.st_mode)) && named.st_uid==uid && named.st_gid==gid && (named.st_mode&07777)==mode);
  }
  void Check() {
    Require(!poisoned && creator==getpid());
    try {
      CheckOne(lock.value,lock_identity,policy.lock_path,policy.maintainer,policy.lock_group,policy.lock_mode,policy.lock_label);
      CheckOne(directory.value,directory_identity,policy.directory,policy.writer,policy.group,policy.directory_mode,policy.directory_label,true);
      for(size_t i=0;i<names.size();++i)CheckOne(files[i].value,identities[i],policy.directory+"/"+names[i],policy.writer,policy.group,policy.file_mode,policy.file_label);
    }catch(...){poisoned=true;throw;}
  }
};
CatalogReadLease::CatalogReadLease(ReadLeasePolicy policy,ReadLeaseOperations* operations):impl_(std::make_unique<Impl>(std::move(policy),operations)){}
CatalogReadLease::~CatalogReadLease()=default;
const std::string& CatalogReadLease::Path() const noexcept{return impl_->path;}
void CatalogReadLease::Check(){impl_->Check();}
std::string CatalogReadLease::Descriptor() {
  Check();
  static_assert(sizeof(dev_t)<=sizeof(uint64_t) && sizeof(ino_t)<=sizeof(uint64_t));
  const std::array<struct stat,5> identities{impl_->lock_identity,impl_->directory_identity,
    impl_->identities[0],impl_->identities[1],impl_->identities[2]};
  std::string result(165,'0');result.replace(0,5,"CMR1:");size_t offset=5;
  constexpr char hex[]="0123456789abcdef";
  for(const auto& info:identities)for(uint64_t value:{static_cast<uint64_t>(info.st_dev),static_cast<uint64_t>(info.st_ino)}) {
    for(unsigned index=0;index<16;++index)result[offset++]=hex[(value>>((15-index)*4))&15];
  }
  return result;
}
void CatalogReadLease::MatchDescriptor(std::string_view descriptor) {
  try {Require(descriptor.size()==165 && descriptor==Descriptor());}
  catch(...){impl_->poisoned=true;throw;}
}
void CatalogReadLease::Opened(Database& db) {
  try {
    Check();Require(sqlite3_db_readonly(db.handle(),"main")==1);
    const char* name=sqlite3_db_filename(db.handle(),"main");Require(name && impl_->path==name);
    Statement mode(db.handle(),"PRAGMA journal_mode");Require(mode.Step() && mode.Text(0)=="wal");
    Check();
  }catch(...){impl_->poisoned=true;throw;}
}
}
