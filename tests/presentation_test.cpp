#include "augusta/presentation.h"

#include <algorithm>
#include <chrono>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/audio.h"
#include "augusta/command.h"
#include "augusta/cues.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/tick.h"

// PresentationWorld through RunFrame, one render frame at a time. An
// audio::Engine without an output device is silent (audio.h), so none is
// needed; bodies stand still, so what a frame shows does not depend on how long
// it took.
namespace {

using augusta::math::Vec3;
using augusta::presentation::DynamicBody;
using augusta::presentation::EntityId;
using augusta::presentation::FrameInput;
using augusta::presentation::PlayerCharacter;
using augusta::presentation::RemotePlayer;
using augusta::presentation::Shot;
using augusta::presentation::State;
using augusta::presentation::WorldSnapshot;

constexpr float kTolerance = 1e-4F;
constexpr double kTickDuration = 1.0 / 60.0;
constexpr EntityId kLocal{10};
constexpr EntityId kSniper{30};
constexpr EntityId kMedic{70};
const Vec3 kEye(0.0F, 1.6F, 0.0F);

DynamicBody BodyAt(EntityId entity, float x) {
  DynamicBody body{.entity = entity, .state = {}, .yaw = 0.0F};
  body.state.position = Vec3(x, 0.0F, 0.0F);
  return body;
}

WorldSnapshot ThreePlayersAt(augusta::tick::Tick tick) {
  return {.tick = tick,
          .tick_duration = kTickDuration,
          .bodies = {BodyAt(kLocal, 0.0F), BodyAt(kMedic, -100.0F), BodyAt(kSniper, 100.0F)}};
}

// The match's players in Session order: the sniper's session joined first.
std::vector<PlayerCharacter> SessionOrder() {
  return {{.entity = kLocal, .character = "scout"},
          {.entity = kSniper, .character = "sniper"},
          {.entity = kMedic, .character = "medic"}};
}

// The frame of a match whose recent updates are updates, oldest first.
FrameInput InMatch(std::span<const WorldSnapshot> updates, const std::vector<PlayerCharacter>& characters) {
  FrameInput input;
  input.local_entity = kLocal;
  input.updates = updates;
  input.characters = characters;
  return input;
}

FrameInput InMatch(const WorldSnapshot& snapshot, const std::vector<PlayerCharacter>& characters) {
  return InMatch(std::span(&snapshot, 1), characters);
}

const RemotePlayer* Find(const State& state, EntityId entity) {
  const auto found =
      std::ranges::find_if(state.remote_players, [&](const RemotePlayer& remote) { return remote.entity == entity; });
  return found == state.remote_players.end() ? nullptr : &*found;
}

class PresentationWorldTest : public ::testing::Test {
 protected:
  augusta::audio::Engine audio;
  augusta::presentation::World world{audio, augusta::audio::CueSounds{}, kEye};
};

// Requirements: US-17
TEST_F(PresentationWorldTest, DrawsEachRemotePlayerAsTheCharacterMatchStartNamed) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();

  const State state = world.RunFrame(InMatch(snapshot, characters));

  ASSERT_EQ(state.remote_players.size(), 2U);
  ASSERT_NE(Find(state, kSniper), nullptr);
  EXPECT_EQ(Find(state, kSniper)->character, "sniper");
  ASSERT_NE(Find(state, kMedic), nullptr);
  EXPECT_EQ(Find(state, kMedic)->character, "medic");
}

// Requirements: US-13
TEST_F(PresentationWorldTest, ASpectatorWatchesTheLivingPlayersInSessionOrder) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  FrameInput died = InMatch(snapshot, characters);
  died.deaths = {kLocal};

  const State first = world.RunFrame(died);
  EXPECT_NEAR(first.camera.position.x, 100.0F, kTolerance);

  FrameInput fire = InMatch(snapshot, characters);
  fire.fire = true;
  const State next = world.RunFrame(fire);
  EXPECT_NEAR(next.camera.position.x, -100.0F, kTolerance);
}

// Another player's muzzle flash shows where its Shot left: the Muzzle of its
// rifle, which the server fires from (ADR-0064), not a point estimated from it.
// Requirements: US-19
TEST_F(PresentationWorldTest, AnotherPlayersMuzzleFlashShowsWhereItsShotLeft) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  FrameInput input = InMatch(snapshot, characters);
  const Vec3 muzzle(100.25F, 0.85F, -0.7F);
  input.shots = {Shot{.shooter = kSniper, .tick = 5, .origin = muzzle, .yaw = 0.5F, .pitch = 0.125F}};

  const State state = world.RunFrame(input);

  ASSERT_EQ(state.muzzle_flashes.size(), 1U);
  EXPECT_NEAR(state.muzzle_flashes.front().position.x, muzzle.x, kTolerance);
  EXPECT_NEAR(state.muzzle_flashes.front().position.y, muzzle.y, kTolerance);
  EXPECT_NEAR(state.muzzle_flashes.front().position.z, muzzle.z, kTolerance);
}

// The three players as a Replay viewer, which plays none of them, is shown them.
FrameInput Watching(const WorldSnapshot& snapshot, const std::vector<PlayerCharacter>& characters) {
  FrameInput input;
  input.replay_viewer = true;
  input.updates = std::span(&snapshot, 1);
  input.characters = characters;
  return input;
}

// A Replay viewer is a Spectator from the first tick: it watches the first
// player in Session order, its body drawn too, and moves on with fire.
// Requirements: US-21
TEST_F(PresentationWorldTest, AReplayViewerWatchesTheFirstPlayerFromTheStartAndMovesOnWithFire) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();

  const State first = world.RunFrame(Watching(snapshot, characters));
  EXPECT_NEAR(first.camera.position.x, 0.0F, kTolerance);
  EXPECT_EQ(first.remote_players.size(), 3U);
  EXPECT_FALSE(first.crosshair);
  EXPECT_FALSE(first.hit_marker);

  FrameInput fire = Watching(snapshot, characters);
  fire.fire = true;
  const State next = world.RunFrame(fire);
  EXPECT_NEAR(next.camera.position.x, 100.0F, kTolerance);
}

// The three players, the local and the sniper each looking and aiming as given.
WorldSnapshot ThreePlayersLooking(float local_pitch, bool local_ads, float sniper_pitch, bool sniper_ads) {
  WorldSnapshot snapshot = ThreePlayersAt(5);
  for (DynamicBody& body : snapshot.bodies) {
    if (body.entity == kLocal) {
      body.pitch = local_pitch;
      body.ads = local_ads;
    } else if (body.entity == kSniper) {
      body.pitch = sniper_pitch;
      body.ads = sniper_ads;
    }
  }
  return snapshot;
}

// The camera looks where the watched player looked, and zooms with its ADS,
// as the replayed state of the tick says (ADR-0066).
// Requirements: US-21
TEST_F(PresentationWorldTest, AReplayViewersCameraTakesTheWatchedPlayersPitchAndAdsZoom) {
  augusta::parameters::Parameters parameters;
  parameters.rifle.ads_field_of_view = 0.5F;
  world.SetParameters(parameters, static_cast<float>(kTickDuration));
  const WorldSnapshot snapshot = ThreePlayersLooking(0.25F, true, -0.5F, false);
  const std::vector<PlayerCharacter> characters = SessionOrder();

  // The zoom takes its time, as frames measure it: past AdsZoom's transition.
  State shown{};
  for (int frame = 0; frame < 12; ++frame) {
    shown = world.RunFrame(Watching(snapshot, characters));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  EXPECT_EQ(shown.camera.rotation, augusta::command::ViewRotation(0.0F, 0.25F));
  EXPECT_NEAR(shown.camera.vertical_fov, 0.5F, kTolerance);
}

// A dead player's own spectating, on a live server, looks where the watched
// player looks too, never where its own aim would: every Authoritative State
// carries each body's pitch and ADS (ADR-0066).
// Requirements: US-13
TEST_F(PresentationWorldTest, ASpectatorLooksAtTheWatchedPlayersPitchNotItsOwnAim) {
  const WorldSnapshot snapshot = ThreePlayersLooking(0.0F, false, -0.5F, false);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  FrameInput died = InMatch(snapshot, characters);
  died.deaths = {kLocal};
  died.aim = {.yaw = 1.0F, .pitch = 0.5F, .ads = true};

  const State state = world.RunFrame(died);

  EXPECT_EQ(state.camera.rotation, augusta::command::ViewRotation(0.0F, -0.5F));
  EXPECT_FLOAT_EQ(state.camera.vertical_fov, augusta::presentation::kHipFieldOfView);
}

// Requirements: US-17
TEST_F(PresentationWorldTest, RenderFramesWithoutNewerStateKeepShowingTheRemotePlayers) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();

  (void)world.RunFrame(InMatch(snapshot, characters));
  const State repeated = world.RunFrame(InMatch(snapshot, characters));
  const State again = world.RunFrame(InMatch(snapshot, characters));

  for (const State* state : {&repeated, &again}) {
    ASSERT_EQ(state->remote_players.size(), 2U);
    ASSERT_NE(Find(*state, kSniper), nullptr);
    EXPECT_NEAR(Find(*state, kSniper)->body.position.x, 100.0F, kTolerance);
    EXPECT_EQ(Find(*state, kSniper)->character, "sniper");
    EXPECT_TRUE(state->seen_time.has_value());
  }
}

TEST_F(PresentationWorldTest, KeepsNothingItWasLentPastTheRenderFrame) {
  State shown;
  {
    const WorldSnapshot snapshot = ThreePlayersAt(5);
    const std::vector<PlayerCharacter> characters = SessionOrder();
    shown = world.RunFrame(InMatch(snapshot, characters));
  }
  // What the first frame was lent is gone: the next is shown from its own, and
  // what the first returned is the caller's.
  const WorldSnapshot newer = ThreePlayersAt(6);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const State next = world.RunFrame(InMatch(newer, characters));

  ASSERT_NE(Find(shown, kMedic), nullptr);
  EXPECT_EQ(Find(shown, kMedic)->character, "medic");
  ASSERT_NE(Find(next, kMedic), nullptr);
  EXPECT_EQ(Find(next, kMedic)->character, "medic");
}

// Requirements: US-17
TEST_F(PresentationWorldTest, OutsideAMatchNoRemotePlayerIsShown) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  (void)world.RunFrame(InMatch(snapshot, characters));

  FrameInput ended;
  ended.local_entity = kLocal;
  ended.characters = characters;
  const State state = world.RunFrame(ended);

  EXPECT_TRUE(state.remote_players.empty());
  EXPECT_FALSE(state.seen_time.has_value());
}

// Updates a second apart, so each realigns the frame's clock to it
// (ServerClock::kResyncThreshold): a frame samples exactly kInterpolationDelay
// before the newest it recorded, however long it took.
constexpr double kSlowTickDuration = 1.0;

// The sniper at x on tick, a slow tick apart from the next.
WorldSnapshot SniperAt(augusta::tick::Tick tick, float x) {
  return {.tick = tick, .tick_duration = kSlowTickDuration, .bodies = {BodyAt(kLocal, 0.0F), BodyAt(kSniper, x)}};
}

// The sniper holds still on ticks 0 to 2 and steps 10 m on tick 3: shown
// 0.1 s before tick 3, it is 9 m along only if tick 2 was recorded too, and
// 9.67 m along if the frame had cut from tick 0 straight to tick 3.
// Requirements: US-17
TEST_F(PresentationWorldTest, AFrameGivenSeveralNewUpdatesInterpolatesThroughEachOfThem) {
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const std::vector<WorldSnapshot> first = {SniperAt(0, 0.0F)};
  (void)world.RunFrame(InMatch(first, characters));

  const std::vector<WorldSnapshot> since = {SniperAt(0, 0.0F), SniperAt(1, 0.0F), SniperAt(2, 0.0F),
                                            SniperAt(3, 10.0F)};
  const State state = world.RunFrame(InMatch(since, characters));

  ASSERT_NE(Find(state, kSniper), nullptr);
  EXPECT_NEAR(Find(state, kSniper)->body.position.x, 9.0F, kTolerance);
}

// Updates a frame already recorded are not recorded again, whatever they say:
// the sniper is still shown between tick 2's 0 m and tick 3's 10 m.
// Requirements: US-17
TEST_F(PresentationWorldTest, AFrameGivenOnlyUpdatesItRecordedChangesNothing) {
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const std::vector<WorldSnapshot> recorded = {SniperAt(1, 0.0F), SniperAt(2, 0.0F), SniperAt(3, 10.0F)};
  (void)world.RunFrame(InMatch(recorded, characters));

  const std::vector<WorldSnapshot> again = {SniperAt(2, 500.0F), SniperAt(3, 500.0F)};
  const State state = world.RunFrame(InMatch(again, characters));

  ASSERT_NE(Find(state, kSniper), nullptr);
  EXPECT_GE(Find(state, kSniper)->body.position.x, 9.0F - kTolerance);
  EXPECT_LE(Find(state, kSniper)->body.position.x, 10.0F + kTolerance);
}

// The first frame of a match is given three updates: it shows the sniper 0.1 s
// before the newest, between the two before it, and reports that as its Seen
// time - not the newest alone, had it recorded only that.
// Requirements: US-17
TEST_F(PresentationWorldTest, TheSeenTimeIsOfTheUpdatesTheFrameRecorded) {
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const std::vector<WorldSnapshot> updates = {SniperAt(1, 0.0F), SniperAt(2, 0.0F), SniperAt(3, 10.0F)};

  const State state = world.RunFrame(InMatch(updates, characters));

  ASSERT_TRUE(state.seen_time.has_value());
  EXPECT_EQ(state.seen_time->tick, 2U);
  EXPECT_NEAR(state.seen_time->fraction, 0.9F, kTolerance);
}

// The frame samples 0.1 s before tick 3's 3 s, where the newest update is.
TEST_F(PresentationWorldTest, WithUpdatesWaitingTheSlackIsTheNewestUpdatesLead) {
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const std::vector<WorldSnapshot> updates = {SniperAt(1, 0.0F), SniperAt(2, 0.0F), SniperAt(3, 10.0F)};

  const State state = world.RunFrame(InMatch(updates, characters));

  ASSERT_TRUE(state.interpolation_slack.has_value());
  EXPECT_NEAR(*state.interpolation_slack, 0.1, 1e-9);
}

// Frames run on, well past kInterpolationDelay, with no newer update.
TEST_F(PresentationWorldTest, PastTheNewestUpdateTheSlackIsZeroOrBelow) {
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const std::vector<WorldSnapshot> updates = {SniperAt(3, 10.0F)};
  const State waiting = world.RunFrame(InMatch(updates, characters));
  ASSERT_TRUE(waiting.interpolation_slack.has_value());
  EXPECT_GT(*waiting.interpolation_slack, 0.0);

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const State dry = world.RunFrame(InMatch(updates, characters));

  ASSERT_TRUE(dry.interpolation_slack.has_value());
  EXPECT_LE(*dry.interpolation_slack, 0.0);
}

TEST_F(PresentationWorldTest, OutsideAMatchThereIsNoSlack) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const State in_match = world.RunFrame(InMatch(snapshot, characters));
  EXPECT_TRUE(in_match.interpolation_slack.has_value());

  FrameInput ended;
  ended.local_entity = kLocal;
  ended.characters = characters;
  const State state = world.RunFrame(ended);

  EXPECT_FALSE(state.interpolation_slack.has_value());
}

// The sniper's update is the control: the same frame with someone to show.
TEST_F(PresentationWorldTest, WithNoRemotePlayerThereIsNoSlack) {
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const WorldSnapshot with_sniper = SniperAt(3, 10.0F);
  const WorldSnapshot alone = {.tick = 3, .tick_duration = kSlowTickDuration, .bodies = {BodyAt(kLocal, 0.0F)}};
  augusta::presentation::World control{audio, augusta::audio::CueSounds{}, kEye};
  EXPECT_TRUE(control.RunFrame(InMatch(with_sniper, characters)).interpolation_slack.has_value());

  const State state = world.RunFrame(InMatch(alone, characters));

  EXPECT_FALSE(state.interpolation_slack.has_value());
}

}  // namespace
