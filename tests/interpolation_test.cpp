#include "augusta/interpolation.h"

#include <array>

#include <gtest/gtest.h>

// Pure: buffered by timestamp and session, no clock or ECS.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::presentation::RemoteBody;
using augusta::presentation::RemoteInterpolator;
using augusta::presentation::RemotePlayer;

constexpr auto kSessionA = static_cast<augusta::presentation::SessionId>(1);
constexpr auto kSessionB = static_cast<augusta::presentation::SessionId>(2);

BodyState At(float x, Stance stance = Stance::kStanding) {
  return BodyState{.position = Vec3(x, 0.0F, 0.0F), .velocity = Vec3(), .stance = stance};
}

// Asserts interpolator has exactly one buffered session (kSessionA) and returns its body.
RemoteBody Only(const RemoteInterpolator& interpolator, float render_time) {
  const std::vector<RemotePlayer> sampled = interpolator.Sample(render_time);
  EXPECT_EQ(sampled.size(), 1U);
  return sampled.front().body;
}

TEST(RemoteInterpolatorTest, ASessionWithNoUpdatesIsNotSampled) {
  const RemoteInterpolator interpolator;

  EXPECT_TRUE(interpolator.Sample(0.0F).empty());
}

TEST(RemoteInterpolatorTest, ASingleUpdateIsShownAsIs) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 1.0F, At(5.0F, Stance::kCrouching));

  // Before, at, and long after the one update: nothing to interpolate between.
  for (const float render_time : {0.0F, 1.0F, 100.0F}) {
    const RemoteBody body = Only(interpolator, render_time);
    EXPECT_FLOAT_EQ(body.position.x, 5.0F) << render_time;
    EXPECT_EQ(body.stance, Stance::kCrouching) << render_time;
  }
}

TEST(RemoteInterpolatorTest, PositionIsLinearlyInterpolatedBetweenTheTwoSurroundingUpdates) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F));
  interpolator.Record(kSessionA, 1.0F, At(10.0F));

  EXPECT_FLOAT_EQ(Only(interpolator, 0.25F).position.x, 2.5F);
  EXPECT_FLOAT_EQ(Only(interpolator, 0.5F).position.x, 5.0F);
  EXPECT_FLOAT_EQ(Only(interpolator, 0.75F).position.x, 7.5F);
}

TEST(RemoteInterpolatorTest, StanceSwitchesAtTheMidpointBetweenTheTwoUpdates) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F, Stance::kStanding));
  interpolator.Record(kSessionA, 1.0F, At(10.0F, Stance::kProne));

  EXPECT_EQ(Only(interpolator, 0.25F).stance, Stance::kStanding);
  EXPECT_EQ(Only(interpolator, 0.75F).stance, Stance::kProne);
}

TEST(RemoteInterpolatorTest, ARenderTimeBeforeTheFirstUpdateHoldsAtTheFirst) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 1.0F, At(0.0F));
  interpolator.Record(kSessionA, 2.0F, At(10.0F));

  EXPECT_FLOAT_EQ(Only(interpolator, 0.0F).position.x, 0.0F);
}

TEST(RemoteInterpolatorTest, AGapPastTheNewestUpdateHoldsAtTheNewestRatherThanExtrapolating) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F));
  interpolator.Record(kSessionA, 1.0F, At(10.0F));

  // No update has arrived since t=1; render_time keeps advancing anyway.
  EXPECT_FLOAT_EQ(Only(interpolator, 1.5F).position.x, 10.0F);
  EXPECT_FLOAT_EQ(Only(interpolator, 50.0F).position.x, 10.0F);
}

TEST(RemoteInterpolatorTest, SamplingRepeatedlyAtTheSameRenderTimeIsUnaffectedByHowManyTimesItWasSampled) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F));
  interpolator.Record(kSessionA, 1.0F, At(10.0F));

  const float first = Only(interpolator, 0.5F).position.x;
  for (int i = 0; i < 10; ++i) {
    EXPECT_FLOAT_EQ(Only(interpolator, 0.5F).position.x, first) << i;
  }
}

TEST(RemoteInterpolatorTest, AnOutOfOrderOrRepeatedUpdateDoesNotMoveInterpolationBackward) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 1.0F, At(10.0F));

  // Older than, and equal to, the newest recorded timestamp: both ignored.
  interpolator.Record(kSessionA, 0.5F, At(999.0F));
  interpolator.Record(kSessionA, 1.0F, At(999.0F));

  EXPECT_FLOAT_EQ(Only(interpolator, 1.0F).position.x, 10.0F);
}

TEST(RemoteInterpolatorTest, EachSessionIsBufferedAndInterpolatedIndependently) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F));
  interpolator.Record(kSessionA, 1.0F, At(10.0F));
  interpolator.Record(kSessionB, 0.0F, At(0.0F));
  interpolator.Record(kSessionB, 1.0F, At(-20.0F));

  const std::vector<RemotePlayer> sampled = interpolator.Sample(0.5F);

  ASSERT_EQ(sampled.size(), 2U);
  for (const RemotePlayer& player : sampled) {
    if (player.session == kSessionA) {
      EXPECT_FLOAT_EQ(player.body.position.x, 5.0F);
    } else {
      ASSERT_EQ(player.session, kSessionB);
      EXPECT_FLOAT_EQ(player.body.position.x, -10.0F);
    }
  }
}

TEST(RemoteInterpolatorTest, ASessionNoLongerInSyncsCurrentListIsNoLongerSampled) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F));
  interpolator.Record(kSessionB, 0.0F, At(0.0F));

  const std::array<augusta::presentation::SessionId, 1> still_here{kSessionA};
  interpolator.Sync(still_here);

  const std::vector<RemotePlayer> sampled = interpolator.Sample(0.0F);
  ASSERT_EQ(sampled.size(), 1U);
  EXPECT_EQ(sampled.front().session, kSessionA);
}

TEST(RemoteInterpolatorTest, SyncWithEveryoneStillPresentKeepsBufferedHistory) {
  RemoteInterpolator interpolator;
  interpolator.Record(kSessionA, 0.0F, At(0.0F));
  interpolator.Record(kSessionA, 1.0F, At(10.0F));

  const std::array<augusta::presentation::SessionId, 1> still_here{kSessionA};
  interpolator.Sync(still_here);

  // The two updates recorded before Sync are still both buffered, so this still
  // interpolates rather than snapping back to a single point.
  EXPECT_FLOAT_EQ(Only(interpolator, 0.5F).position.x, 5.0F);
}

}  // namespace
