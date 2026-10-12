#include "augusta/local_view.h"

#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/weapon.h"

// Pure: the two ticks, the fraction and the view are handed in, no clock or ECS.
namespace {

using augusta::math::Vec3;
using augusta::physics::Stance;
using augusta::prediction::State;
using augusta::presentation::AdsZoom;
using augusta::presentation::Aim;
using augusta::presentation::BlendTicks;
using augusta::presentation::Camera;
using augusta::presentation::EntityId;
using augusta::presentation::HitMarker;
using augusta::presentation::kHipFieldOfView;
using augusta::presentation::LocalCamera;
using augusta::presentation::RemoteBody;
using augusta::presentation::Spectator;
using augusta::presentation::WatchedCamera;
using augusta::weapon::RecoilOffset;

constexpr float kTolerance = 1e-5F;
const Vec3 kStandingEye(0.0F, 1.7F, 0.1F);
constexpr Aim kLookingAhead{};
constexpr RecoilOffset kNoRecoil{};
constexpr float kAdsFieldOfView = 0.5F;
constexpr float kFrame = 1.0F / 120.0F;

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

TEST(BlendTicksTest, TheRecoilOffsetIsBlendedBetweenTheTwoTicks) {
  State previous;
  previous.rifle.recoil = RecoilOffset{.pitch = 0.02F, .yaw = -0.01F};
  State latest;
  latest.rifle.recoil = RecoilOffset{.pitch = 0.04F, .yaw = 0.01F};

  const RecoilOffset blended = BlendTicks(previous, latest, 0.25F).rifle.recoil;

  EXPECT_NEAR(blended.pitch, 0.025F, kTolerance);
  EXPECT_NEAR(blended.yaw, -0.005F, kTolerance);
}

TEST(LocalCameraTest, TheCameraFollowsTheBodysStance) {
  const Vec3 feet(3.0F, 1.0F, -4.0F);

  const Camera standing = LocalCamera(feet, Stance::kStanding, kStandingEye, kLookingAhead, kNoRecoil);
  const Camera crouching = LocalCamera(feet, Stance::kCrouching, kStandingEye, kLookingAhead, kNoRecoil);
  const Camera prone = LocalCamera(feet, Stance::kProne, kStandingEye, kLookingAhead, kNoRecoil);

  ExpectNear(standing.position, feet + kStandingEye);
  ExpectNear(crouching.position, feet + augusta::physics::LowerToStance(kStandingEye, Stance::kCrouching));
  EXPECT_LT(prone.position.y, crouching.position.y);
  EXPECT_LT(crouching.position.y, standing.position.y);
}

TEST(LocalCameraTest, TheCameraTurnsByTheViewHandedToTheFrame) {
  const Aim aim{.yaw = 0.7F, .pitch = -0.2F, .ads = false};

  const Camera camera = LocalCamera(Vec3(0.0F, 0.0F, 0.0F), Stance::kStanding, kStandingEye, aim, kNoRecoil);

  EXPECT_EQ(camera.rotation, augusta::command::ViewRotation(aim.yaw, aim.pitch));
}

// The view shows where the next round goes: a rifle climbing by its recoil
// takes the camera with it, off the view the player holds.
TEST(LocalCameraTest, TheCameraLooksWhereTheNextRoundLeaves) {
  augusta::parameters::Rifle rifle;
  rifle.magazine_capacity = 30;
  rifle.rounds_per_minute = 600.0F;
  rifle.recoil_pattern = {{.pitch = 0.03F, .yaw = 0.01F}, {.pitch = 0.02F, .yaw = -0.02F}};
  augusta::command::Command command;
  command.yaw = 0.4F;
  command.pitch = 0.1F;
  command.fire = true;
  augusta::weapon::State held = augusta::weapon::Loaded(rifle);
  held = augusta::weapon::Step(rifle, held, command, 1.0F).state;
  const augusta::weapon::Result next = augusta::weapon::Step(rifle, held, command, 1.0F);
  ASSERT_TRUE(next.fired);

  const Camera camera = LocalCamera(Vec3(0.0F, 0.0F, 0.0F), Stance::kStanding, kStandingEye,
                                    Aim{.yaw = command.yaw, .pitch = command.pitch, .ads = false}, held.recoil);

  const Vec3 looking = camera.rotation * Vec3(0.0F, 0.0F, -1.0F);
  ExpectNear(looking, augusta::command::ViewDirection(next.yaw, next.pitch));
}

// Requirements: US-06
TEST(AdsZoomTest, FromTheHipTheViewIsNotZoomed) {
  AdsZoom zoom;

  EXPECT_FLOAT_EQ(zoom.Update(false, kAdsFieldOfView, kFrame), kHipFieldOfView);
}

// Requirements: US-06
TEST(AdsZoomTest, HoldingAdsZoomsToItsFieldOfViewOverTheTransition) {
  AdsZoom zoom;

  const float first = zoom.Update(true, kAdsFieldOfView, kFrame);
  float previous = first;
  for (float held = kFrame; held < AdsZoom::kTransitionSeconds - kFrame; held += kFrame) {
    const float now = zoom.Update(true, kAdsFieldOfView, kFrame);
    EXPECT_LE(now, previous);
    previous = now;
  }
  const float settled = zoom.Update(true, kAdsFieldOfView, 2.0F * kFrame);

  EXPECT_LT(first, kHipFieldOfView);
  EXPECT_GT(first, kAdsFieldOfView);
  EXPECT_FLOAT_EQ(settled, kAdsFieldOfView);
}

// Requirements: US-06
TEST(AdsZoomTest, ReleasingAdsZoomsBackOut) {
  AdsZoom zoom;
  (void)zoom.Update(true, kAdsFieldOfView, AdsZoom::kTransitionSeconds);

  const float releasing = zoom.Update(false, kAdsFieldOfView, kFrame);
  const float released = zoom.Update(false, kAdsFieldOfView, AdsZoom::kTransitionSeconds);

  EXPECT_GT(releasing, kAdsFieldOfView);
  EXPECT_LT(releasing, kHipFieldOfView);
  EXPECT_FLOAT_EQ(released, kHipFieldOfView);
}

// Requirements: US-11, US-19
TEST(HitMarkerTest, NoHitMarkerShowsWithoutAHitConfirmation) {
  HitMarker marker;

  for (int frame = 0; frame < 100; ++frame) {
    EXPECT_FALSE(marker.Update(0, kFrame)) << frame;
  }
}

// Requirements: US-11, US-19
TEST(HitMarkerTest, AHitConfirmationShowsTheMarkerForAMoment) {
  HitMarker marker;

  EXPECT_TRUE(marker.Update(1, kFrame));
  EXPECT_TRUE(marker.Update(0, HitMarker::kShownSeconds / 2.0F));
  EXPECT_FALSE(marker.Update(0, HitMarker::kShownSeconds));
}

// Requirements: US-11, US-19
TEST(HitMarkerTest, EachHitConfirmationShowsTheMarkerAfresh) {
  HitMarker marker;
  (void)marker.Update(1, kFrame);
  (void)marker.Update(0, HitMarker::kShownSeconds * 0.9F);

  EXPECT_TRUE(marker.Update(1, kFrame));
  EXPECT_TRUE(marker.Update(0, HitMarker::kShownSeconds * 0.9F));
}

// The match's players in Session order; the local player (kLocal) is dead.
constexpr EntityId kLocal{1};
constexpr EntityId kSecond{2};
constexpr EntityId kThird{3};
constexpr EntityId kFourth{4};
const std::vector<EntityId> kPlayers{kLocal, kSecond, kThird, kFourth};

// Requirements: US-13
TEST(SpectatorTest, ADeadPlayerFirstWatchesTheFirstLivingPlayerInSessionOrder) {
  Spectator spectator;

  EXPECT_EQ(spectator.Update(kPlayers, std::vector{kFourth, kThird}, false), kThird);
}

// Requirements: US-13
TEST(SpectatorTest, EachPressOfFireWatchesTheNextLivingPlayerInSessionOrderSkippingTheDead) {
  Spectator spectator;
  const std::vector living{kSecond, kFourth};
  (void)spectator.Update(kPlayers, living, false);

  EXPECT_EQ(spectator.Update(kPlayers, living, true), kFourth);
  EXPECT_EQ(spectator.Update(kPlayers, living, false), kFourth);
  EXPECT_EQ(spectator.Update(kPlayers, living, true), kSecond);
}

// A player killed while firing does not skip past the first living player, nor
// cycle on for as long as fire stays held.
// Requirements: US-13
TEST(SpectatorTest, HoldingFireMovesOnOncePerPress) {
  Spectator spectator;
  const std::vector all_alive{kSecond, kThird, kFourth};

  EXPECT_EQ(spectator.Update(kPlayers, all_alive, true), kSecond);
  EXPECT_EQ(spectator.Update(kPlayers, all_alive, true), kSecond);
  EXPECT_EQ(spectator.Update(kPlayers, all_alive, false), kSecond);
  EXPECT_EQ(spectator.Update(kPlayers, all_alive, true), kThird);
  EXPECT_EQ(spectator.Update(kPlayers, all_alive, true), kThird);
}

// Requirements: US-13
TEST(SpectatorTest, WhenTheWatchedPlayerDiesTheViewMovesOnToTheNextLivingPlayer) {
  Spectator spectator;
  const std::vector all_alive{kSecond, kThird, kFourth};
  (void)spectator.Update(kPlayers, all_alive, false);
  ASSERT_EQ(spectator.Update(kPlayers, all_alive, true), kThird);

  EXPECT_EQ(spectator.Update(kPlayers, std::vector{kSecond, kFourth}, false), kFourth);
  EXPECT_EQ(spectator.Update(kPlayers, std::vector{kSecond}, false), kSecond);
}

// Requirements: US-13
TEST(SpectatorTest, WithNoOneLeftAliveNoOneIsWatched) {
  Spectator spectator;
  (void)spectator.Update(kPlayers, std::vector{kThird}, false);

  EXPECT_EQ(spectator.Update(kPlayers, {}, false), std::nullopt);
  EXPECT_EQ(spectator.Update(kPlayers, {}, true), std::nullopt);
}

// Remote pitch is not replicated, so the watched view looks level.
// Requirements: US-13
TEST(WatchedCameraTest, TheCameraIsAtTheWatchedPlayersEyeForItsStanceLookingLevelWhereItFaces) {
  const RemoteBody crouching{.position = Vec3(3.0F, 1.0F, -4.0F),
                             .velocity = Vec3(1.0F, 0.0F, 0.0F),
                             .yaw = 0.6F,
                             .stance = Stance::kCrouching};

  const Camera camera = WatchedCamera(crouching, kStandingEye, 0.0F);

  ExpectNear(camera.position,
             Vec3(3.0F, 1.0F, -4.0F) + augusta::physics::LowerToStance(kStandingEye, Stance::kCrouching));
  EXPECT_EQ(camera.rotation, augusta::command::ViewRotation(0.6F, 0.0F));
  EXPECT_FLOAT_EQ(camera.vertical_fov, kHipFieldOfView);
}

// A Replay viewer is shown the watched player's pitch (ADR-0051).
// Requirements: US-21
TEST(WatchedCameraTest, TheCameraLooksAtTheWatchedPlayersPitchWhenItIsKnown) {
  const RemoteBody standing{.position = Vec3(1.0F, 0.0F, 2.0F), .velocity = {}, .yaw = -0.4F, .stance = {}};

  const Camera camera = WatchedCamera(standing, kStandingEye, 0.3F);

  EXPECT_EQ(camera.rotation, augusta::command::ViewRotation(-0.4F, 0.3F));
}

}  // namespace
