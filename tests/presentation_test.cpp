#include "augusta/presentation.h"

#include <algorithm>
#include <chrono>
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
using augusta::presentation::PlayerView;
using augusta::presentation::RemotePlayer;
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

FrameInput InMatch(const WorldSnapshot& snapshot, const std::vector<PlayerCharacter>& characters) {
  FrameInput input;
  input.local_entity = kLocal;
  input.snapshot = &snapshot;
  input.characters = characters;
  return input;
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

// The three players as a Replay viewer, which plays none of them, is shown them.
FrameInput Watching(const WorldSnapshot& snapshot, const std::vector<PlayerCharacter>& characters,
                    const std::vector<PlayerView>& views) {
  FrameInput input;
  input.replay_viewer = true;
  input.snapshot = &snapshot;
  input.characters = characters;
  input.views = views;
  return input;
}

// A Replay viewer is a Spectator from the first tick: it watches the first
// player in Session order, its body drawn too, and moves on with fire.
// Requirements: US-21
TEST_F(PresentationWorldTest, AReplayViewerWatchesTheFirstPlayerFromTheStartAndMovesOnWithFire) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();

  const State first = world.RunFrame(Watching(snapshot, characters, {}));
  EXPECT_NEAR(first.camera.position.x, 0.0F, kTolerance);
  EXPECT_EQ(first.remote_players.size(), 3U);
  EXPECT_FALSE(first.crosshair);
  EXPECT_FALSE(first.hit_marker);

  FrameInput fire = Watching(snapshot, characters, {});
  fire.fire = true;
  const State next = world.RunFrame(fire);
  EXPECT_NEAR(next.camera.position.x, 100.0F, kTolerance);
}

// The camera looks where the watched player looked, and zooms with its ADS,
// as the Replay view of the tick says (ADR-0051).
// Requirements: US-21
TEST_F(PresentationWorldTest, AReplayViewersCameraTakesTheWatchedPlayersPitchAndAdsZoom) {
  augusta::parameters::Parameters parameters;
  parameters.rifle.ads_field_of_view = 0.5F;
  world.SetParameters(parameters, static_cast<float>(kTickDuration));
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  const std::vector<PlayerView> views = {{.entity = kLocal, .pitch = 0.25F, .ads = true},
                                         {.entity = kSniper, .pitch = -0.5F, .ads = false}};

  // The zoom takes its time, as frames measure it: past AdsZoom's transition.
  State shown{};
  for (int frame = 0; frame < 12; ++frame) {
    shown = world.RunFrame(Watching(snapshot, characters, views));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  EXPECT_EQ(shown.camera.rotation, augusta::command::ViewRotation(0.0F, 0.25F));
  EXPECT_NEAR(shown.camera.vertical_fov, 0.5F, kTolerance);
}

// A dead player's own spectating, on a live server, still looks level from the
// hip: no Replay view is sent to a player.
// Requirements: US-13
TEST_F(PresentationWorldTest, ASpectatorWithoutAReplayViewLooksLevelFromTheHip) {
  const WorldSnapshot snapshot = ThreePlayersAt(5);
  const std::vector<PlayerCharacter> characters = SessionOrder();
  FrameInput died = InMatch(snapshot, characters);
  died.deaths = {kLocal};
  died.aim = {.yaw = 1.0F, .pitch = 0.5F, .ads = true};

  const State state = world.RunFrame(died);

  EXPECT_EQ(state.camera.rotation, augusta::command::ViewRotation(0.0F, 0.0F));
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

}  // namespace
