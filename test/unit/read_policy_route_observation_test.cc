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

#include "../fixtures/read_policy_route_observation.hh"

#include <gtest/gtest.h>

namespace capmgr::fixture::realpolicy {
namespace {

int calls = 0;
int Denied(const char*, struct stat*) {
  ++calls;
  errno = EACCES;
  return -1;
}
int NamedSocket(const char*, struct stat* info) {
  ++calls;
  *info = {};
  info->st_dev = 2;
  info->st_ino = 3;
  info->st_uid = 4;
  info->st_gid = 5;
  info->st_mode = S_IFSOCK | 0600;
  return 0;
}

TEST(RouteObservation, NullAndErrorPayloadHasNoCompleteText) {
  const auto value = CaptureRouteText(nullptr);
  EXPECT_FALSE(value.complete);
  EXPECT_TRUE(value.bytes.empty());
}

TEST(RouteObservation, ArbitraryBytesAreHexAndNeverRawControls) {
  const char value[] = {'x', '\n', '\x1b', '\xff', 0};
  const auto text = CaptureRouteText(value);
  EXPECT_TRUE(text.complete);
  EXPECT_EQ(text.Report()["hex"], "780a1bff");
  EXPECT_EQ(text.Report()["sample_bytes"], 4);
}

TEST(RouteObservation, OverlongTextIsExplicitlyIncompleteAndBounded) {
  const std::string value(513, 'x');
  const auto text = CaptureRouteText(value.c_str());
  EXPECT_FALSE(text.complete);
  EXPECT_EQ(text.bytes.size(), 512u);
  EXPECT_EQ(text.Report()["hex"].get<std::string>().size(), 1024u);
}

TEST(RouteObservation, MismatchAndSunPathTruncationNeverInspect) {
  calls = 0;
  const auto mismatch =
      RouteMetadata(CaptureRouteText("/other"), "/expected", Denied);
  EXPECT_FALSE(mismatch["attempted"].get<bool>());
  const std::string overlong(108, '/');
  const auto too_long =
      RouteMetadata(CaptureRouteText(overlong.c_str()), overlong, Denied);
  EXPECT_FALSE(too_long["attempted"].get<bool>());
  EXPECT_EQ(calls, 0);
}

TEST(RouteObservation, FailedNamedLstatSavesOnlyThatImmediateErrno) {
  calls = 0;
  const auto value =
      RouteMetadata(CaptureRouteText("/expected"), "/expected", Denied);
  errno = ENOENT;
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(value["attempted"].get<bool>());
  EXPECT_EQ(value["result"], -1);
  EXPECT_EQ(value["errno"], EACCES);
  EXPECT_TRUE(value["ino"].is_null());
}

TEST(RouteObservation,
     NamedMetadataIsReportedWithoutAccessOrIdentityAuthority) {
  calls = 0;
  const auto value =
      RouteMetadata(CaptureRouteText("/expected"), "/expected", NamedSocket);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(value["result"], 0);
  EXPECT_EQ(value["errno"], 0);
  EXPECT_EQ(value["dev"], 2);
  EXPECT_EQ(value["ino"], 3);
  EXPECT_EQ(value["uid"], 4);
  EXPECT_EQ(value["gid"], 5);
  EXPECT_EQ(value["mode"], S_IFSOCK | 0600);
}

}  // namespace
}  // namespace capmgr::fixture::realpolicy
