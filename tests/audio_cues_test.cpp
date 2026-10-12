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
using augusta::presentation::CueSelector;
using augusta::presentation::DynamicBody;
using augusta::presentation::EntityId;
using augusta::presentation::FrameInput;
using augusta::presentation::ListenerOf;
using augusta::presentation::MatchEnd;
using augusta::presentation::Shot;
using augusta::presentation::WorldSnapshot;

constexpr float kTolerance = 1e-5F;

constexpr double kTickDuration = 1.0 / 60.0;
const Vec3 kThere(5.0F, 0.0F, -2.0F);

DynamicBody BodyAt(EntityId entity, const Vec3& position) {
  DynamicBody body{.entity = entity, .state = {}, .yaw = 0.0F};
  body.state.position = position;
  return body;
}

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// Requirements: US-18
TEST(CueSelectorTest, AFrameWhereNothingHappenedPlaysNoCue) {
  const FrameInput input;

  EXPECT_TRUE(CueSelector().Select(input, 0).empty());
}

// Requirements: US-18
TEST(CueSelectorTest, EachRoundThePredictedFireFiredIsTheLocalPlayersOwnGunshot) {
  const FrameInput input;

  // Two rounds since the previous frame, heard at once, as the listener's own.
  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kGunshot, .position = std::nullopt},
                                         CuePlay{.cue = Cue::kGunshot, .position = std::nullopt}};
  EXPECT_EQ(CueSelector().Select(input, 2), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, AnotherPlayersShotIsAGunshotHeardFromWhereItWasFired) {
  FrameInput input;
  input.local_entity = EntityId{1};
  input.shots = {Shot{.shooter = EntityId{2}, .origin = Vec3(4.0F, 1.7F, -3.0F), .yaw = 0.5F, .pitch = 0.0F}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kGunshot, .position = Vec3(4.0F, 1.7F, -3.0F)}};
  EXPECT_EQ(CueSelector().Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, TheLocalPlayersOwnShotIsNotHeardAgain) {
  FrameInput input;
  input.local_entity = EntityId{1};
  input.shots = {Shot{.shooter = EntityId{1}, .origin = Vec3(0.0F, 1.7F, 0.0F), .yaw = 0.0F, .pitch = 0.0F}};

  // Its predicted fire was heard a round trip ago.
  EXPECT_TRUE(CueSelector().Select(input, 0).empty());
}

// Requirements: US-18
TEST(CueSelectorTest, AHitConfirmationIsTheHitMarkerHeardAsTheListenersOwn) {
  FrameInput input;
  input.hit_confirmations = 1;

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kHitMarker, .position = std::nullopt}};
  EXPECT_EQ(CueSelector().Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, HitsConfirmedTogetherAreHeardAsOneHitMarker) {
  FrameInput input;
  input.hit_confirmations = 3;

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kHitMarker, .position = std::nullopt}};
  EXPECT_EQ(CueSelector().Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, FiringAloneIsNoHitMarker) {
  FrameInput input;
  input.local_entity = EntityId{1};
  input.shots = {Shot{.shooter = EntityId{1}, .origin = Vec3(0.0F, 1.7F, 0.0F), .yaw = 0.0F, .pitch = 0.0F}};

  // A round the prediction fired, and its Shot, but no Hit confirmation yet.
  for (const CuePlay& cue : CueSelector().Select(input, 1)) {
    EXPECT_NE(cue.cue, Cue::kHitMarker);
  }
}

// Requirements: US-18
TEST(CueSelectorTest, ADropInTheLocalPlayersHealthIsAHitTakenHeardAsTheListenersOwn) {
  CueSelector selector;
  FrameInput input;
  input.health = 100.0F;
  EXPECT_TRUE(selector.Select(input, 0).empty());

  input.health = 75.0F;

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kHitTaken, .position = std::nullopt}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, HealthThatHoldsOrStartsAMatchIsNoHitTaken) {
  CueSelector selector;
  FrameInput input;
  input.health = 40.0F;
  EXPECT_TRUE(selector.Select(input, 0).empty());
  EXPECT_TRUE(selector.Select(input, 0).empty());

  // Between matches there is no health, and the next starts afresh, even lower.
  input.health.reset();
  EXPECT_TRUE(selector.Select(input, 0).empty());
  input.health = 30.0F;
  EXPECT_TRUE(selector.Select(input, 0).empty());
}

// Requirements: US-18
TEST(CueSelectorTest, ADeathIsHeardFromWhereTheServerLastReportedTheBody) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  const WorldSnapshot listed{.tick = 10, .tick_duration = kTickDuration, .bodies = {BodyAt(EntityId{2}, kThere)}};
  input.snapshot = &listed;
  EXPECT_TRUE(selector.Select(input, 0).empty());

  // The body left the simulation, and the updates, before its Death arrived.
  const WorldSnapshot gone{.tick = 11, .tick_duration = kTickDuration, .bodies = {}};
  input.snapshot = &gone;
  input.deaths = {EntityId{2}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kDeath, .position = kThere}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, TheLocalPlayersOwnDeathIsHeardFromWhereItsBodyWas) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  const WorldSnapshot snapshot{.tick = 10, .tick_duration = kTickDuration, .bodies = {BodyAt(EntityId{1}, kThere)}};
  input.snapshot = &snapshot;
  EXPECT_TRUE(selector.Select(input, 0).empty());

  input.deaths = {EntityId{1}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kDeath, .position = kThere}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, TheWinnerHearsTheMatchWonStingerAsTheListenersOwnOnce) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  input.match_end = MatchEnd{.winner = EntityId{1}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kMatchWon, .position = std::nullopt}};
  EXPECT_EQ(selector.Select(input, 0), expected);
  // Match end stays told until the next match starts.
  EXPECT_TRUE(selector.Select(input, 0).empty());
}

// Requirements: US-18
TEST(CueSelectorTest, EveryoneButTheWinnerHearsTheMatchLostStinger) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  input.match_end = MatchEnd{.winner = EntityId{2}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kMatchLost, .position = std::nullopt}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, ADrawIsHeardAsTheMatchLostStinger) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  input.match_end = MatchEnd{.winner = std::nullopt};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kMatchLost, .position = std::nullopt}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, EachMatchsEndIsHeard) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  input.match_end = MatchEnd{.winner = EntityId{2}};
  (void)selector.Select(input, 0);

  input.match_end.reset();
  EXPECT_TRUE(selector.Select(input, 0).empty());

  input.match_end = MatchEnd{.winner = EntityId{2}};
  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kMatchLost, .position = std::nullopt}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
TEST(CueSelectorTest, TheDeathThatEndsTheMatchIsHeardWithItsStinger) {
  CueSelector selector;
  FrameInput input;
  input.local_entity = EntityId{1};
  const WorldSnapshot snapshot{.tick = 10, .tick_duration = kTickDuration, .bodies = {BodyAt(EntityId{2}, kThere)}};
  input.snapshot = &snapshot;
  (void)selector.Select(input, 0);

  // The match is over, so there is no update any more.
  input.snapshot = nullptr;
  input.deaths = {EntityId{2}};
  input.match_end = MatchEnd{.winner = EntityId{1}};

  const std::vector<CuePlay> expected = {CuePlay{.cue = Cue::kDeath, .position = kThere},
                                         CuePlay{.cue = Cue::kMatchWon, .position = std::nullopt}};
  EXPECT_EQ(selector.Select(input, 0), expected);
}

// Requirements: US-18
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
