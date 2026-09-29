/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../fixtures/read_policy_survivor_hold.hh"

#include <gtest/gtest.h>

namespace capmgr::fixture::realpolicy {
namespace {

TEST(SurvivorHold, ExpiredOrDescheduledTimeNeverBecomesInfinitePoll) {
  const auto end = SurvivorClock::time_point(std::chrono::seconds(20));
  EXPECT_EQ(SurvivorPollMillis(end, end), 0);
  EXPECT_EQ(SurvivorPollMillis(end, end + std::chrono::hours(1)), 0);
}

TEST(SurvivorHold, PositiveRemainderRoundsUpAndStaysBounded) {
  const auto end = SurvivorClock::time_point(std::chrono::seconds(20));
  EXPECT_EQ(SurvivorPollMillis(end, end - std::chrono::nanoseconds(1)), 1);
  EXPECT_EQ(SurvivorPollMillis(end, end - std::chrono::microseconds(1001)), 2);
  EXPECT_EQ(SurvivorPollMillis(end, end - std::chrono::hours(1)), 20000);
}

TEST(SurvivorHold, ExactFixedTopologyRejectsPrefixesDeletedPathsAndOtherPorts) {
  constexpr auto root = "/opt/usr/capmgr-reference-survivor-aZ019z";
  constexpr auto endpoint =
      "d::org.capmgr.referencesurvivor.123.system301-platform";
  EXPECT_TRUE(ReferenceModuleTopology(root, endpoint, 123));
  EXPECT_FALSE(
      ReferenceModuleTopology(std::string(root) + "/extra", endpoint, 123));
  EXPECT_FALSE(
      ReferenceModuleTopology(std::string(root) + " (deleted)", endpoint, 123));
  EXPECT_FALSE(ReferenceModuleTopology(
      "/opt/usr/capmgr-reference-survivor-aZ01/z", endpoint, 123));
  EXPECT_FALSE(ReferenceModuleTopology(root, endpoint, 124));
  EXPECT_FALSE(ReferenceModuleTopology(root, endpoint, 0));
}

TEST(SurvivorHold, StopBeforeDisconnectKeepsDispatchingUntilActualEmptyDrain) {
  EXPECT_EQ(SurvivorDrainDecision(false, true, false, false, false, false),
            SurvivorDrain::kContinue);
  EXPECT_EQ(SurvivorDrainDecision(false, true, true, false, false, false),
            SurvivorDrain::kComplete);
  EXPECT_EQ(SurvivorDrainDecision(false, true, false, false, false, true),
            SurvivorDrain::kFail);
  EXPECT_EQ(SurvivorDrainDecision(false, true, true, false, false, true),
            SurvivorDrain::kFail);
}

TEST(SurvivorHold,
     ObservationErrorOrUnexpectedHoldActivityCannotDrainSuccessfully) {
  EXPECT_EQ(SurvivorDrainDecision(true, false, true, false, false, false),
            SurvivorDrain::kFail);
  EXPECT_EQ(SurvivorDrainDecision(true, false, true, true, true, false),
            SurvivorDrain::kFail);
  EXPECT_EQ(SurvivorDrainDecision(true, false, true, true, false, true),
            SurvivorDrain::kComplete);
}

TEST(SurvivorHold, AlreadyExpiredHoldReturnsWithoutPolling) {
  SurvivorHold(SurvivorClock::now() - std::chrono::seconds(1));
}

}  // namespace
}  // namespace capmgr::fixture::realpolicy
