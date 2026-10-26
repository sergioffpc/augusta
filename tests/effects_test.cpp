#include "augusta/effects.h"

#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"

// Pure: ages and totals are handed in, no clock or ECS.
namespace {

using augusta::math::Vec3;
using augusta::presentation::Age;
using augusta::presentation::Effect;
using augusta::presentation::FiredRounds;
using augusta::presentation::kMuzzleFlashSeconds;

// Requirements: US-19
TEST(AgeTest, AnEffectShowsUntilItsLifetimeIsUp) {
  std::vector<Effect> effects{{.position = Vec3(1.0F, 2.0F, 3.0F), .age = 0.0F}};

  Age(effects, kMuzzleFlashSeconds / 2.0F, kMuzzleFlashSeconds);
  ASSERT_EQ(effects.size(), 1U);
  EXPECT_FLOAT_EQ(effects.front().age, kMuzzleFlashSeconds / 2.0F);

  Age(effects, kMuzzleFlashSeconds / 2.0F, kMuzzleFlashSeconds);
  EXPECT_TRUE(effects.empty());
}

TEST(AgeTest, OnlyTheEffectsWhoseTimeIsUpGo) {
  std::vector<Effect> effects{{.position = Vec3(), .age = 0.9F}, {.position = Vec3(), .age = 0.1F}};

  Age(effects, 0.2F, 1.0F);

  ASSERT_EQ(effects.size(), 1U);
  EXPECT_FLOAT_EQ(effects.front().age, 0.3F);
}

// Requirements: US-18, US-19
TEST(FiredRoundsTest, TheFirstStateSeenFiresNothingNew) {
  FiredRounds fired;

  EXPECT_EQ(fired.Update(7), 0U);
}

// Requirements: US-18, US-19
TEST(FiredRoundsTest, EveryRoundFiredSinceTheLastStateSeenIsNew) {
  FiredRounds fired;
  (void)fired.Update(7);

  EXPECT_EQ(fired.Update(7), 0U);
  EXPECT_EQ(fired.Update(8), 1U);
  // Two rounds between two frames: a frame that saw neither tick still shows both.
  EXPECT_EQ(fired.Update(10), 2U);
}

}  // namespace
