#include "augusta/body_part.h"

#include <gtest/gtest.h>

// A Body part's name is what logs and the hit-confirmation metric's label read,
// so a dashboard or a log search depends on its spelling.
namespace {

using augusta::body_part::BodyPart;
using augusta::body_part::kBodyParts;
using augusta::body_part::NameOf;

// Requirements: US-11
TEST(BodyPartTest, EveryBodyPartIsListedInOrder) {
  EXPECT_EQ(kBodyParts.size(), 3U);
  EXPECT_EQ(kBodyParts[0], BodyPart::kHead);
  EXPECT_EQ(kBodyParts[1], BodyPart::kTorso);
  EXPECT_EQ(kBodyParts[2], BodyPart::kLimb);
}

// Requirements: US-11, NFR-07
TEST(BodyPartTest, EachBodyPartIsNamedAsLogsAndMetricsSpellIt) {
  EXPECT_EQ(NameOf(BodyPart::kHead), "head");
  EXPECT_EQ(NameOf(BodyPart::kTorso), "torso");
  EXPECT_EQ(NameOf(BodyPart::kLimb), "limb");
}

}  // namespace
