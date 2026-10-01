#include "augusta/audio_cues.h"

#include <numbers>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/audio.h"
#include "augusta/command.h"
#include "augusta/cues.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/presentation.h"

// Pure: the frame's events are handed in, no clock, ECS or audio device.
namespace {

using augusta::audio::Cue;
using augusta::audio::Listener;
using augusta::math::Vec3;
using augusta::presentation::Camera;
using augusta::presentation::CuePlay;
using augusta::presentation::EntityId;
using augusta::presentation::FrameInput;
using augusta::presentation::ListenerOf;
using augusta::presentation::SelectCues;
using augusta::presentation::Shot;

constexpr float kTolerance = 1e-5F;

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

TEST(SelectCuesTest, AFrameWhereNothingHappenedPlaysNoCue) {
  const FrameInput input;

  EXPECT_TRUE(SelectCues(input, 0).empty());
}

TEST(SelectCuesTest, EachRoundThePredictedFireFiredIsTheLocalPlayersOwnGunshot) {
  const FrameInput input;

  // Two rounds since the previous frame, heard at once, as the listener's own.
  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kGunshot, .position = std::nullopt},
                                         CuePlay{.cue = Cue::kGunshot, .position = std::nullopt}};
  EXPECT_EQ(SelectCues(input, 2), expected);
}

TEST(SelectCuesTest, AnotherPlayersShotIsAGunshotHeardFromWhereItWasFired) {
  FrameInput input;
  input.local_entity = EntityId{1};
  input.shots = {Shot{.shooter = EntityId{2}, .origin = Vec3(4.0F, 1.7F, -3.0F), .yaw = 0.5F, .pitch = 0.0F}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kGunshot, .position = Vec3(4.0F, 1.7F, -3.0F)}};
  EXPECT_EQ(SelectCues(input, 0), expected);
}

TEST(SelectCuesTest, TheLocalPlayersOwnShotIsNotHeardAgain) {
  FrameInput input;
  input.local_entity = EntityId{1};
  input.shots = {Shot{.shooter = EntityId{1}, .origin = Vec3(0.0F, 1.7F, 0.0F), .yaw = 0.0F, .pitch = 0.0F}};

  // Its predicted fire was heard a round trip ago.
  EXPECT_TRUE(SelectCues(input, 0).empty());
}

TEST(SelectCuesTest, AHitConfirmationIsTheHitMarkerHeardAsTheListenersOwn) {
  FrameInput input;
  input.hit_confirmations = 1;

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kHitMarker, .position = std::nullopt}};
  EXPECT_EQ(SelectCues(input, 0), expected);
}

TEST(SelectCuesTest, HitsConfirmedTogetherAreHeardAsOneHitMarker) {
  FrameInput input;
  input.hit_confirmations = 3;

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kHitMarker, .position = std::nullopt}};
  EXPECT_EQ(SelectCues(input, 0), expected);
}

TEST(SelectCuesTest, FiringAloneIsNoHitMarker) {
  FrameInput input;
  input.local_entity = EntityId{1};
  input.shots = {Shot{.shooter = EntityId{1}, .origin = Vec3(0.0F, 1.7F, 0.0F), .yaw = 0.0F, .pitch = 0.0F}};

  // A round the prediction fired, and its Shot, but no Hit confirmation yet.
  for (const CuePlay& cue : SelectCues(input, 1)) {
    EXPECT_NE(cue.cue, Cue::kHitMarker);
  }
}

TEST(ListenerOfTest, TheListenerHearsFromTheCameraFacingWhereItLooks) {
  const Camera camera{.position = Vec3(1.0F, 1.7F, 2.0F),
                      .rotation = augusta::command::ViewRotation(std::numbers::pi_v<float> / 2.0F, 0.0F),
                      .vertical_fov = 1.0F};

  const Listener listener = ListenerOf(camera);

  // A quarter turn left from looking down -z looks down -x.
  ExpectNear(listener.position, Vec3(1.0F, 1.7F, 2.0F));
  ExpectNear(listener.forward, Vec3(-1.0F, 0.0F, 0.0F));
  ExpectNear(listener.up, Vec3(0.0F, 1.0F, 0.0F));
}

}  // namespace
