// SPDX-License-Identifier: Apache-2.0
#include "api/dispatcher.hh"
#include <gtest/gtest.h>
#include <future>
#include <unistd.h>
using namespace capmgr;
using namespace std::chrono_literals;
namespace {
struct Blocked {
  std::promise<void> entered, release;
  std::shared_future<void> allowed = release.get_future().share();
  std::atomic<int> calls{0};
  static void Changed(uint64_t, void* data) {
    auto& self = *static_cast<Blocked*>(data);
    if (++self.calls == 1) self.entered.set_value();
    self.allowed.wait();
  }
};
}
TEST(Dispatcher,
     ActiveCallbackRejectsReplaceRemoveAndExternalDestroyWithoutClosing) {
  Dispatcher dispatcher;
  Blocked old;
  ASSERT_TRUE(dispatcher.SetChanged(Blocked::Changed, &old));
  dispatcher.Changed(1);
  ASSERT_EQ(old.entered.get_future().wait_for(2s), std::future_status::ready);
  EXPECT_FALSE(dispatcher.SetChanged(nullptr, nullptr));
  EXPECT_FALSE(dispatcher.SetChanged(Blocked::Changed, nullptr));
  EXPECT_EQ(dispatcher.Close(), Dispatcher::CloseResult::kBusy);
  old.release.set_value();
  for (int i = 0; i < 200 && !dispatcher.SetChanged(nullptr, nullptr); ++i)
    std::this_thread::sleep_for(1ms);
  ASSERT_TRUE(dispatcher.SetChanged(nullptr, nullptr));
  dispatcher.Changed(2);
  EXPECT_EQ(dispatcher.Close(), Dispatcher::CloseResult::kDone);
  EXPECT_EQ(old.calls, 1);
}
TEST(Dispatcher, CallbackCanCancelButCannotDestroyOrReplaceItself) {
  Dispatcher dispatcher;
  struct State {
    Dispatcher* dispatcher;
    std::promise<void> done;
    bool busy = false, cancelled = false;
  } state{&dispatcher, {}};
  auto callback =
      +[](uint64_t token, const char* response, bool event, void* data) {
        auto& state = *static_cast<State*>(data);
        if (event) {
          state.busy =
              (state.dispatcher->Close() == Dispatcher::CloseResult::kBusy) &&
              !state.dispatcher->SetChanged(nullptr, nullptr);
          state.dispatcher->Cancel(token);
        } else {
          state.cancelled =
              nlohmann::json::parse(response)["result"] == "cancelled";
          state.done.set_value();
        }
      };
  dispatcher.Execute(
      [](const auto& cancelled, const auto& emit) {
        emit(R"({"jsonrpc":"2.0","id":"id","event":{"seq":1}})");
        while (!cancelled) std::this_thread::sleep_for(1ms);
        return std::string(
            R"({"jsonrpc":"2.0","id":"id","result":"cancelled"})");
      },
      "id", callback, &state);
  ASSERT_EQ(state.done.get_future().wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(state.busy);
  EXPECT_TRUE(state.cancelled);
  while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
    std::this_thread::yield();
}
TEST(Dispatcher, SuccessfulDestroyCancelsWorkersAndPreventsFutureCallbacks) {
  Dispatcher dispatcher;
  std::atomic<int> calls = 0;
  auto callback = +[](uint64_t, const char*, bool, void* data) {
    ++*static_cast<std::atomic<int>*>(data);
  };
  for (int i = 0; i < 2; ++i)
    dispatcher.Execute(
        [](const auto& cancelled, const auto&) {
          while (!cancelled) std::this_thread::sleep_for(1ms);
          return std::string("finished");
        },
        i, callback, &calls);
  EXPECT_THROW(dispatcher.Execute(
                   [](const auto&, const auto&) { return std::string("x"); }, 9,
                   callback, &calls),
               Error);
  EXPECT_EQ(dispatcher.Close(), Dispatcher::CloseResult::kDone);
  EXPECT_EQ(calls, 0);
}
TEST(Dispatcher, TokenExhaustionDoesNotWrapAndExceptionsHaveOneTerminalReply) {
  Dispatcher dispatcher(UINT64_MAX);
  std::promise<std::string> response;
  auto cb = +[](uint64_t, const char* json, bool event, void* data) {
    if (!event) static_cast<std::promise<std::string>*>(data)->set_value(json);
  };
  auto work = [](const auto&, const auto&) -> std::string {
    throw std::runtime_error("fixture");
  };
  EXPECT_EQ(dispatcher.Execute(work, INT64_MIN, cb, &response), UINT64_MAX);
  EXPECT_THROW(dispatcher.Execute(work, "id", cb, &response), Error);
  auto future = response.get_future();
  ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
  auto json = nlohmann::json::parse(future.get());
  EXPECT_EQ(json["id"], INT64_MIN);
  EXPECT_TRUE(json.contains("error"));
  while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
    std::this_thread::yield();
}

TEST(Dispatcher, GlobalLimitAndDuplicateIdsAreIndependentOfClientTokens) {
  Dispatcher first, second, third;
  std::atomic<int> calls = 0;
  auto callback = +[](uint64_t, const char*, bool, void* data) {
    ++*static_cast<std::atomic<int>*>(data);
  };
  auto work = [](const auto& cancelled, const auto&) {
    while (!cancelled) std::this_thread::sleep_for(1ms);
    return std::string("done");
  };
  first.Execute(work, 0, callback, &calls);
  EXPECT_THROW(first.Execute(work, 0, callback, &calls), Error);
  first.Execute(work, "0", callback, &calls);
  second.Execute(work, 0, callback, &calls);
  second.Execute(work, 1, callback, &calls);
  EXPECT_THROW(third.Execute(work, 0, callback, &calls), Error);
  EXPECT_EQ(first.Close(), Dispatcher::CloseResult::kDone);
  EXPECT_EQ(second.Close(), Dispatcher::CloseResult::kDone);
  EXPECT_NO_THROW(third.Execute(work, 0, callback, &calls));
  EXPECT_EQ(third.Close(), Dispatcher::CloseResult::kDone);
}

TEST(Dispatcher, UnsupportedCancellationIsNeverReportedAsSuccess) {
  Dispatcher dispatcher;
  auto work = [](const auto& stop, const auto&) {
    while (!stop) std::this_thread::sleep_for(1ms);
    return std::string("shutdown");
  };
  auto callback = +[](uint64_t, const char*, bool, void*) {};
  auto token = dispatcher.Execute(work, "id", callback, nullptr, false);
  try {
    dispatcher.Cancel(token);
    FAIL();
  } catch (const Error& error) {
    EXPECT_EQ(error.code(), ErrorCode::kUnsupported);
  }
  EXPECT_EQ(dispatcher.Close(), Dispatcher::CloseResult::kDone);
}

TEST(Dispatcher, MalformedAfterAckAndMissingTerminalRetireWithOneFailure) {
  for (bool malformed : {false, true}) {
    Dispatcher dispatcher;
    struct State {
      std::promise<nlohmann::json> done;
      std::atomic<int> calls{0};
    } state;
    auto callback = +[](uint64_t, const char* text, bool event, void* data) {
      auto& state = *static_cast<State*>(data);
      ++state.calls;
      EXPECT_FALSE(event);
      auto json = nlohmann::json::parse(text);
      if (json.contains("error")) state.done.set_value(json);
    };
    dispatcher.ExecuteFrames(
        [&](const auto&, const Dispatcher::EmitFrame& emit) {
          emit({R"({"jsonrpc":"2.0","id":1,"result":{"subscription":true}})",
                false, false});
          if (malformed) emit({"not-json", true, false});
        },
        1, callback, &state);
    auto future = state.done.get_future();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(future.get()["error"]["data"]["cause"],
              malformed ? "malformed response"
                        : "backend ended without terminal response");
    while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
      std::this_thread::yield();
    EXPECT_EQ(state.calls, 2);
  }
}
TEST(Dispatcher, ClosedEventReleasesCapacityAndDropsLaterFrames) {
  Dispatcher dispatcher;
  struct State {
    std::promise<void> closed;
    std::atomic<int> calls{0};
  } state;
  auto callback = +[](uint64_t, const char* text, bool event, void* data) {
    auto& state = *static_cast<State*>(data);
    ++state.calls;
    if (event && nlohmann::json::parse(text)["event"].contains("closed"))
      state.closed.set_value();
  };
  auto work = [](const auto&, const Dispatcher::EmitFrame& emit) {
    emit({R"({"jsonrpc":"2.0","id":1,"result":{"subscription":true}})", false,
          false});
    emit({R"({"jsonrpc":"2.0","id":1,"event":{"closed":"once"}})", true, true});
    emit({R"({"jsonrpc":"2.0","id":1,"event":{"seq":99}})", true, false});
  };
  dispatcher.ExecuteFrames(work, 1, callback, &state);
  ASSERT_EQ(state.closed.get_future().wait_for(2s), std::future_status::ready);
  // Reusing the completed JSON ID proves the closed event releases its job.
  bool admitted = false;
  for (int i = 0; i < 200 && !admitted; ++i) {
    try {
      dispatcher.ExecuteFrames(
          [](const auto&, const Dispatcher::EmitFrame& emit) {
            emit({R"({"jsonrpc":"2.0","id":1,"result":0})", false, true});
          },
          1, +[](uint64_t, const char*, bool, void*) {}, nullptr);
      admitted = true;
    } catch (const Error&) {
      std::this_thread::sleep_for(1ms);
    }
  }
  EXPECT_TRUE(admitted);
  while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
    std::this_thread::yield();
  EXPECT_EQ(state.calls, 2);
}
TEST(Dispatcher, DestroyAfterAcknowledgementCancelsAndSuppressesLateEvents) {
  Dispatcher dispatcher;
  std::promise<void> ack;
  std::atomic<int> calls{0};
  struct State {
    std::promise<void>* ack;
    std::atomic<int>* calls;
  } state{&ack, &calls};
  dispatcher.ExecuteFrames(
      [](const auto& cancelled, const Dispatcher::EmitFrame& emit) {
        emit({R"({"jsonrpc":"2.0","id":1,"result":{"subscription":true}})",
              false, false});
        while (!cancelled) std::this_thread::sleep_for(1ms);
        emit({R"({"jsonrpc":"2.0","id":1,"event":{"closed":"cancelled"}})",
              true, true});
      },
      1,
      +[](uint64_t, const char*, bool, void* data) {
        auto& state = *static_cast<State*>(data);
        if (++*state.calls == 1) state.ack->set_value();
      },
      &state);
  ASSERT_EQ(ack.get_future().wait_for(2s), std::future_status::ready);
  while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
    std::this_thread::yield();
  EXPECT_EQ(calls, 1);
}

TEST(Dispatcher, QueuedTerminalCannotBeCancelledWhileAnotherCallbackBlocks) {
  Dispatcher dispatcher;
  Blocked blocker;
  dispatcher.SetChanged(Blocked::Changed, &blocker);
  dispatcher.Changed(1);
  ASSERT_EQ(blocker.entered.get_future().wait_for(2s),
            std::future_status::ready);
  std::promise<void> queued;
  auto token = dispatcher.ExecuteFrames(
      [&](const auto&, const Dispatcher::EmitFrame& emit) {
        emit({R"({"jsonrpc":"2.0","id":1,"result":0})", false, true});
        queued.set_value();
      },
      1, +[](uint64_t, const char*, bool, void*) {}, nullptr);
  auto ready = queued.get_future().wait_for(2s);
  EXPECT_EQ(ready, std::future_status::ready);
  if (ready == std::future_status::ready) {
    try {
      dispatcher.Cancel(token);
      ADD_FAILURE() << "Completed token accepted cancellation";
    } catch (const Error& error) {
      EXPECT_EQ(error.code(), ErrorCode::kNotFound);
    }
  }
  blocker.release.set_value();
  while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
    std::this_thread::yield();
}

TEST(Dispatcher, PostTerminalEmitDoesNotWaitBehindSaturatedQueue) {
  ASSERT_EXIT(
      ([] {
        alarm(5);  // Bound regressions even if a dispatch join would deadlock.
        Dispatcher dispatcher;
        Blocked blocker;
        dispatcher.SetChanged(Blocked::Changed, &blocker);
        dispatcher.Changed(1);
        if (blocker.entered.get_future().wait_for(1s) !=
            std::future_status::ready)
          _exit(2);
        std::promise<void> terminal, full, finished;
        auto filled = full.get_future().share();
        dispatcher.ExecuteFrames(
            [&](const auto&, const Dispatcher::EmitFrame& emit) {
              emit({R"({"jsonrpc":"2.0","id":1,"result":0})", false, true});
              terminal.set_value();
              filled.wait();
              emit({nlohmann::json{
                        {"jsonrpc", "2.0"},
                        {"id", 1},
                        {"event", {{"data", std::string(300 * 1024, 'x')}}}}
                        .dump(),
                    true, false});
              finished.set_value();
            },
            1, +[](uint64_t, const char*, bool, void*) {}, nullptr);
        if (terminal.get_future().wait_for(1s) != std::future_status::ready)
          _exit(3);
        dispatcher.ExecuteFrames(
            [&](const auto& cancel, const Dispatcher::EmitFrame& emit) {
              emit({nlohmann::json{
                        {"jsonrpc", "2.0"},
                        {"id", 2},
                        {"event", {{"data", std::string(900 * 1024, 'x')}}}}
                        .dump(),
                    true, false});
              full.set_value();
              while (!cancel) std::this_thread::sleep_for(1ms);
            },
            2, +[](uint64_t, const char*, bool, void*) {}, nullptr);
        if (finished.get_future().wait_for(1s) != std::future_status::ready)
          _exit(4);
        blocker.release.set_value();
        while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
          std::this_thread::yield();
        _exit(0);
      }()),
      testing::ExitedWithCode(0), "");
}

TEST(Dispatcher,
     BoundedCloseRetainsUnfinishedWorkerAndRejectsAdmissionUntilRetry) {
  ASSERT_EXIT(
      ([] {
        alarm(5);
        Dispatcher dispatcher;
        std::promise<void> release, entered;
        auto allowed = release.get_future().share();
        std::atomic<int> calls = 0;
        auto token = dispatcher.Execute(
            [&](const auto&, const auto&) {
              entered.set_value();
              allowed.wait();
              return std::string(R"({"jsonrpc":"2.0","id":1,"result":0})");
            },
            1,
            +[](uint64_t, const char*, bool, void* p) {
              ++*static_cast<std::atomic<int>*>(p);
            },
            &calls);
        if (entered.get_future().wait_for(1s) != std::future_status::ready)
          _exit(2);
        auto before = std::chrono::steady_clock::now();
        if (dispatcher.Close(10ms) != Dispatcher::CloseResult::kIoPending)
          _exit(3);
        if (std::chrono::steady_clock::now() - before > 200ms) _exit(4);
        try {
          dispatcher.CheckAdmission();
          _exit(5);
        } catch (const Error& e) {
          if (e.code() != ErrorCode::kBusy) _exit(6);
        }
        dispatcher.Cancel(token);
        if (dispatcher.Close(10ms) != Dispatcher::CloseResult::kIoPending)
          _exit(7);
        release.set_value();
        if (dispatcher.Close(1s) != Dispatcher::CloseResult::kDone ||
            calls != 0)
          _exit(8);
        _exit(0);
      }()),
      testing::ExitedWithCode(0), "");
}
TEST(Dispatcher, CallbackCompletionDoesNotHideAnUnfinishedWorkerBehindBusy) {
  ASSERT_EXIT(
      ([] {
        alarm(5);
        Dispatcher dispatcher;
        std::promise<void> release, callback;
        auto allowed = release.get_future().share();
        dispatcher.ExecuteFrames(
            [&](const auto&, const auto& emit) {
              emit({R"({"jsonrpc":"2.0","id":1,"result":0})", false, true});
              allowed.wait();
            },
            1,
            +[](uint64_t, const char*, bool, void* p) {
              static_cast<std::promise<void>*>(p)->set_value();
            },
            &callback);
        if (callback.get_future().wait_for(1s) != std::future_status::ready)
          _exit(2);
        Dispatcher::CloseResult result;
        do {
          result = dispatcher.Close(10ms);
        } while (result == Dispatcher::CloseResult::kBusy);
        if (result != Dispatcher::CloseResult::kIoPending) _exit(3);
        release.set_value();
        if (dispatcher.Close(1s) != Dispatcher::CloseResult::kDone) _exit(4);
        _exit(0);
      }()),
      testing::ExitedWithCode(0), "");
}
TEST(Dispatcher, WorkerAndDispatcherThreadLocalDestructionRemainBounded) {
  ASSERT_EXIT(
      ([] {
        alarm(8);
        struct HeldTls {
          std::shared_future<void> released;
          std::promise<void>* entered;
          ~HeldTls() {
            entered->set_value();
            released.wait();
          }
        };
        for (bool callback_thread : {false, true}) {
          Dispatcher dispatcher;
          std::promise<void> release, entered;
          auto allowed = release.get_future().share();
          if (callback_thread) {
            std::promise<void> callback;
            struct State {
              std::shared_future<void> allowed;
              std::promise<void>* entered;
              std::promise<void>* callback;
            } state{allowed, &entered, &callback};
            dispatcher.SetChanged(
                +[](uint64_t, void* p) {
                  auto& state = *static_cast<State*>(p);
                  thread_local std::unique_ptr<HeldTls> held;
                  held.reset(new HeldTls{state.allowed, state.entered});
                  state.callback->set_value();
                },
                &state);
            dispatcher.Changed(1);
            if (callback.get_future().wait_for(1s) != std::future_status::ready)
              _exit(5);
            Dispatcher::CloseResult result;
            do {
              result = dispatcher.Close(10ms);
            } while (result == Dispatcher::CloseResult::kBusy);
            if (result != Dispatcher::CloseResult::kIoPending) _exit(6);
            if (entered.get_future().wait_for(1s) != std::future_status::ready)
              _exit(7);
            release.set_value();
            if (dispatcher.Close(1s) != Dispatcher::CloseResult::kDone)
              _exit(8);
          } else {
            dispatcher.Execute(
                [&](const auto&, const auto&) {
                  thread_local std::unique_ptr<HeldTls> held;
                  held.reset(new HeldTls{allowed, &entered});
                  return std::string(R"({"jsonrpc":"2.0","id":1,"result":0})");
                },
                1, +[](uint64_t, const char*, bool, void*) {}, nullptr);
            if (entered.get_future().wait_for(1s) != std::future_status::ready)
              _exit(2);
            Dispatcher::CloseResult result;
            do {
              result = dispatcher.Close(10ms);
            } while (result == Dispatcher::CloseResult::kBusy);
            if (result != Dispatcher::CloseResult::kIoPending) _exit(3);
            release.set_value();
            if (dispatcher.Close(1s) != Dispatcher::CloseResult::kDone)
              _exit(4);
          }
        }
        _exit(0);
      }()),
      testing::ExitedWithCode(0), "");
}
TEST(Dispatcher, PendingCloseRetainsProcessCapacity) {
  ASSERT_EXIT(([] {
                alarm(5);
                Dispatcher first, second, third;
                std::promise<void> release;
                auto allowed = release.get_future().share();
                auto work = [&](const auto&, const auto&) {
                  allowed.wait();
                  return std::string(R"({"jsonrpc":"2.0","id":1,"result":0})");
                };
                auto callback = +[](uint64_t, const char*, bool, void*) {};
                first.Execute(work, 1, callback, nullptr);
                first.Execute(work, 2, callback, nullptr);
                second.Execute(work, 3, callback, nullptr);
                second.Execute(work, 4, callback, nullptr);
                if (first.Close(1ms) != Dispatcher::CloseResult::kIoPending)
                  _exit(2);
                try {
                  third.Execute(work, 5, callback, nullptr);
                  _exit(3);
                } catch (const Error& e) {
                  if (e.code() != ErrorCode::kLimit) _exit(4);
                }
                release.set_value();
                if (first.Close(1s) != Dispatcher::CloseResult::kDone) _exit(5);
                third.Execute(work, 5, callback, nullptr);
                while (second.Close(1s) != Dispatcher::CloseResult::kDone) {
                }
                while (third.Close(1s) != Dispatcher::CloseResult::kDone) {
                }
                _exit(0);
              }()),
              testing::ExitedWithCode(0), "");
}
TEST(Dispatcher, BlockedResultCallbackKeepsDestroyBusyWithoutClosingAdmission) {
  Dispatcher dispatcher;
  Blocked blocked;
  dispatcher.Execute(
      [](const auto&, const auto&) {
        return std::string(R"({"jsonrpc":"2.0","id":1,"result":0})");
      },
      1, +[](uint64_t, const char*, bool, void* p) { Blocked::Changed(1, p); },
      &blocked);
  ASSERT_EQ(blocked.entered.get_future().wait_for(2s),
            std::future_status::ready);
  auto before = std::chrono::steady_clock::now();
  EXPECT_EQ(dispatcher.Close(), Dispatcher::CloseResult::kBusy);
  EXPECT_LT(std::chrono::steady_clock::now() - before, 50ms);
  EXPECT_NO_THROW(dispatcher.CheckAdmission());
  EXPECT_FALSE(dispatcher.SetChanged(nullptr, nullptr));
  blocked.release.set_value();
  while (dispatcher.Close() != Dispatcher::CloseResult::kDone)
    std::this_thread::yield();
}
