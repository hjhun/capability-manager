// SPDX-License-Identifier: Apache-2.0
#include "../fixtures/read_policy_context.hh"

#include <gtest/gtest.h>

namespace capmgr::fixture::realpolicy {
TEST(ReadPolicyContext, FixedRoutesDoNotAcceptEachOthersRoles) {
  for (const auto& expected : kRoles) {
    const auto role = Role(expected.name);
    EXPECT_FALSE(role.platform_group);
    EXPECT_EQ(role.uid, expected.uid);
    EXPECT_STREQ(role.label, expected.label);
    EXPECT_THROW(Role(expected.name, true), std::runtime_error);
  }
  EXPECT_THROW(Role("system301-platform"), std::runtime_error);
  EXPECT_THROW(Role("arbitrary", true), std::runtime_error);
  EXPECT_THROW(Role("arbitrary"), std::runtime_error);
}
TEST(ReadPolicyContext, NewTupleHasOnlyOneFixedSystemReader) {
  ASSERT_EQ(kPlatformRoles.size(), 1u);
  const auto role = Role("system301-platform", true);
  EXPECT_TRUE(role.platform_group);
  EXPECT_EQ(role.uid, 301u);
  EXPECT_STREQ(role.label, "System");
  EXPECT_EQ(kPlatformGroup, 10212u);
}
TEST(ReadPolicyContext, ExactGroupMembershipRejectsMissingExtraAndWrongGroups) {
  const std::array<gid_t, 1> allowed{kPlatformGroup}, primary{301}, root{0};
  const std::array<gid_t, 2> extra{kPlatformGroup, 0};
  const std::array<gid_t, 2> duplicate{kPlatformGroup, kPlatformGroup};
  EXPECT_TRUE(ExactGroups(false, {}));
  EXPECT_FALSE(ExactGroups(true, {}));
  EXPECT_TRUE(ExactGroups(true, allowed));
  EXPECT_FALSE(ExactGroups(false, allowed));
  EXPECT_FALSE(ExactGroups(true, primary));
  EXPECT_FALSE(ExactGroups(true, root));
  EXPECT_FALSE(ExactGroups(true, extra));
  EXPECT_FALSE(ExactGroups(true, duplicate));
}
TEST(ReadPolicyContext, ImageGroupMappingCannotChooseFallback) {
  EXPECT_NO_THROW(RequirePlatformGroup("priv_platform", 10212));
  EXPECT_THROW(RequirePlatformGroup(nullptr, 10212), std::runtime_error);
  EXPECT_THROW(RequirePlatformGroup("priv_platform", 301), std::runtime_error);
  EXPECT_THROW(RequirePlatformGroup("another_group", 10212),
               std::runtime_error);
}
}  // namespace capmgr::fixture::realpolicy
