// SPDX-License-Identifier: Apache-2.0
#include "api/dispatcher.hh"
#include <gtest/gtest.h>
#include <future>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
struct Blocked {
  std::promise<void> entered,release;
  std::shared_future<void> allowed=release.get_future().share();
  std::atomic<int> calls{0};
  static void Changed(uint64_t,void* data) {
    auto& self=*static_cast<Blocked*>(data);
    if(++self.calls==1)self.entered.set_value();
    self.allowed.wait();
  }
};
}
TEST(Dispatcher, ActiveCallbackRejectsReplaceRemoveAndExternalDestroyWithoutClosing) {
  Dispatcher dispatcher;Blocked old;
  ASSERT_TRUE(dispatcher.SetChanged(Blocked::Changed,&old));dispatcher.Changed(1);
  ASSERT_EQ(old.entered.get_future().wait_for(2s),std::future_status::ready);
  EXPECT_FALSE(dispatcher.SetChanged(nullptr,nullptr));
  EXPECT_FALSE(dispatcher.SetChanged(Blocked::Changed,nullptr));
  EXPECT_FALSE(dispatcher.Close());old.release.set_value();
  for(int i=0;i<200 && !dispatcher.SetChanged(nullptr,nullptr);++i)std::this_thread::sleep_for(1ms);
  ASSERT_TRUE(dispatcher.SetChanged(nullptr,nullptr));
  dispatcher.Changed(2);EXPECT_TRUE(dispatcher.Close());EXPECT_EQ(old.calls,1);
}
TEST(Dispatcher, CallbackCanCancelButCannotDestroyOrReplaceItself) {
  Dispatcher dispatcher;
  struct State {Dispatcher* dispatcher;std::promise<void> done;bool busy=false,cancelled=false;} state{&dispatcher,{}};
  auto callback=+[](uint64_t token,const char* response,bool event,void* data) {
    auto& state=*static_cast<State*>(data);
    if(event) {
      state.busy=!state.dispatcher->Close() && !state.dispatcher->SetChanged(nullptr,nullptr);
      state.dispatcher->Cancel(token);
    } else {state.cancelled=nlohmann::json::parse(response)["result"]=="cancelled";state.done.set_value();}
  };
  dispatcher.Execute([](const auto& cancelled,const auto& emit) {
    emit(R"({"jsonrpc":"2.0","id":"id","result":"event"})");while(!cancelled)std::this_thread::sleep_for(1ms);
    return std::string(R"({"jsonrpc":"2.0","id":"id","result":"cancelled"})");
  },"id",callback,&state);
  ASSERT_EQ(state.done.get_future().wait_for(2s),std::future_status::ready);
  EXPECT_TRUE(state.busy);EXPECT_TRUE(state.cancelled);
  while(!dispatcher.Close())std::this_thread::yield();
}
TEST(Dispatcher, SuccessfulDestroyCancelsWorkersAndPreventsFutureCallbacks) {
  Dispatcher dispatcher;std::atomic<int> calls=0;
  auto callback=+[](uint64_t,const char*,bool,void* data){++*static_cast<std::atomic<int>*>(data);};
  for(int i=0;i<2;++i)dispatcher.Execute([](const auto& cancelled,const auto&) {
    while(!cancelled)std::this_thread::sleep_for(1ms);
    return std::string("finished");
  },i,callback,&calls);
  EXPECT_THROW(dispatcher.Execute([](const auto&,const auto&){return std::string("x");},9,callback,&calls),Error);
  EXPECT_TRUE(dispatcher.Close());EXPECT_EQ(calls,0);
}
TEST(Dispatcher, TokenExhaustionDoesNotWrapAndExceptionsHaveOneTerminalReply) {
  Dispatcher dispatcher(UINT64_MAX);std::promise<std::string> response;
  auto cb=+[](uint64_t,const char* json,bool event,void* data) {
    if(!event)static_cast<std::promise<std::string>*>(data)->set_value(json);
  };
  auto work=[](const auto&,const auto&)->std::string {throw std::runtime_error("fixture");};
  EXPECT_EQ(dispatcher.Execute(work,INT64_MIN,cb,&response),UINT64_MAX);
  EXPECT_THROW(dispatcher.Execute(work,"id",cb,&response),Error);
  auto future=response.get_future();ASSERT_EQ(future.wait_for(2s),std::future_status::ready);
  auto json=nlohmann::json::parse(future.get());EXPECT_EQ(json["id"],INT64_MIN);EXPECT_TRUE(json.contains("error"));
  while(!dispatcher.Close())std::this_thread::yield();
}

TEST(Dispatcher, GlobalLimitAndDuplicateIdsAreIndependentOfClientTokens) {
  Dispatcher first,second,third;std::atomic<int> calls=0;
  auto callback=+[](uint64_t,const char*,bool,void* data){++*static_cast<std::atomic<int>*>(data);};
  auto work=[](const auto& cancelled,const auto&) {
    while(!cancelled)std::this_thread::sleep_for(1ms);
    return std::string("done");
  };
  first.Execute(work,0,callback,&calls);
  EXPECT_THROW(first.Execute(work,0,callback,&calls),Error);
  first.Execute(work,"0",callback,&calls);
  second.Execute(work,0,callback,&calls);second.Execute(work,1,callback,&calls);
  EXPECT_THROW(third.Execute(work,0,callback,&calls),Error);
  EXPECT_TRUE(first.Close());EXPECT_TRUE(second.Close());
  EXPECT_NO_THROW(third.Execute(work,0,callback,&calls));EXPECT_TRUE(third.Close());
}

TEST(Dispatcher, UnsupportedCancellationIsNeverReportedAsSuccess) {
  Dispatcher dispatcher;
  auto work=[](const auto& stop,const auto&) {
    while(!stop)std::this_thread::sleep_for(1ms);
    return std::string("shutdown");
  };
  auto callback=+[](uint64_t,const char*,bool,void*){};
  auto token=dispatcher.Execute(work,"id",callback,nullptr,false);
  try {dispatcher.Cancel(token);FAIL();}
  catch(const Error& error){EXPECT_EQ(error.code(),ErrorCode::kUnsupported);}
  EXPECT_TRUE(dispatcher.Close());
}
