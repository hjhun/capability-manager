// SPDX-License-Identifier: Apache-2.0
#include "catalog/catalog.hh"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
using namespace capmgr;
namespace {
size_t Number(const char* text,size_t maximum) {
  std::string_view s(text);size_t value=0;
  auto result=std::from_chars(s.data(),s.data()+s.size(),value);
  if(result.ec!=std::errc{} || result.ptr!=s.data()+s.size() || value==0 || value>maximum)
    throw Error(ErrorCode::kInvalid,"Invalid benchmark size");
  return value;
}
Json Distribution(std::vector<double> samples) {
  std::sort(samples.begin(),samples.end());
  return {{"median_us",samples[samples.size()/2]},
          {"p95_us",samples[(samples.size()-1)*95/100]},
          {"min_us",samples.front()},{"max_us",samples.back()}};
}
}
int main(int argc,char** argv) {
  std::string root;
  try {
    size_t rows=1000,iterations=100;
    for(int i=1;i<argc;i+=2) {
      if(i+1==argc)throw Error(ErrorCode::kInvalid,"Expected option value");
      std::string option=argv[i];
      if(option=="--rows")rows=Number(argv[i+1],100000);
      else if(option=="--iterations")iterations=Number(argv[i+1],10000);
      else throw Error(ErrorCode::kInvalid,"Unknown benchmark option");
    }
    char pattern[]="/tmp/capmgr-bench-XXXXXX";
    char* created=mkdtemp(pattern);if(!created)throw Error(ErrorCode::kIo,"Cannot create benchmark scope");
    root=created;const auto path=root+"/catalog.db";
    {
      Catalog writer(path,Database::Access::kWriter);std::vector<Entry> entries;entries.reserve(rows);
      for(size_t i=0;i<rows;++i) {
        Entry e;e.key="photo-"+std::to_string(i);e.id=CanonicalId(Kind::kSkill,e.key);
        e.name="Find photos "+std::to_string(i);e.desc="Search photographs by date and location";
        e.keywords="image picture";e.kind=Kind::kSkill;e.owner="benchmark";e.detail=Json::object();
        entries.push_back(std::move(e));
      }
      writer.Stage("seed","benchmark",entries);writer.Finalize("seed",true);
      writer.database().Exec("PRAGMA wal_checkpoint(TRUNCATE)");
    }
    std::vector<double> warm,reopen;
    using Clock=std::chrono::steady_clock;
    {
      Catalog reader(path,Database::Access::kReadOnly);
      for(size_t i=0;i<iterations;++i) {
        auto start=Clock::now();auto found=reader.Search("photographs date");
        warm.push_back(std::chrono::duration<double,std::micro>(Clock::now()-start).count());
        if(found.size()!=std::min(rows,size_t{5}))throw Error(ErrorCode::kDatabase,"Invalid benchmark result count");
      }
    }
    for(size_t i=0;i<iterations;++i) {
      auto start=Clock::now();
      {Catalog reader(path,Database::Access::kReadOnly);(void)reader.Search("photographs date");}
      reopen.push_back(std::chrono::duration<double,std::micro>(Clock::now()-start).count());
    }
    struct rusage usage{};if(getrusage(RUSAGE_SELF,&usage))throw Error(ErrorCode::kIo,"RSS measurement failed");
    Json output={{"status","PASS"},{"coverage","isolated SQLite fixture; no platform IPC or mounts"},
      {"rows",rows},{"iterations",iterations},{"warm",Distribution(warm)},
      {"reopen",Distribution(reopen)},{"reopen_caveat","OS caches retained; not cold storage latency"},
      {"peak_rss_kib",usage.ru_maxrss},{"database_bytes",std::filesystem::file_size(path)},
      {"pointer_bits",sizeof(void*)*8}};
    std::filesystem::remove_all(root);root.clear();output["cleanup"]="PASS";
    std::cout<<output.dump(2)<<'\n';return 0;
  } catch(const std::exception& error) {
    if(!root.empty()){std::error_code ec;std::filesystem::remove_all(root,ec);}
    std::cerr<<error.what()<<'\n';return 1;
  }
}
