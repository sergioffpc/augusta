#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "capture.h"
#include "match.h"
#include "replay.h"

// A Replay (ADR-0051) re-run on a SimulationWorld with no Map, from captures
// made by hand: what it hands the World, tick by tick, and what it compares
// with the capture's markers (NFR-09). The whole path, from a live Host's
// capture to a viewer, is replay_server_test.cpp's.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::server::Capture;
using augusta::server::CapturedCommand;
using augusta::server::CapturedDeath;
using augusta::server::CapturedJoin;
using augusta::server::CapturedLeave;
using augusta::server::CapturedMatchEnd;
using augusta::server::CaptureRecord;
using augusta::server::Divergence;
using augusta::server::EntityId;
using augusta::server::Replay;
using augusta::server::ReplayCheck;
using augusta::server::ReplayTick;
using augusta::server::SessionId;
using augusta::simulation::Character;

constexpr std::uint8_t kTickRate = 60;
constexpr const char* kCharacter = "soldier";

// Two players, sessions 7 and 9, apart on the X axis.
std::vector<CaptureRecord> TwoJoins() {
  return {CaptureRecord{
              .offset = 0,
              .event =
                  CapturedJoin{
                      .player = 1, .session = SessionId{7}, .character = kCharacter, .spawn = Vec3(10.0F, 0.0F, 0.0F)}},
          CaptureRecord{
              .offset = 0,
              .event = CapturedJoin{
                  .player = 2, .session = SessionId{9}, .character = kCharacter, .spawn = Vec3(-10.0F, 0.0F, 4.0F)}}};
}

Capture CaptureOf(std::vector<CaptureRecord> records) {
  Capture capture;
  capture.header.tick_rate_hz = kTickRate;
  capture.records = TwoJoins();
  capture.records.insert(capture.records.end(), records.begin(), records.end());
  return capture;
}

CaptureRecord CommandAt(std::uint32_t offset, augusta::server::CapturedPlayer player, const Command& command,
                        std::int32_t seen_offset = 0) {
  return CaptureRecord{.offset = offset,
                       .event = CapturedCommand{.player = player, .seen_offset = seen_offset, .command = command}};
}

CaptureRecord EndAt(std::uint32_t offset, std::optional<augusta::server::CapturedPlayer> winner = std::nullopt) {
  return CaptureRecord{.offset = offset, .event = CapturedMatchEnd{.winner = winner}};
}

const std::unordered_map<std::string, Character>& Characters() {
  static const std::unordered_map<std::string, Character> characters = {
      {kCharacter, Character{.eye = Vec3(0.0F, 1.6F, 0.0F), .hitboxes = {}}}};
  return characters;
}

augusta::scripting::Engine Rules(const char* rules) {
  auto policy = augusta::scripting::Engine::Load(rules);
  EXPECT_TRUE(policy.has_value());
  return policy ? *std::move(policy) : augusta::scripting::Engine{};
}

Replay ReplayOf(Capture capture, augusta::scripting::Engine policy = {}) {
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
  EXPECT_EQ(replay.Start().players[0].session, SessionId{7});
  EXPECT_EQ(replay.Start().players[0].entity, EntityId{1});
  EXPECT_EQ(replay.Start().players[1].session, SessionId{9});
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
// new for, as the live server's did: the view keeps its pitch and ADS.
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
    ASSERT_EQ(tick.views.size(), 2U);
    EXPECT_EQ(tick.views[0].entity, EntityId{1});
    EXPECT_FLOAT_EQ(tick.views[0].pitch, 0.5F);
    EXPECT_TRUE(tick.views[0].ads);
    EXPECT_FLOAT_EQ(tick.views[1].pitch, 0.0F);
    EXPECT_FALSE(tick.views[1].ads);
  }
  // The movement is held for kMaxHeldTime, then the body stops, as live.
  EXPECT_GT(BodyOf(ticks[10], 1)->body.position.x, 10.0F);
  EXPECT_FLOAT_EQ(BodyOf(ticks.back(), 1)->body.position.x, BodyOf(ticks[ticks.size() - 2], 1)->body.position.x);
}

// A Command the capture has on a tick reaches the World on that tick, and the
// view the viewer is shown is its pitch.
// Requirements: US-21
TEST(CaptureReplayTest, EachCapturedCommandReachesTheWorldOnItsTick) {
  Command up;
  up.pitch = 0.25F;
  Command down;
  down.pitch = -0.75F;
  Replay replay = ReplayOf(CaptureOf({CommandAt(2, 2, up), CommandAt(5, 2, down), EndAt(6)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 7U);
  EXPECT_FLOAT_EQ(ticks[1].views[1].pitch, 0.0F);
  EXPECT_FLOAT_EQ(ticks[2].views[1].pitch, 0.25F);
  EXPECT_FLOAT_EQ(ticks[4].views[1].pitch, 0.25F);
  EXPECT_FLOAT_EQ(ticks[5].views[1].pitch, -0.75F);
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
  Replay replay = ReplayOf(CaptureOf({CaptureRecord{.offset = 3, .event = CapturedLeave{.player = 2}}, EndAt(5)}));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 6U);
  EXPECT_NE(BodyOf(ticks[2], 2), nullptr);
  EXPECT_EQ(BodyOf(ticks[3], 2), nullptr);
  EXPECT_EQ(ticks[3].views.size(), 1U);
  EXPECT_NE(BodyOf(ticks[3], 1), nullptr);
}

// The live server takes out the bodies of those who left an ended Match after
// its last tick: their Leaves on that tick do not take them out before it.
TEST(CaptureReplayTest, ALeaveAfterTheLastTickLeavesThatTickAsItRan) {
  Replay replay = ReplayOf(CaptureOf({CaptureRecord{.offset = 4, .event = CapturedLeave{.player = 2}}, EndAt(4)}));

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
  EXPECT_EQ(ticks.back().end->winner, SessionId{9});
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
      if ticks == 4 then return {winner = 9} end
    end
  )";
  Replay agreeing = ReplayOf(CaptureOf({EndAt(3, 2)}), Rules(kWinsOnTheFourthTick));
  Replay disagreeing = ReplayOf(CaptureOf({EndAt(3, 1)}), Rules(kWinsOnTheFourthTick));

  const std::vector<ReplayTick> agreed = RunToEnd(agreeing);
  const std::vector<ReplayTick> disagreed = RunToEnd(disagreeing);

  ASSERT_EQ(agreed.size(), 4U);
  EXPECT_EQ(agreed.back().end->winner, SessionId{9});
  for (const ReplayTick& tick : agreed) {
    EXPECT_FALSE(tick.divergence.has_value());
  }
  ASSERT_EQ(disagreed.size(), 4U);
  ASSERT_TRUE(disagreed.back().divergence.has_value());
  EXPECT_EQ(disagreed.back().divergence->captured_end, CapturedMatchEnd{.winner = 1});
  EXPECT_EQ(disagreed.back().divergence->resolved_end, CapturedMatchEnd{.winner = 2});
}

// A capture cut short ends at its last record, with no Match end to hold the World's to.
TEST(CaptureReplayTest, ACaptureCutShortEndsAtItsLastRecord) {
  Capture capture = CaptureOf({CommandAt(7, 1, Command{})});
  capture.torn = true;
  Replay replay = ReplayOf(std::move(capture));

  const std::vector<ReplayTick> ticks = RunToEnd(replay);

  ASSERT_EQ(ticks.size(), 8U);
  EXPECT_FALSE(ticks.back().end->winner.has_value());
  for (const ReplayTick& tick : ticks) {
    EXPECT_FALSE(tick.divergence.has_value());
  }
}

Capture MarkedCapture() {
  return CaptureOf({CaptureRecord{.offset = 4, .event = CapturedDeath{.victim = 2, .killer = 1}},
                    CaptureRecord{.offset = 9, .event = CapturedDeath{.victim = 1, .killer = 3}},
                    CaptureRecord{.offset = 9, .event = CapturedDeath{.victim = 3, .killer = 1}}, EndAt(9, 1)});
}

// Requirements: NFR-09
TEST(ReplayCheckTest, MarkersResolvedAsCapturedHaveNoDivergence) {
  ReplayCheck check(MarkedCapture());

  for (std::uint32_t offset = 0; offset < 9; ++offset) {
    std::vector<CapturedDeath> deaths;
    if (offset == 4) {
      deaths.push_back(CapturedDeath{.victim = 2, .killer = 1});
    }
    EXPECT_FALSE(check.Check(offset, deaths, std::nullopt).has_value()) << offset;
  }
  // The same Deaths in another order are the same Deaths.
  EXPECT_FALSE(check
                   .Check(9, {CapturedDeath{.victim = 3, .killer = 1}, CapturedDeath{.victim = 1, .killer = 3}},
                          CapturedMatchEnd{.winner = 1})
                   .has_value());
}

// Requirements: NFR-09
TEST(ReplayCheckTest, ADeathWithAnotherKillerOrOnAnotherTickIsTheFirstDivergence) {
  ReplayCheck other_killer(MarkedCapture());
  ReplayCheck late(MarkedCapture());

  const auto killer = other_killer.Check(4, {CapturedDeath{.victim = 2, .killer = 3}}, std::nullopt);
  const auto missing = late.Check(4, {}, std::nullopt);

  ASSERT_TRUE(killer.has_value());
  EXPECT_EQ(killer->marker, Divergence::Marker::kDeath);
  EXPECT_EQ(killer->offset, 4U);
  EXPECT_EQ(killer->captured_deaths, (std::vector<CapturedDeath>{{.victim = 2, .killer = 1}}));
  EXPECT_EQ(killer->resolved_deaths, (std::vector<CapturedDeath>{{.victim = 2, .killer = 3}}));
  ASSERT_TRUE(missing.has_value());
  EXPECT_TRUE(missing->resolved_deaths.empty());
}

// Requirements: NFR-09
TEST(ReplayCheckTest, AMatchEndOnAnotherTickOrWithAnotherWinnerIsADivergence) {
  ReplayCheck early(MarkedCapture());
  ReplayCheck missed(MarkedCapture());
  ReplayCheck other_winner(MarkedCapture());
  const std::vector<CapturedDeath> on_nine = {{.victim = 1, .killer = 3}, {.victim = 3, .killer = 1}};

  const auto ended_early = early.Check(2, {}, CapturedMatchEnd{.winner = 1});
  (void)missed.Check(4, {{.victim = 2, .killer = 1}}, std::nullopt);
  const auto never_ended = missed.Check(9, on_nine, std::nullopt);
  (void)other_winner.Check(4, {{.victim = 2, .killer = 1}}, std::nullopt);
  const auto drawn = other_winner.Check(9, on_nine, CapturedMatchEnd{});

  ASSERT_TRUE(ended_early.has_value());
  EXPECT_EQ(ended_early->marker, Divergence::Marker::kMatchEnd);
  EXPECT_FALSE(ended_early->captured_end.has_value());
  ASSERT_TRUE(never_ended.has_value());
  EXPECT_EQ(never_ended->captured_end, CapturedMatchEnd{.winner = 1});
  EXPECT_FALSE(never_ended->resolved_end.has_value());
  ASSERT_TRUE(drawn.has_value());
  EXPECT_EQ(drawn->resolved_end, CapturedMatchEnd{});
}

// Only the first difference is logged: the rest follow from it.
// Requirements: NFR-09
TEST(ReplayCheckTest, OnlyTheFirstDivergenceIsReported) {
  ReplayCheck check(MarkedCapture());

  EXPECT_TRUE(check.Check(1, {CapturedDeath{.victim = 1, .killer = 2}}, std::nullopt).has_value());
  EXPECT_FALSE(check.Check(4, {}, std::nullopt).has_value());
  EXPECT_FALSE(check.Check(9, {}, std::nullopt).has_value());
}

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
                       .captured_end = CapturedMatchEnd{.winner = 1},
                       .resolved_end = CapturedMatchEnd{}};

  EXPECT_EQ(augusta::server::DescribeDivergence(death), "marker=death offset=4 captured=[2<-1] resolved=[]");
  EXPECT_EQ(augusta::server::DescribeDivergence(end), "marker=match_end offset=9 captured=winner:1 resolved=draw");
}

}  // namespace
