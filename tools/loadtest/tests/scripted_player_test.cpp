#include "scripted_player.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// What a Scripted player decides to do each tick, from the Server view alone.
namespace {

using augusta::command::Command;
using augusta::command::ViewDirection;
using augusta::harness::EntityId;
using augusta::harness::ServerView;
using augusta::harness::SessionId;
using augusta::loadtest::ScriptedPlayer;
using augusta::math::Vec3;

constexpr std::uint8_t kTickRate = 60;
constexpr EntityId kOwn{1};
constexpr EntityId kNear{2};
constexpr EntityId kFar{3};
constexpr std::uint32_t kSeed = 7;
// Enough ticks for several legs and bursts.
constexpr int kManyTicks = 10 * kTickRate;

// A view of a match in progress where this player's body is at own, with a
// full magazine, and every other body is where others puts it.
ServerView InMatch(Vec3 own, const std::vector<std::pair<EntityId, Vec3>>& others) {
  ServerView view;
  view.accepted = augusta::harness::Admission{
      .session = SessionId{1}, .tick_rate_hz = kTickRate, .parameters = {}, .character = "characters/player"};
  view.match_start = augusta::harness::MatchStart{
      .players = {{.session = SessionId{1}, .entity = kOwn, .character = "characters/player", .spawn = own}}};
  view.matches_started = 1;
  view.in_match = true;
  augusta::harness::AuthoritativeState state;
  state.tick = 100;
  state.health = 100.0F;
  state.rifle.rounds = 30;
  state.bodies.push_back({.entity = kOwn, .body = {.position = own}, .yaw = 0.0F});
  for (const auto& [entity, position] : others) {
    state.bodies.push_back({.entity = entity, .body = {.position = position}, .yaw = 0.0F});
  }
  view.authoritative = state;
  return view;
}

// The horizontal part of v, normalized.
Vec3 Horizontal(Vec3 v) { return augusta::math::Normalize(Vec3(v.x, 0.0F, v.z)); }

TEST(ScriptedPlayerTest, InTheLobbyItHoldsNothing) {
  ScriptedPlayer player(kSeed);
  ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});
  view.in_match = false;

  const Command command = player.NextCommand(view);

  EXPECT_FALSE(command.fire);
  EXPECT_FALSE(command.reload);
  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
}

TEST(ScriptedPlayerTest, OnceDeadItHoldsNothing) {
  ScriptedPlayer player(kSeed);
  ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});
  view.dead = {kOwn};

  const Command command = player.NextCommand(view);

  EXPECT_FALSE(command.fire);
  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
}

TEST(ScriptedPlayerTest, AimsAtTheNearestOtherPlayer) {
  ScriptedPlayer player(kSeed);
  const ServerView view = InMatch(Vec3(0.0F), {{kFar, Vec3(30.0F, 0.0F, 0.0F)}, {kNear, Vec3(0.0F, 0.0F, -10.0F)}});

  const Command command = player.NextCommand(view);

  const Vec3 aim = Horizontal(ViewDirection(command.yaw, command.pitch));
  EXPECT_NEAR(aim.x, 0.0F, 1e-3F);
  EXPECT_NEAR(aim.z, -1.0F, 1e-3F);
}

TEST(ScriptedPlayerTest, AimsPastADeadPlayerAtTheNearestLivingOne) {
  ScriptedPlayer player(kSeed);
  ServerView view = InMatch(Vec3(0.0F), {{kFar, Vec3(30.0F, 0.0F, 0.0F)}, {kNear, Vec3(0.0F, 0.0F, -10.0F)}});
  view.dead = {kNear};

  const Command command = player.NextCommand(view);

  const Vec3 aim = Horizontal(ViewDirection(command.yaw, command.pitch));
  EXPECT_NEAR(aim.x, 1.0F, 1e-3F);
  EXPECT_NEAR(aim.z, 0.0F, 1e-3F);
}

TEST(ScriptedPlayerTest, AimsUpAtAPlayerAboveItAndDownAtOneBelow) {
  ScriptedPlayer above(kSeed);
  ScriptedPlayer below(kSeed);

  const Command up = above.NextCommand(InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 10.0F, -10.0F)}}));
  const Command down = below.NextCommand(InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, -10.0F, -10.0F)}}));

  EXPECT_GT(up.pitch, 0.0F);
  EXPECT_LT(down.pitch, 0.0F);
}

TEST(ScriptedPlayerTest, FiresAtATargetInBurstsItReleasesBetween) {
  ScriptedPlayer player(kSeed);
  const ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});

  int bursts = 0;
  int ticks_firing = 0;
  bool was_firing = false;
  for (int tick = 0; tick < kManyTicks; ++tick) {
    const bool firing = player.NextCommand(view).fire;
    bursts += firing && !was_firing ? 1 : 0;
    ticks_firing += firing ? 1 : 0;
    was_firing = firing;
  }

  EXPECT_GT(bursts, 1);
  EXPECT_LT(ticks_firing, kManyTicks);
}

TEST(ScriptedPlayerTest, NeverFiresWithNoOneToAimAt) {
  ScriptedPlayer player(kSeed);
  const ServerView view = InMatch(Vec3(0.0F), {});

  for (int tick = 0; tick < kManyTicks; ++tick) {
    ASSERT_FALSE(player.NextCommand(view).fire) << tick;
  }
}

TEST(ScriptedPlayerTest, ReloadsAnEmptyMagazineInsteadOfFiring) {
  ScriptedPlayer player(kSeed);
  ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});
  view.authoritative->rifle.rounds = 0;

  const Command command = player.NextCommand(view);

  EXPECT_TRUE(command.reload);
  EXPECT_FALSE(command.fire);
}

TEST(ScriptedPlayerTest, DoesNotPressReloadAgainWhileAReloadIsUnderWay) {
  ScriptedPlayer player(kSeed);
  ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});
  view.authoritative->rifle.rounds = 0;
  view.authoritative->rifle.reload_remaining = 1.0F;

  EXPECT_FALSE(player.NextCommand(view).reload);
}

TEST(ScriptedPlayerTest, ReportsTheNewestAuthoritativeStateAsItsSeenTime) {
  ScriptedPlayer player(kSeed);
  const ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});

  const Command command = player.NextCommand(view);

  EXPECT_EQ(command.seen_tick, view.authoritative->tick);
  EXPECT_FLOAT_EQ(command.seen_fraction, 0.0F);
}

TEST(ScriptedPlayerTest, WandersInSeveralDirectionsWithinWhatTheServerAccepts) {
  ScriptedPlayer player(kSeed);
  const ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});

  std::set<std::pair<float, float>> directions;
  for (int tick = 0; tick < kManyTicks; ++tick) {
    const Command command = player.NextCommand(view);
    ASSERT_LE(augusta::math::Length(command.movement.direction), 1.0F + 1e-5F) << tick;
    ASSERT_LE(std::fabs(command.yaw), std::numbers::pi_v<float>) << tick;
    ASSERT_LE(std::fabs(command.pitch), augusta::input::kMaxLookPitch) << tick;
    directions.emplace(command.movement.direction.x, command.movement.direction.z);
  }

  EXPECT_GT(directions.size(), 2U);
}

TEST(ScriptedPlayerTest, TheSameSeedMakesTheSameCommandsAndAnotherSeedOthers) {
  ScriptedPlayer first(kSeed);
  ScriptedPlayer again(kSeed);
  ScriptedPlayer other(kSeed + 1);
  const ServerView view = InMatch(Vec3(0.0F), {{kNear, Vec3(0.0F, 0.0F, -10.0F)}});

  bool differs = false;
  for (int tick = 0; tick < kManyTicks; ++tick) {
    const Command a = first.NextCommand(view);
    const Command b = again.NextCommand(view);
    const Command c = other.NextCommand(view);
    ASSERT_EQ(a.movement.direction, b.movement.direction) << tick;
    ASSERT_EQ(a.movement.sprint, b.movement.sprint) << tick;
    ASSERT_EQ(a.movement.desired_stance, b.movement.desired_stance) << tick;
    ASSERT_EQ(a.fire, b.fire) << tick;
    differs = differs || a.movement.direction != c.movement.direction;
  }

  EXPECT_TRUE(differs);
}

}  // namespace
