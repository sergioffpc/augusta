#include "augusta/local_view.h"

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"

// Pure: the two ticks, the fraction and the view are handed in, no clock or ECS.
namespace {

using augusta::math::Quat;
using augusta::math::Vec3;
using augusta::physics::Stance;
using augusta::prediction::State;
using augusta::presentation::BlendTicks;
using augusta::presentation::Camera;
using augusta::presentation::LocalCamera;

constexpr float kTolerance = 1e-5F;
const Vec3 kStandingEye(0.0F, 1.7F, 0.1F);
const Quat kLookingAhead(1.0F, 0.0F, 0.0F, 0.0F);

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

State At(const Vec3& position, Stance stance = Stance::kStanding) {
  State state;
  state.local_body.position = position;
  state.local_body.stance = stance;
  return state;
}

TEST(BlendTicksTest, TheBlendedPositionIsTheLerpOfTheTwoTicksAtTheFraction) {
  const State previous = At(Vec3(1.0F, 0.0F, -2.0F));
  const State latest = At(Vec3(2.0F, 0.5F, 4.0F));

  for (const float fraction : {0.1F, 0.25F, 0.5F, 0.9F}) {
    ExpectNear(BlendTicks(previous, latest, fraction).local_body.position,
               augusta::math::Lerp(previous.local_body.position, latest.local_body.position, fraction));
  }
}

TEST(BlendTicksTest, AtZeroItIsThePreviousTickAndAtOneTheLatest) {
  const State previous = At(Vec3(1.0F, 0.0F, -2.0F), Stance::kStanding);
  const State latest = At(Vec3(2.0F, 0.5F, 4.0F), Stance::kCrouching);

  const State at_zero = BlendTicks(previous, latest, 0.0F);
  const State at_one = BlendTicks(previous, latest, 1.0F);

  ExpectNear(at_zero.local_body.position, previous.local_body.position);
  EXPECT_EQ(at_zero.local_body.stance, Stance::kStanding);
  ExpectNear(at_one.local_body.position, latest.local_body.position);
  EXPECT_EQ(at_one.local_body.stance, Stance::kCrouching);
}

TEST(BlendTicksTest, AJumpInTheLatestTickIsBlendedInWithItsCorrection) {
  // The body stood still, but reconciliation moved it 1 m in the latest tick.
  const State previous = At(Vec3(0.0F, 0.0F, 0.0F));
  State latest = At(Vec3(1.0F, 0.0F, 0.0F));
  latest.total_correction = Vec3(1.0F, 0.0F, 0.0F);

  const State blended = BlendTicks(previous, latest, 0.3F);

  // What is left once the correction is taken out is where the body stood.
  ExpectNear(blended.local_body.position - blended.total_correction, Vec3(0.0F, 0.0F, 0.0F));
}

TEST(LocalCameraTest, TheCameraFollowsTheBodysStance) {
  const Vec3 feet(3.0F, 1.0F, -4.0F);

  const Camera standing = LocalCamera(feet, Stance::kStanding, kStandingEye, kLookingAhead);
  const Camera crouching = LocalCamera(feet, Stance::kCrouching, kStandingEye, kLookingAhead);
  const Camera prone = LocalCamera(feet, Stance::kProne, kStandingEye, kLookingAhead);

  ExpectNear(standing.position, feet + kStandingEye);
  ExpectNear(crouching.position, feet + augusta::physics::LowerToStance(kStandingEye, Stance::kCrouching));
  EXPECT_LT(prone.position.y, crouching.position.y);
  EXPECT_LT(crouching.position.y, standing.position.y);
}

TEST(LocalCameraTest, TheCameraTurnsByTheViewHandedToTheFrame) {
  const Quat view(0.9F, 0.1F, 0.3F, 0.2F);

  const Camera camera = LocalCamera(Vec3(0.0F, 0.0F, 0.0F), Stance::kStanding, kStandingEye, view);

  EXPECT_EQ(camera.rotation, view);
}

}  // namespace
