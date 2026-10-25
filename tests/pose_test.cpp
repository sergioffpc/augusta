#include "augusta/pose.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>

#include <gtest/gtest.h>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "clips_fixture.h"

// A player's Hitbox pose evaluated from its Character's Clips (ADR-0066), on
// the Walker the test authors: its stance's blend space at its velocity, each
// Clip sampled where the Stride puts it between its Foot contacts. What the
// tests look at is where the Walker's head and feet end up.
namespace {

using augusta::animation::CharacterClips;
using augusta::animation::PoseInput;
using augusta::animation::Poser;
using augusta::animation::SkeletonPose;
using augusta::assets::Skeleton;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
namespace fixture = augusta::clips_fixture;

// How far ozz-animation's half-float translations and its normalised blend of
// rotations may put a point from where the Clips author it, in meters.
constexpr float kTolerance = 5e-3F;

// The Stride at each Foot contact of the Walker's walks: its left foot lands as
// a cycle starts and its right halfway through, and a quarter of a cycle
// between them its legs pass each other.
constexpr std::uint8_t kLeftContact = 0;
constexpr std::uint8_t kPassing = 64;
constexpr std::uint8_t kRightContact = 128;

class PoseTest : public ::testing::Test {
 protected:
  // The Walker posed on the ground at a body with this velocity, stance and
  // Stride, its feet at position and facing yaw.
  SkeletonPose PoseAt(const Vec3& velocity, Stance stance, std::uint8_t stride, const Vec3& position = Vec3(0.0F),
                      float yaw = 0.0F) {
    BodyState body;
    body.position = position;
    body.velocity = velocity;
    body.stance = stance;
    body.stride = stride;
    body.grounded = true;
    return PoseOf(body, yaw);
  }

  // The Walker posed at body, facing yaw.
  SkeletonPose PoseOf(const BodyState& body, float yaw = 0.0F) {
    SkeletonPose posed;
    poser_.Pose(*clips_, skeleton_, PoseInput{.body = body, .yaw = yaw}, posed);
    return posed;
  }

  // Where joint's end is: 1 m down a leg from its hip.
  static Vec3 FootOf(const SkeletonPose& posed, std::uint32_t leg) {
    return augusta::math::TransformPoint(posed.world.at(leg), Vec3(0.0F, -1.0F, 0.0F));
  }

  static Vec3 HeadOf(const SkeletonPose& posed) {
    return augusta::math::TransformPoint(posed.world.at(fixture::kWalkerHead), Vec3(0.0F));
  }

  std::shared_ptr<const CharacterClips> clips_ = fixture::WalkerClips();
  Skeleton skeleton_ = fixture::WalkerSkeleton();
  Poser poser_;
};

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// Walking ahead (-Z) at its forward walk's own speed.
const Vec3 kWalking(0.0F, 0.0F, -fixture::kStandingForward);

// A leg kLegSwing ahead of straight down puts its foot ahead (-Z) and up.
const float kFootAhead = std::sin(fixture::kLegSwing);
const float kFootUp = 1.0F - std::cos(fixture::kLegSwing);

TEST_F(PoseTest, StillACrouchedBodysHeadIsWhereItsCrouchingIdleHoldsIt) {
  const Vec3 standing = HeadOf(PoseAt(Vec3(0.0F), Stance::kStanding, 0));
  const Vec3 crouched = HeadOf(PoseAt(Vec3(0.0F), Stance::kCrouching, 0));

  ExpectNear(standing, Vec3(0.0F, fixture::kPelvisStanding + fixture::kHeadAbovePelvis, 0.0F));
  ExpectNear(crouched, Vec3(0.0F, fixture::kPelvisCrouching + fixture::kHeadAbovePelvis, 0.0F));
}

TEST_F(PoseTest, AtTheLeftFootContactTheLeftLegIsAheadAndTheRightBehind) {
  const SkeletonPose posed = PoseAt(kWalking, Stance::kStanding, kLeftContact);

  ExpectNear(FootOf(posed, fixture::kWalkerLeftLeg), Vec3(-fixture::kHipSide, kFootUp, -kFootAhead));
  ExpectNear(FootOf(posed, fixture::kWalkerRightLeg), Vec3(fixture::kHipSide, kFootUp, kFootAhead));
}

TEST_F(PoseTest, HalfAStrideOnTheRightLegIsAheadAndAQuarterOnTheLegsPass) {
  const SkeletonPose right = PoseAt(kWalking, Stance::kStanding, kRightContact);
  const SkeletonPose passing = PoseAt(kWalking, Stance::kStanding, kPassing);

  ExpectNear(FootOf(right, fixture::kWalkerRightLeg), Vec3(fixture::kHipSide, kFootUp, -kFootAhead));
  ExpectNear(FootOf(passing, fixture::kWalkerLeftLeg), Vec3(-fixture::kHipSide, 0.0F, 0.0F));
  ExpectNear(FootOf(passing, fixture::kWalkerRightLeg), Vec3(fixture::kHipSide, 0.0F, 0.0F));
}

// Ahead and to the right, half way between its forward walk and its right one,
// at their blended speed so neither idle nor sprint joins them. The right walk
// lands its left foot a quarter of a cycle later than the forward one: sampled
// at the same ratio, its left leg would be straight down as the forward walk's
// is ahead, and the blend half way between.
TEST_F(PoseTest, TwoBlendedWalksLandTheSameFootTogether) {
  const float speed = (fixture::kStandingForward + fixture::kStandingRight) / 2.0F;
  const float along = speed / std::numbers::sqrt2_v<float>;

  const SkeletonPose posed = PoseAt(Vec3(along, 0.0F, -along), Stance::kStanding, kLeftContact);

  ExpectNear(FootOf(posed, fixture::kWalkerLeftLeg), Vec3(-fixture::kHipSide, kFootUp, -kFootAhead));
}

// Facing a quarter turn to its left (yaw +pi/2 looks down -X), at (5, 0, 2).
TEST_F(PoseTest, ThePositionAndTheYawMoveAndTurnThePose) {
  const float quarter_turn = std::numbers::pi_v<float> / 2.0F;
  const Vec3 ahead_of_it(-fixture::kStandingForward, 0.0F, 0.0F);

  const SkeletonPose posed = PoseAt(ahead_of_it, Stance::kStanding, kLeftContact, Vec3(5.0F, 0.0F, 2.0F), quarter_turn);

  ExpectNear(FootOf(posed, fixture::kWalkerLeftLeg), Vec3(5.0F - kFootAhead, kFootUp, 2.0F + fixture::kHipSide));
}

// Down from standing into a crouch, still: the Pelvis is held at each stance's
// height by its idle, so the head moves down with the change's progress.
TEST_F(PoseTest, ThroughAChangeOfStanceTheHeadMovesDownByItsProgress) {
  BodyState body;
  body.stance = Stance::kCrouching;
  body.previous_stance = Stance::kStanding;
  body.grounded = true;
  SkeletonPose started;
  SkeletonPose a_fifth;
  SkeletonPose over;

  body.stance_progress = 0;
  poser_.Pose(*clips_, skeleton_, PoseInput{.body = body}, started);
  body.stance_progress = 51;
  poser_.Pose(*clips_, skeleton_, PoseInput{.body = body}, a_fifth);
  body.stance_progress = augusta::physics::kStanceProgressSteps;
  poser_.Pose(*clips_, skeleton_, PoseInput{.body = body}, over);

  const float standing = fixture::kPelvisStanding + fixture::kHeadAbovePelvis;
  const float crouched = fixture::kPelvisCrouching + fixture::kHeadAbovePelvis;
  ExpectNear(HeadOf(started), Vec3(0.0F, standing, 0.0F));
  // 51 steps of 255 are a fifth of the way down.
  ExpectNear(HeadOf(a_fifth), Vec3(0.0F, standing + ((crouched - standing) / 5.0F), 0.0F));
  ExpectNear(HeadOf(over), Vec3(0.0F, crouched, 0.0F));
}

// Posing is a function of what it is given alone: the same state posed again
// after another gives the same pose.
TEST_F(PoseTest, ThePoseOfAStateIsTheSameWhateverWasPosedBefore) {
  const SkeletonPose first = PoseAt(kWalking, Stance::kStanding, 40);
  PoseAt(Vec3(0.0F, 0.0F, 1.5F), Stance::kCrouching, 200);
  const SkeletonPose again = PoseAt(kWalking, Stance::kStanding, 40);

  ASSERT_EQ(first.world.size(), again.world.size());
  for (std::size_t joint = 0; joint < first.world.size(); ++joint) {
    EXPECT_EQ(first.world[joint], again.world[joint]);
  }
}

// The Aimer (clips_fixture.h) still, posed at a view's pitch and ADS: its Aim
// offset bends it by its pitch, its ADS grip raises its Arm.
class AimTest : public ::testing::Test {
 protected:
  // The Aimer on the ground in stance, which it has finished changing to unless
  // a test names the stance it left and how far it has gone from it.
  SkeletonPose PoseAt(Stance stance, float pitch, bool ads = false, Stance previous = Stance::kStanding,
                      std::uint8_t progress = static_cast<std::uint8_t>(augusta::physics::kStanceProgressSteps)) {
    BodyState body;
    body.stance = stance;
    body.previous_stance = previous;
    body.stance_progress = progress;
    body.grounded = true;
    SkeletonPose posed;
    poser_.Pose(*clips_, skeleton_, PoseInput{.body = body, .yaw = 0.0F, .pitch = pitch, .ads = ads}, posed);
    return posed;
  }

  static Vec3 HeadOf(const SkeletonPose& posed) {
    return augusta::math::TransformPoint(posed.world.at(fixture::kAimerHead), Vec3(0.0F));
  }

  // Where the Arm's end is: 0.6 m down it from its shoulder.
  static Vec3 HandOf(const SkeletonPose& posed) {
    return augusta::math::TransformPoint(posed.world.at(fixture::kAimerArm), Vec3(0.0F, -0.6F, 0.0F));
  }

  std::shared_ptr<const CharacterClips> clips_ = fixture::AimerClips();
  Skeleton skeleton_ = fixture::AimerSkeleton();
  Poser poser_;
};

// The head kHeadAbovePelvis from a Pelvis pelvis high, bent ahead (-Z) by bend.
Vec3 HeadBentBy(float pelvis, float bend) {
  return Vec3(0.0F, pelvis + (fixture::kHeadAbovePelvis * std::cos(bend)), -fixture::kHeadAbovePelvis * std::sin(bend));
}

constexpr float kLowest = -augusta::command::kMaxLookPitch;
constexpr float kHighest = augusta::command::kMaxLookPitch;

TEST_F(AimTest, AimingAtTheLowestPitchBendsTheBodyByItsAimDown) {
  const Vec3 head = HeadOf(PoseAt(Stance::kStanding, kLowest));

  ExpectNear(head, HeadBentBy(fixture::kPelvisStanding, fixture::kStandingAimBend));
}

TEST_F(AimTest, AimingAtTheHighestPitchLeansTheBodyBackByItsAimUp) {
  const Vec3 head = HeadOf(PoseAt(Stance::kStanding, kHighest));

  ExpectNear(head, HeadBentBy(fixture::kPelvisStanding, -fixture::kStandingAimBend));
}

// Half way down, the Aim offset bends it half as far as at the lowest pitch.
TEST_F(AimTest, HalfWayDownTheBodyBendsHalfAsFar) {
  const Vec3 head = HeadOf(PoseAt(Stance::kStanding, kLowest / 2.0F));

  ExpectNear(head, HeadBentBy(fixture::kPelvisStanding, fixture::kStandingAimBend / 2.0F));
}

// The hand 0.6 m down an Arm from a shoulder on a Pelvis pelvis high, swung up
// ahead (-Z) by raise.
Vec3 HandRaisedBy(float pelvis, float raise) {
  return Vec3(fixture::kAimerShoulderSide, pelvis + fixture::kAimerShoulderAbovePelvis - (0.6F * std::cos(raise)),
              -0.6F * std::sin(raise));
}

TEST_F(AimTest, WithoutADSTheArmHangsAndWithItTheADSGripRaisesIt) {
  const Vec3 hanging = HandOf(PoseAt(Stance::kStanding, 0.0F));
  const Vec3 raised = HandOf(PoseAt(Stance::kStanding, 0.0F, true));

  ExpectNear(hanging, HandRaisedBy(fixture::kPelvisStanding, 0.0F));
  ExpectNear(raised, HandRaisedBy(fixture::kPelvisStanding, fixture::kStandingAdsRaise));
}

// Aiming down in ADS, the body bends and the arm is raised from the bent
// shoulder: both add onto the pose.
TEST_F(AimTest, AimingDownInADSBendsTheBodyAndRaisesTheArm) {
  const SkeletonPose posed = PoseAt(Stance::kStanding, kLowest, true);
  const Vec3 head = HeadOf(posed);
  const Vec3 hand = HandOf(posed);

  ExpectNear(head, HeadBentBy(fixture::kPelvisStanding, fixture::kStandingAimBend));
  // The Arm turns with the bent Pelvis and is raised within it: from where the
  // bend puts the shoulder, swung up ahead by the raise less the bend, which
  // tips it down.
  const float bend = fixture::kStandingAimBend;
  const Vec3 shoulder(fixture::kAimerShoulderSide,
                      fixture::kPelvisStanding + (fixture::kAimerShoulderAbovePelvis * std::cos(bend)),
                      -fixture::kAimerShoulderAbovePelvis * std::sin(bend));
  const float swing = fixture::kStandingAdsRaise - bend;
  ExpectNear(hand, shoulder + Vec3(0.0F, -0.6F * std::cos(swing), -0.6F * std::sin(swing)));
}

TEST_F(AimTest, EachStanceBendsAndRaisesByItsOwnAimOffsetAndADSGrip) {
  const SkeletonPose crouched = PoseAt(Stance::kCrouching, kLowest, true);
  const SkeletonPose prone = PoseAt(Stance::kProne, kHighest);
  const Vec3 crouched_head = HeadOf(crouched);
  const Vec3 prone_head = HeadOf(prone);
  const Vec3 crouched_hand = HandOf(PoseAt(Stance::kCrouching, 0.0F, true));

  ExpectNear(crouched_head, HeadBentBy(fixture::kPelvisCrouching, fixture::kCrouchingAimBend));
  ExpectNear(prone_head, HeadBentBy(fixture::kPelvisProne, -fixture::kProneAimBend));
  ExpectNear(crouched_hand, HandRaisedBy(fixture::kPelvisCrouching, fixture::kCrouchingAdsRaise));
}

// Just crouched from standing, aiming down, the body is still all standing's:
// standing's own Aim offset bends it, so a change of stance never straightens
// an aiming body.
TEST_F(AimTest, ThroughAChangeOfStanceTheStanceItLeftKeepsItsOwnAimOffset) {
  const SkeletonPose posed = PoseAt(Stance::kCrouching, kLowest, false, Stance::kStanding, 0);
  const Vec3 head = HeadOf(posed);

  ExpectNear(head, HeadBentBy(fixture::kPelvisStanding, fixture::kStandingAimBend));
}

// A standing body off the ground, at the origin, moving at velocity.
BodyState InTheAir(const Vec3& velocity) {
  BodyState body;
  body.position = Vec3(0.0F);
  body.velocity = velocity;
  body.stride = kLeftContact;
  return body;
}

// The Walker's air Clip lays a leg level with its hip, 1 m ahead (-Z) or behind.
const Vec3 kLeftFootTucked(-fixture::kHipSide, fixture::kPelvisStanding, -1.0F);
const Vec3 kRightFootAhead(fixture::kHipSide, fixture::kPelvisStanding, -1.0F);
const Vec3 kRightFootBehind(fixture::kHipSide, fixture::kPelvisStanding, 1.0F);
const Vec3 kRightFootDown(fixture::kHipSide, 0.0F, 0.0F);

// Walking ahead at a left Foot contact, its left leg would be kLegSwing ahead;
// off the ground the air Clip has it tucked level with its hip instead.
TEST_F(PoseTest, OffTheGroundABodyIsPosedByItsAirClipInPlaceOfItsBlendSpace) {
  const SkeletonPose posed = PoseOf(InTheAir(kWalking));

  ExpectNear(FootOf(posed, fixture::kWalkerLeftLeg), kLeftFootTucked);
}

// From rising at kAirVerticalSpeed (its start) to falling as fast (its end),
// the top of a jump halfway; faster is held at either end.
TEST_F(PoseTest, TheAirClipIsSampledByTheBodysVerticalVelocityFromRisingToFalling) {
  constexpr float kSpeed = augusta::animation::kAirVerticalSpeed;

  const SkeletonPose rising = PoseOf(InTheAir(Vec3(0.0F, kSpeed, 0.0F)));
  const SkeletonPose top = PoseOf(InTheAir(Vec3(0.0F)));
  const SkeletonPose falling = PoseOf(InTheAir(Vec3(0.0F, -kSpeed, 0.0F)));
  const SkeletonPose rising_faster = PoseOf(InTheAir(Vec3(0.0F, 4.0F * kSpeed, 0.0F)));
  const SkeletonPose falling_faster = PoseOf(InTheAir(Vec3(0.0F, -4.0F * kSpeed, 0.0F)));

  ExpectNear(FootOf(rising, fixture::kWalkerRightLeg), kRightFootAhead);
  ExpectNear(FootOf(top, fixture::kWalkerRightLeg), kRightFootDown);
  ExpectNear(FootOf(falling, fixture::kWalkerRightLeg), kRightFootBehind);
  ExpectNear(FootOf(rising_faster, fixture::kWalkerRightLeg), kRightFootAhead);
  ExpectNear(FootOf(falling_faster, fixture::kWalkerRightLeg), kRightFootBehind);
}

// The pose switches on the tick the body lands, with nothing of the air Clip
// left: posed on the ground right after the air, it is the ground's pose.
TEST_F(PoseTest, TheTickABodyLandsItIsPosedOnTheGroundWithNoCrossfade) {
  BodyState landed = InTheAir(kWalking);
  landed.grounded = true;
  const SkeletonPose ground = PoseOf(landed);
  PoseOf(InTheAir(kWalking));
  const SkeletonPose after_the_air = PoseOf(landed);

  ExpectNear(FootOf(after_the_air, fixture::kWalkerLeftLeg), Vec3(-fixture::kHipSide, kFootUp, -kFootAhead));
  ASSERT_EQ(ground.world.size(), after_the_air.world.size());
  for (std::size_t joint = 0; joint < ground.world.size(); ++joint) {
    EXPECT_EQ(ground.world[joint], after_the_air.world[joint]);
  }
}

}  // namespace
