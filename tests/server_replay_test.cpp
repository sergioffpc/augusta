#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/animation.h"
#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/command.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy.h"
#include "augusta/pose.h"
#include "augusta/replay.h"
#include "augusta/simulation.h"
#include "character_fixture.h"
#include "clips_fixture.h"

// A Replay (ADR-0051) re-run on a SimulationWorld with no Map, from captures
// made by hand: what it hands the World, tick by tick, and what it compares
// with the capture's markers (NFR-09). The whole path, from a live Host's
// capture to a viewer, is replay_server_test.cpp's.
namespace {

using augusta::command::Command;
using augusta::host::Divergence;
using augusta::host::EntityId;
using augusta::host::Replay;
using augusta::host::ReplayCheck;
using augusta::host::ReplayTick;
using augusta::host::SessionId;
using augusta::math::Vec3;
using augusta::simulation::Character;

namespace capture = augusta::capture;

// An event of a capture, at its offset.
using Captured = capture::AtOffset<capture::Event>;

// A Session ID, a UUIDv7, told apart by its last byte.
constexpr SessionId Session(std::uint8_t last) {
  return SessionId({0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, last});
}

constexpr std::uint8_t kTickRate = 60;
constexpr const char* kCharacter = "soldier";

// Two players, sessions 7 and 9, apart on the X axis, and then events.
capture::Capture CaptureOf(std::vector<Captured> events) {
  capture::Capture capture;
  capture.header.tick_rate_hz = kTickRate;
  capture.joins = {{.session = Session(7), .character = kCharacter, .spawn = Vec3(10.0F, 0.0F, 0.0F)},
                   {.session = Session(9), .character = kCharacter, .spawn = Vec3(-10.0F, 0.0F, 4.0F)}};
  capture.events = std::move(events);
  return capture;
}

Captured CommandAt(std::uint32_t offset, capture::Player player, const Command& command, std::int32_t seen_offset = 0) {
  return Captured{.offset = offset,
                  .event = capture::Command{.player = player, .seen_offset = seen_offset, .command = command}};
}

Captured EndAt(std::uint32_t offset, std::optional<capture::Player> winner = std::nullopt) {
  return Captured{.offset = offset, .event = capture::MatchEnd{.winner = winner}};
}

const std::unordered_map<std::string, Character>& Characters() {
  static const std::unordered_map<std::string, Character> characters = {
      {kCharacter, augusta::character_fixture::Rigged(Character{.eye = Vec3(0.0F, 1.6F, 0.0F), .hitboxes = {}})}};
  return characters;
}

augusta::policy::Policy Rules(const char* rules) {
  auto policy = augusta::policy::Policy::Load(rules);
  EXPECT_TRUE(policy.has_value());
  return policy ? *std::move(policy) : augusta::policy::Policy{};
}

Replay ReplayOf(capture::Capture capture, augusta::policy::Policy policy = {}) {
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  return Replay(std::move(capture), augusta::simulation::World(parameters, kTickRate, std::move(policy)), Characters());
}

// Steps replay until it has ended, at most limit ticks; returns every tick.
std::vector<ReplayTick> RunToEnd(Replay& replay, std::size_t limit = 1000) {
  std::vector<ReplayTick> ticks;
  while (!replay.Ended() && ticks.size() < limit) {
    ticks.push_back(replay.Step());
  }
  return ticks;
}

const augusta::simulation::EntityState* BodyOf(const ReplayTick& tick, std::uint32_t entity) {
  for (const augusta::simulation::EntityState& body : tick.result.state.bodies) {
    if (static_cast<std::uint32_t>(body.entity) == entity) {
      return &body;
    }
  }
  return nullptr;
}

// Requirements: US-21
TEST(CaptureReplayTest, TheMatchStartsWithTheCapturesPlayersWhereItHasThemWithoutAskingPolicy) {
  // Rules that would swap the two spawns, were they asked.
  Replay replay = ReplayOf(CaptureOf({EndAt(3)}), Rules(R"(
    function assign_spawns(match)
      return {{session = match.players[1].session, spawn_point = 2},
              {session = match.players[2].session, spawn_point = 1}}
    end
  )"));

  ASSERT_EQ(replay.Start().players.size(), 2U);
  EXPECT_EQ(replay.Start().players[0].session, Session(7));
  EXPECT_EQ(replay.Start().players[0].entity, EntityId{1});
  EXPECT_EQ(replay.Start().players[1].session, Session(9));
  EXPECT_EQ(replay.Start().players[1].character, kCharacter);
  EXPECT_EQ(replay.Spawns(), (std::vector<Vec3>{Vec3(10.0F, 0.0F, 0.0F), Vec3(-10.0F, 0.0F, 4.0F)}));
  EXPECT_EQ(replay.FirstTick(), 1U);

  const ReplayTick first = replay.Step();
  EXPECT_EQ(first.result.state.tick, replay.FirstTick());
  ASSERT_NE(BodyOf(first, 1), nullptr);
  EXPECT_NEAR(BodyOf(first, 1)->body.position.x, 10.0F, 0.01F);
  EXPECT_NEAR(BodyOf(first, 2)->body.position.z, 4.0F, 0.01F);
}

// The queue holds a player's last Command over a tick the capture has nothing
// new for, as the live server's did: the body keeps its pitch and ADS.
// Requirements: US-21
TEST(CaptureReplayTest, ATickWithNothingNewHoldsTheLastCommandAsTheLiveQueueDid) {
  Command aiming;
  aiming.pitch = 0.5F;
  aiming.ads = true;
  aiming.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  Replay replay = ReplayOf(CaptureOf({CommandAt(0, 1, aiming), EndAt(40)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 41U);
  for (const ReplayTick& tick : ticks) {
    ASSERT_NE(BodyOf(tick, 1), nullptr);
    EXPECT_FLOAT_EQ(BodyOf(tick, 1)->pitch, 0.5F);
    EXPECT_TRUE(BodyOf(tick, 1)->ads);
    EXPECT_FLOAT_EQ(BodyOf(tick, 2)->pitch, 0.0F);
    EXPECT_FALSE(BodyOf(tick, 2)->ads);
  }
  // The movement is held for kMaxHeldTime, then the body stops, as live.
  EXPECT_GT(BodyOf(ticks[10], 1)->body.position.x, 10.0F);
  EXPECT_FLOAT_EQ(BodyOf(ticks.back(), 1)->body.position.x, BodyOf(ticks[ticks.size() - 2], 1)->body.position.x);
}

// A Command the capture has on a tick reaches the World on that tick, and the
// state the viewer is shown carries its pitch.
// Requirements: US-21
TEST(CaptureReplayTest, EachCapturedCommandReachesTheWorldOnItsTick) {
  Command up;
  up.pitch = 0.25F;
  Command down;
  down.pitch = -0.75F;
  Replay replay = ReplayOf(CaptureOf({CommandAt(2, 2, up), CommandAt(5, 2, down), EndAt(6)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 7U);
  EXPECT_FLOAT_EQ(BodyOf(ticks[1], 2)->pitch, 0.0F);
  EXPECT_FLOAT_EQ(BodyOf(ticks[2], 2)->pitch, 0.25F);
  EXPECT_FLOAT_EQ(BodyOf(ticks[4], 2)->pitch, 0.25F);
  EXPECT_FLOAT_EQ(BodyOf(ticks[5], 2)->pitch, -0.75F);
}

// A Replay re-runs the captured Commands on the Characters' Clips, so each
// body's Stride is the one the live Match's World advanced (ADR-0066): the same
// as a World given those Commands on those ticks by hand, the last held over
// the Match's last tick as the live queue held it.
// Requirements: US-21
TEST(CaptureReplayTest, AReplayReproducesEachBodysStride) {
  Command walking;
  walking.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
  constexpr std::uint32_t kWalkTicks = 20;
  std::vector<Captured> events;
  for (std::uint32_t offset = 0; offset < kWalkTicks; ++offset) {
    events.push_back(CommandAt(offset, 1, walking));
  }
  events.push_back(EndAt(kWalkTicks));
  const Character& walker = Characters().at(kCharacter);
  const std::unordered_map<std::string, Character> characters = {{kCharacter, walker}};
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  Replay replay(CaptureOf(std::move(events)), augusta::simulation::World(parameters, kTickRate), characters);
  augusta::simulation::World live(parameters, kTickRate);
  live.AddPlayer(augusta::simulation::EntityId{1}, Vec3(10.0F, 0.0F, 0.0F), walker);
  live.AddPlayer(augusta::simulation::EntityId{2}, Vec3(-10.0F, 0.0F, 4.0F), walker);

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), kWalkTicks + 1);
  for (std::uint32_t i = 0; i < ticks.size(); ++i) {
    const augusta::simulation::State state =
        live.Tick({{.entity = augusta::simulation::EntityId{1}, .command = walking}}, 1.0F / kTickRate).state;
    ASSERT_NE(BodyOf(ticks[i], 1), nullptr);
    EXPECT_EQ(BodyOf(ticks[i], 1)->body.stride, state.bodies.at(0).body.stride) << "tick " << i;
  }
  EXPECT_NE(BodyOf(ticks.back(), 1)->body.stride, 0);
}

// A World with a floor 1 m below the capture's spawns, which its bodies fall to.
augusta::simulation::World WithAFloorBelowTheSpawns(const augusta::parameters::Parameters& parameters) {
  constexpr float kExtent = 50.0F;
  constexpr float kY = -1.0F;
  augusta::simulation::World world(parameters, kTickRate);
  EXPECT_TRUE(world
                  .AddCollisionMesh({.points = {Vec3(-kExtent, kY, -kExtent), Vec3(-kExtent, kY, kExtent),
                                                Vec3(kExtent, kY, kExtent), Vec3(kExtent, kY, -kExtent)},
                                     .indices = {0, 1, 2, 0, 2, 3}})
                  .has_value());
  return world;
}

// The Commands re-run put each body on the ground on the tick they did live,
// so kGrounded, and the air Clip it poses a body by, comes back with them.
// Requirements: US-21
TEST(CaptureReplayTest, AReplayReproducesWhenEachBodyIsOnTheGround) {
  constexpr std::uint32_t kTicks = 60;
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  Replay replay(CaptureOf({EndAt(kTicks)}), WithAFloorBelowTheSpawns(parameters), Characters());
  augusta::simulation::World live = WithAFloorBelowTheSpawns(parameters);
  live.AddPlayer(augusta::simulation::EntityId{1}, Vec3(10.0F, 0.0F, 0.0F), Characters().at(kCharacter));
  live.AddPlayer(augusta::simulation::EntityId{2}, Vec3(-10.0F, 0.0F, 4.0F), Characters().at(kCharacter));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), kTicks + 1);
  for (std::uint32_t i = 0; i < ticks.size(); ++i) {
    const augusta::simulation::State state = live.Tick({}, 1.0F / kTickRate).state;
    ASSERT_NE(BodyOf(ticks[i], 1), nullptr);
    EXPECT_EQ(BodyOf(ticks[i], 1)->body.grounded, state.bodies.at(0).body.grounded) << "tick " << i;
  }
  EXPECT_FALSE(BodyOf(ticks.front(), 1)->body.grounded);
  EXPECT_TRUE(BodyOf(ticks.back(), 1)->body.grounded);
}

// As the Stride, a change of stance's progress is re-advanced by the Commands
// on the Characters' Clips: a body crouching from offset 3 is posed between
// standing and crouching tick by tick as the live Match's.
// Requirements: US-21
TEST(CaptureReplayTest, AReplayReproducesEachBodysChangeOfStance) {
  Command crouching;
  crouching.movement.desired_stance = augusta::physics::Stance::kCrouching;
  constexpr std::uint32_t kTicks = 20;
  constexpr std::uint32_t kCrouchFrom = 3;
  std::vector<Captured> events;
  for (std::uint32_t offset = 0; offset < kTicks; ++offset) {
    events.push_back(CommandAt(offset, 1, offset < kCrouchFrom ? Command{} : crouching));
  }
  events.push_back(EndAt(kTicks));
  const Character& crouch = Characters().at(kCharacter);
  const std::unordered_map<std::string, Character> characters = {{kCharacter, crouch}};
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  Replay replay(CaptureOf(std::move(events)), augusta::simulation::World(parameters, kTickRate), characters);
  augusta::simulation::World live(parameters, kTickRate);
  live.AddPlayer(augusta::simulation::EntityId{1}, Vec3(10.0F, 0.0F, 0.0F), crouch);
  live.AddPlayer(augusta::simulation::EntityId{2}, Vec3(-10.0F, 0.0F, 4.0F), crouch);

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), kTicks + 1);
  bool under_way = false;
  for (std::uint32_t i = 0; i < ticks.size(); ++i) {
    const Command command = i < kCrouchFrom ? Command{} : crouching;
    const augusta::simulation::State state =
        live.Tick({{.entity = augusta::simulation::EntityId{1}, .command = command}}, 1.0F / kTickRate).state;
    ASSERT_NE(BodyOf(ticks[i], 1), nullptr);
    const augusta::physics::BodyState& replayed = BodyOf(ticks[i], 1)->body;
    EXPECT_EQ(replayed.previous_stance, state.bodies.at(0).body.previous_stance) << "tick " << i;
    EXPECT_EQ(replayed.stance_progress, state.bodies.at(0).body.stance_progress) << "tick " << i;
    under_way = under_way || replayed.stance_progress < augusta::physics::kStanceProgressSteps;
  }
  EXPECT_TRUE(under_way);
}

// A body's Hitbox pose as the Posing phase poses it from its state: its Clips,
// then its rifle held.
augusta::animation::SkeletonPose PoseOf(const Character& character, const augusta::simulation::EntityState& body) {
  augusta::animation::Poser poser;
  augusta::animation::SkeletonPose posed;
  poser.Pose(*character.clips, character.skeleton,
             {.body = body.body, .yaw = body.yaw, .pitch = body.pitch, .ads = body.ads}, posed);
  augusta::animation::HoldRifle(character.skeleton, character.clips->weapon_socket,
                                augusta::clips_fixture::HolderRifle(), posed);
  return posed;
}

// The pose is a pure function of the Authoritative State (ADR-0061), every
// input of which the Commands re-run reproduce: a body walking, crouching,
// sweeping its view and raising its rifle to ADS is posed tick by tick as the
// live Match posed it, Joint for Joint, its left hand on the rifle by IK.
// Requirements: US-21
TEST(CaptureReplayTest, AReplayReproducesThePosesOfTheMatch) {
  constexpr std::uint32_t kTicks = 40;
  std::vector<Command> commands;
  std::vector<Captured> events;
  for (std::uint32_t offset = 0; offset < kTicks; ++offset) {
    Command command;
    command.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
    command.movement.desired_stance =
        offset >= kTicks / 2 ? augusta::physics::Stance::kCrouching : augusta::physics::Stance::kStanding;
    command.pitch = -1.0F + (0.05F * static_cast<float>(offset));
    command.ads = offset >= kTicks / 4;
    commands.push_back(command);
    events.push_back(CommandAt(offset, 1, command));
  }
  events.push_back(EndAt(kTicks));
  const Character holder{.eye = Vec3(0.0F, 1.7F, 0.0F),
                         .hitboxes = {},
                         .skeleton = augusta::clips_fixture::HolderSkeleton(),
                         .clips = augusta::clips_fixture::HolderClips()};
  const std::unordered_map<std::string, Character> characters = {{kCharacter, holder}};
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  Replay replay(CaptureOf(std::move(events)), augusta::simulation::World(parameters, kTickRate), characters);
  augusta::simulation::World live(parameters, kTickRate);
  live.AddPlayer(augusta::simulation::EntityId{1}, Vec3(10.0F, 0.0F, 0.0F), holder);
  live.AddPlayer(augusta::simulation::EntityId{2}, Vec3(-10.0F, 0.0F, 4.0F), holder);

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), kTicks + 1);
  for (std::uint32_t i = 0; i < ticks.size(); ++i) {
    const Command& command = commands.at(std::min<std::size_t>(i, commands.size() - 1));
    const augusta::simulation::State state =
        live.Tick({{.entity = augusta::simulation::EntityId{1}, .command = command}}, 1.0F / kTickRate).state;
    ASSERT_NE(BodyOf(ticks[i], 1), nullptr);
    EXPECT_EQ(PoseOf(holder, *BodyOf(ticks[i], 1)).world, PoseOf(holder, state.bodies.at(0)).world) << "tick " << i;
  }
}

// A round fired at offset 10 whose Seen time was 2 ticks older is judged 2
// ticks back on the Replay too: the Seen time's tick moves with the Command's.
// Requirements: US-21
TEST(CaptureReplayTest, ASeenTimeKeepsItsCapturedDelay) {
  Command fire;
  fire.fire = true;
  Replay replay = ReplayOf(CaptureOf({CommandAt(10, 1, fire, 8), EndAt(12)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 13U);
  ASSERT_EQ(ticks[10].result.shooters_delays.size(), 1U);
  EXPECT_NEAR(ticks[10].result.shooters_delays.front(), 2.0F / kTickRate, 1e-4F);
}

// Requirements: US-21
TEST(CaptureReplayTest, APlayerWhoLeftIsTakenOutBeforeTheTickOfItsLeave) {
  Replay replay = ReplayOf(CaptureOf({Captured{.offset = 3, .event = capture::Leave{.player = 2}}, EndAt(5)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 6U);
  EXPECT_NE(BodyOf(ticks[2], 2), nullptr);
  EXPECT_EQ(BodyOf(ticks[3], 2), nullptr);
  EXPECT_NE(BodyOf(ticks[3], 1), nullptr);
}

// The live server takes out the bodies of those who left an ended Match after
// its last tick: their Leaves on that tick do not take them out before it.
// Requirements: US-21
TEST(CaptureReplayTest, ALeaveAfterTheLastTickLeavesThatTickAsItRan) {
  Replay replay = ReplayOf(CaptureOf({Captured{.offset = 4, .event = capture::Leave{.player = 2}}, EndAt(4)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 5U);
  EXPECT_NE(BodyOf(ticks.back(), 2), nullptr);
}

// Requirements: US-21, NFR-09
TEST(CaptureReplayTest, ItEndsAtTheCapturesMatchEndWithItsWinner) {
  Replay replay = ReplayOf(CaptureOf({EndAt(5, 2)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 6U);
  EXPECT_TRUE(replay.Ended());
  ASSERT_TRUE(ticks.back().end.has_value());
  EXPECT_EQ(ticks.back().end->winner, Session(9));
  for (std::size_t i = 0; i + 1 < ticks.size(); ++i) {
    EXPECT_FALSE(ticks[i].end.has_value());
  }
  // The World did not end it: the check says so.
  ASSERT_TRUE(ticks.back().divergence.has_value());
  EXPECT_EQ(ticks.back().divergence->marker, Divergence::Marker::kMatchEnd);
}

// Requirements: US-21, NFR-09
TEST(CaptureReplayTest, ItEndsAtTheWorldsMatchEndAndChecksItAgainstTheCapturesOne) {
  // Session 9 wins on the World's fourth tick, offset 3.
  constexpr const char* kWinsOnTheFourthTick = R"(
    local ticks = 0
    function on_tick(match)
      ticks = ticks + 1
      if ticks == 4 then return {winner = '0190a1b2-c3d4-7000-8000-000000000009'} end
    end
  )";
  Replay agreeing = ReplayOf(CaptureOf({EndAt(3, 2)}), Rules(kWinsOnTheFourthTick));
  Replay disagreeing = ReplayOf(CaptureOf({EndAt(3, 1)}), Rules(kWinsOnTheFourthTick));

  const std::vector<ReplayTick> agreed = RunToEnd(agreeing);
  const std::vector<ReplayTick> disagreed = RunToEnd(disagreeing);

  ASSERT_EQ(agreed.size(), 4U);
  EXPECT_EQ(agreed.back().end->winner, Session(9));
  for (const ReplayTick& tick : agreed) {
    EXPECT_FALSE(tick.divergence.has_value());
  }
  ASSERT_EQ(disagreed.size(), 4U);
  ASSERT_TRUE(disagreed.back().divergence.has_value());
  EXPECT_EQ(disagreed.back().divergence->captured_end, capture::MatchEnd{.winner = 1});
  EXPECT_EQ(disagreed.back().divergence->resolved_end, capture::MatchEnd{.winner = 2});
}

// A capture cut short ends at its last record, with no Match end to hold the World's to.
// Requirements: US-21
TEST(CaptureReplayTest, ACaptureCutShortEndsAtItsLastRecord) {
  capture::Capture capture = CaptureOf({CommandAt(7, 1, Command{})});
  capture.torn = true;
  Replay replay = ReplayOf(std::move(capture));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 8U);
  EXPECT_FALSE(ticks.back().end->winner.has_value());
  for (const ReplayTick& tick : ticks) {
    EXPECT_FALSE(tick.divergence.has_value());
  }
}

capture::Capture MarkedCapture() {
  return CaptureOf({Captured{.offset = 4, .event = capture::Death{.victim = 2, .killer = 1}},
                    Captured{.offset = 9, .event = capture::Death{.victim = 1, .killer = 3}},
                    Captured{.offset = 9, .event = capture::Death{.victim = 3, .killer = 1}}, EndAt(9, 1)});
}

// Requirements: NFR-09
TEST(ReplayCheckTest, MarkersResolvedAsCapturedHaveNoDivergence) {
  ReplayCheck check(MarkedCapture());

  for (std::uint32_t offset = 0; offset < 9; ++offset) {
    std::vector<capture::Death> deaths;
    if (offset == 4) {
      deaths.push_back(capture::Death{.victim = 2, .killer = 1});
    }
    EXPECT_FALSE(check.Check(offset, deaths, std::nullopt).has_value()) << offset;
  }
  // The same Deaths in another order are the same Deaths.
  EXPECT_FALSE(check
                   .Check(9, {capture::Death{.victim = 3, .killer = 1}, capture::Death{.victim = 1, .killer = 3}},
                          capture::MatchEnd{.winner = 1})
                   .has_value());
}

// Requirements: NFR-09
TEST(ReplayCheckTest, ADeathWithAnotherKillerOrOnAnotherTickIsTheFirstDivergence) {
  ReplayCheck other_killer(MarkedCapture());
  ReplayCheck late(MarkedCapture());

  const auto killer = other_killer.Check(4, {capture::Death{.victim = 2, .killer = 3}}, std::nullopt);
  const auto missing = late.Check(4, {}, std::nullopt);

  ASSERT_TRUE(killer.has_value());
  EXPECT_EQ(killer->marker, Divergence::Marker::kDeath);
  EXPECT_EQ(killer->offset, 4U);
  EXPECT_EQ(killer->captured_deaths, (std::vector<capture::Death>{{.victim = 2, .killer = 1}}));
  EXPECT_EQ(killer->resolved_deaths, (std::vector<capture::Death>{{.victim = 2, .killer = 3}}));
  ASSERT_TRUE(missing.has_value());
  EXPECT_TRUE(missing->resolved_deaths.empty());
}

// Requirements: NFR-09
TEST(ReplayCheckTest, AMatchEndOnAnotherTickOrWithAnotherWinnerIsADivergence) {
  ReplayCheck early(MarkedCapture());
  ReplayCheck missed(MarkedCapture());
  ReplayCheck other_winner(MarkedCapture());
  const std::vector<capture::Death> on_nine = {{.victim = 1, .killer = 3}, {.victim = 3, .killer = 1}};

  const auto ended_early = early.Check(2, {}, capture::MatchEnd{.winner = 1});
  (void)missed.Check(4, {{.victim = 2, .killer = 1}}, std::nullopt);
  const auto never_ended = missed.Check(9, on_nine, std::nullopt);
  (void)other_winner.Check(4, {{.victim = 2, .killer = 1}}, std::nullopt);
  const auto drawn = other_winner.Check(9, on_nine, capture::MatchEnd{});

  ASSERT_TRUE(ended_early.has_value());
  EXPECT_EQ(ended_early->marker, Divergence::Marker::kMatchEnd);
  EXPECT_FALSE(ended_early->captured_end.has_value());
  ASSERT_TRUE(never_ended.has_value());
  EXPECT_EQ(never_ended->captured_end, capture::MatchEnd{.winner = 1});
  EXPECT_FALSE(never_ended->resolved_end.has_value());
  ASSERT_TRUE(drawn.has_value());
  EXPECT_EQ(drawn->resolved_end, capture::MatchEnd{});
}

// Only the first difference is logged: the rest follow from it.
// Requirements: NFR-09
TEST(ReplayCheckTest, OnlyTheFirstDivergenceIsReported) {
  ReplayCheck check(MarkedCapture());

  EXPECT_TRUE(check.Check(1, {capture::Death{.victim = 1, .killer = 2}}, std::nullopt).has_value());
  EXPECT_FALSE(check.Check(4, {}, std::nullopt).has_value());
  EXPECT_FALSE(check.Check(9, {}, std::nullopt).has_value());
}

// Requirements: NFR-09
TEST(ReplayCheckTest, ADivergenceIsDescribedWithBothSides) {
  const Divergence death{.marker = Divergence::Marker::kDeath,
                         .offset = 4,
                         .captured_deaths = {{.victim = 2, .killer = 1}},
                         .resolved_deaths = {},
                         .captured_end = std::nullopt,
                         .resolved_end = std::nullopt};
  const Divergence end{.marker = Divergence::Marker::kMatchEnd,
                       .offset = 9,
                       .captured_deaths = {},
                       .resolved_deaths = {},
                       .captured_end = capture::MatchEnd{.winner = 1},
                       .resolved_end = capture::MatchEnd{}};

  EXPECT_EQ(augusta::host::DescribeDivergence(death), "marker=death offset=4 captured=[2<-1] resolved=[]");
  EXPECT_EQ(augusta::host::DescribeDivergence(end), "marker=match_end offset=9 captured=winner:1 resolved=draw");
}

}  // namespace
