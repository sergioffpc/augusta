#include "augusta/locomotion.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <span>

#include <gtest/gtest.h>

#include "augusta/clips.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "clips_fixture.h"

// A stance's blend space over the body's velocity in its own space (ADR-0066),
// and the stride length a Stride advances by at that velocity, on Clips the
// test authors: in the body's space -Z is forward and +X its right.
namespace {

using augusta::animation::BlendSpace;
using augusta::animation::CharacterClips;
using augusta::animation::Clip;
using augusta::animation::ClipWeight;
using augusta::animation::StanceClips;
using augusta::animation::StrideLength;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
namespace fixture = augusta::clips_fixture;

constexpr float kTolerance = 1e-4F;

class LocomotionTest : public ::testing::Test {
 protected:
  [[nodiscard]] const StanceClips& Standing() const { return clips_->Of(Stance::kStanding); }
  [[nodiscard]] const StanceClips& Prone() const { return clips_->Of(Stance::kProne); }

  std::shared_ptr<const CharacterClips> clips_ = fixture::TestClips();
};

// The weight of clip in weights, 0 if it is not among them.
float WeightOf(std::span<const ClipWeight> weights, const Clip& clip) {
  float weight = 0.0F;
  for (const ClipWeight& each : weights) {
    if (each.clip == &clip) {
      weight += each.weight;
    }
  }
  return weight;
}

// Requirements: US-11
TEST_F(LocomotionTest, MovingForwardAtTheForwardClipsSpeedIsTheForwardClipAlone) {
  const auto blend = BlendSpace(Standing(), Vec3(0.0F, 0.0F, -fixture::kStandingForward));

  EXPECT_NEAR(WeightOf(blend.Weights(), Standing().forward), 1.0F, kTolerance);
  EXPECT_NEAR(WeightOf(blend.Weights(), Standing().idle), 0.0F, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, StillIsTheIdleClipAlone) {
  const auto blend = BlendSpace(Standing(), Vec3(0.0F, 0.0F, 0.0F));

  ASSERT_EQ(blend.Weights().size(), 1U);
  EXPECT_EQ(blend.Weights()[0].clip, &Standing().idle);
  EXPECT_NEAR(blend.Weights()[0].weight, 1.0F, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, BelowAClipsSpeedTheIdleClipMakesUpTheRest) {
  const auto blend = BlendSpace(Standing(), Vec3(0.0F, 0.0F, -fixture::kStandingForward / 4.0F));

  EXPECT_NEAR(WeightOf(blend.Weights(), Standing().forward), 0.25F, kTolerance);
  EXPECT_NEAR(WeightOf(blend.Weights(), Standing().idle), 0.75F, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, BetweenTwoDirectionsTheirClipsShareTheWeightByAngle) {
  // A third of the way from forward to the right, as fast as the two Clips
  // move there: neither idle nor sprint.
  const float angle = std::numbers::pi_v<float> / 6.0F;
  const float speed = (fixture::kStandingForward * 2.0F / 3.0F) + (fixture::kStandingRight / 3.0F);
  const auto blend = BlendSpace(Standing(), Vec3(std::sin(angle), 0.0F, -std::cos(angle)) * speed);

  EXPECT_NEAR(WeightOf(blend.Weights(), Standing().forward), 2.0F / 3.0F, kTolerance);
  EXPECT_NEAR(WeightOf(blend.Weights(), *Standing().right), 1.0F / 3.0F, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, TheWeightsAlwaysSumToOne) {
  for (const Vec3& velocity : {Vec3(0.3F, 0.0F, 0.1F), Vec3(-2.0F, 0.0F, 5.0F), Vec3(0.0F, 0.0F, -4.0F)}) {
    float sum = 0.0F;
    for (const ClipWeight& each : BlendSpace(Standing(), velocity).Weights()) {
      sum += each.weight;
    }

    EXPECT_NEAR(sum, 1.0F, kTolerance);
  }
}

// Requirements: US-11
TEST_F(LocomotionTest, EachDirectionsStrideLengthIsItsClips) {
  EXPECT_NEAR(StrideLength(Standing(), Vec3(0.0F, 0.0F, -3.0F)), fixture::kStandingForward, kTolerance);
  EXPECT_NEAR(StrideLength(Standing(), Vec3(0.0F, 0.0F, 2.0F)), fixture::kStandingBackward, kTolerance);
  EXPECT_NEAR(StrideLength(Standing(), Vec3(-2.0F, 0.0F, 0.0F)), fixture::kStandingLeft, kTolerance);
  EXPECT_NEAR(StrideLength(Standing(), Vec3(2.0F, 0.0F, 0.0F)), fixture::kStandingRight, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, TheStrideLengthIsTheMovingClipsBlendedWithoutTheIdle) {
  // Halfway between forward and right, well below their speed.
  const float slow = 0.5F;

  EXPECT_NEAR(StrideLength(Standing(), Vec3(slow, 0.0F, -slow)),
              (fixture::kStandingForward + fixture::kStandingRight) / 2.0F, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, StillTheStrideLengthIsTheForwardClips) {
  EXPECT_NEAR(StrideLength(Standing(), Vec3(0.0F, 0.0F, 0.0F)), fixture::kStandingForward, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, AtTheSprintClipsSpeedTheStrideLengthIsTheSprintClips) {
  EXPECT_NEAR(StrideLength(Standing(), Vec3(0.0F, 0.0F, -fixture::kStandingSprint)), fixture::kStandingSprint,
              kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, BetweenTheWalkAndTheSprintSpeedTheStrideLengthIsBetweenTheirs) {
  const float halfway = (fixture::kStandingForward + fixture::kStandingSprint) / 2.0F;

  EXPECT_NEAR(StrideLength(Standing(), Vec3(0.0F, 0.0F, -halfway)), halfway, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, WithoutSideClipsMovingSidewaysBlendsForwardAndBackward) {
  EXPECT_NEAR(StrideLength(Prone(), Vec3(0.3F, 0.0F, 0.0F)), (fixture::kProneForward + fixture::kProneBackward) / 2.0F,
              kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, AVerticalVelocityDoesNotCount) {
  EXPECT_NEAR(StrideLength(Standing(), Vec3(0.0F, -9.0F, -3.0F)), fixture::kStandingForward, kTolerance);
}

// Requirements: US-11
TEST_F(LocomotionTest, ABodysStrideLengthIsItsStancesAtItsVelocityInTheSpaceItFaces) {
  // Turned a quarter left, -X is ahead of it.
  const float yaw = std::numbers::pi_v<float> / 2.0F;
  BodyState body{};
  body.velocity = Vec3(-1.0F, 0.0F, 0.0F);
  body.stance = Stance::kCrouching;

  EXPECT_NEAR(PacingOf(*clips_, body, yaw).stride_length, fixture::kCrouchingForward, kTolerance);
}

}  // namespace
