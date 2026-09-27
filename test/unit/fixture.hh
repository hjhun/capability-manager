// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <gtest/gtest.h>
#include <filesystem>
#include <atomic>
#include <unistd.h>
#include "catalog/catalog.hh"
class CatalogTest : public testing::Test {
 protected:
  void SetUp() override {
    char pattern[]="/tmp/capmgr-test-XXXXXX";
    auto* dir=mkdtemp(pattern); ASSERT_NE(dir,nullptr);
    root_=dir; path_=root_+"/catalog.db";
  }
  void TearDown() override { std::filesystem::remove_all(root_); }
  capmgr::Entry Make(std::string key="search",std::string owner="pkg.one",
                     capmgr::Kind kind=capmgr::Kind::kSkill,std::string app={}) {
    capmgr::Entry e;
    e.key=key; e.id=capmgr::CanonicalId(kind,key,app); e.name=key;
    e.desc="Search pictures and media library"; e.owner=owner; e.kind=kind;
    e.app_id=app; e.detail=capmgr::Json::object(); return e;
  }
  void Publish(capmgr::Catalog& db, const std::string& owner,
               const std::vector<capmgr::Entry>& entries) {
    auto operation="fixture-"+std::to_string(sequence_.fetch_add(1));
    db.Stage(operation,owner,entries); db.Finalize(operation,true);
  }
  std::atomic<uint64_t> sequence_{0};
  std::string root_,path_;
};
