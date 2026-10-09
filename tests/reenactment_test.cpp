#include "augusta/reenactment.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/tick.h"
#include "capture.h"
#include "match.h"

// A Captured player (ADR-0050) without a network: its Script read from a
// capture a server::Capturer writes, and its pacing and progress decided from
// Server views built by hand.
namespace {

using augusta::capture_file::ReadError;
using augusta::command::Command;
using augusta::harness::CapturedCommand;
using augusta::harness::CapturedPlayer;
using augusta::harness::DueTick;
using augusta::harness::EntityId;
using augusta::harness::MatchPlayer;
using augusta::harness::MatchStart;
using augusta::harness::ObservedDeath;
using augusta::harness::Pacer;
using augusta::harness::Progress;
using augusta::harness::ReadScript;
using augusta::harness::Reenactment;
using augusta::harness::Script;
using augusta::harness::ScriptError;
using augusta::harness::ServerView;
using augusta::harness::SessionId;
using augusta::math::Vec3;
using augusta::tick::Tick;

constexpr std::uint8_t kTickRate = 60;
// At 60 Hz the server's queue holds a movement for 6 ticks (command::HeldTicks).
constexpr int kHeldTicks = 6;
constexpr Tick kFirstTick = 1000;

// A Command walking along x at speed, its fire held or not.
Command Walk(float speed, bool fire = false) {
  Command command;
  command.movement.direction = Vec3(speed, 0.0F, 0.0F);
  command.fire = fire;
  command.yaw = 0.5F;
  return command;
}

// --- Reading a capture ---

class ReadScriptTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = std::filesystem::temp_directory_path() / (std::string("augusta_reenactment_test_") + test.name());
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  // Captures a Match of two players, the soldier at (1, 0, 2) and the sniper
  // at (-3, 0, 4): the sniper walks on its first two ticks and the fifth, the
  // soldier on its second; the soldier kills the sniper on the sixth tick,
  // and leaves before the eighth; the Match ends on the ninth, the soldier's.
  // Returns the capture's file.
  std::filesystem::path CaptureAMatch() {
    augusta::server::CaptureHeader header{.engine_version = "2.0.1", .tick_rate_hz = kTickRate, .started = {}};
    header.client_pack.fill(std::byte{2});
    {
      augusta::server::Capturer capturer(directory_, header);
      capturer.StartMatch({{.session = augusta::server::SessionId{7},
                            .entity = augusta::server::EntityId{70},
                            .character = "soldier",
                            .spawn = {1, 0, 2}},
                           {.session = augusta::server::SessionId{8},
                            .entity = augusta::server::EntityId{80},
                            .character = "sniper",
                            .spawn = {-3, 0, 4}}},
                          kFirstTick, std::chrono::system_clock::now());
      Command seen = Walk(1.0F);
      seen.seen_tick = kFirstTick - 3;
      capturer.Command(kFirstTick, augusta::server::EntityId{80}, seen);
      seen.seen_tick = kFirstTick - 2;
      capturer.Command(kFirstTick + 1, augusta::server::EntityId{80}, seen);
      capturer.Command(kFirstTick + 1, augusta::server::EntityId{70}, Walk(-1.0F, true));
      capturer.Command(kFirstTick + 4, augusta::server::EntityId{80}, Walk(0.5F));
      capturer.Death(kFirstTick + 5, augusta::server::EntityId{80}, augusta::server::EntityId{70});
      capturer.Leave(kFirstTick + 7, augusta::server::EntityId{70});
      capturer.EndMatch(kFirstTick + 8, augusta::server::SessionId{7});
    }
    const std::vector<std::filesystem::path> files(std::filesystem::directory_iterator(directory_), {});
    EXPECT_EQ(files.size(), 1U);
    return files.empty() ? std::filesystem::path{} : files.front();
  }

  static std::optional<Script> Read(const std::filesystem::path& path, CapturedPlayer player) {
    std::ifstream in(path, std::ios::binary);
    auto script = ReadScript(in, player);
    EXPECT_TRUE(script.has_value()) << (script.has_value() ? "" : DescribeScriptError(script.error()));
    return script.has_value() ? std::optional<Script>(*std::move(script)) : std::nullopt;
  }

  std::filesystem::path directory_;
};

// Requirements: US-21
TEST_F(ReadScriptTest, APlayersScriptIsItsJoinItsCommandsAndItsLeave) {
  const auto script = Read(CaptureAMatch(), 2);

  ASSERT_TRUE(script.has_value());
  EXPECT_EQ(script->player, 2);
  EXPECT_EQ(script->character, "sniper");
  EXPECT_EQ(script->spawn, Vec3(-3, 0, 4));
  EXPECT_EQ(script->spawns, (std::vector<Vec3>{{1, 0, 2}, {-3, 0, 4}}));
  EXPECT_EQ(script->tick_rate_hz, kTickRate);
  EXPECT_EQ(script->client_pack.front(), std::byte{2});
  ASSERT_EQ(script->commands.size(), 3U);
  EXPECT_EQ(script->commands[0].offset, 0U);
  EXPECT_EQ(script->commands[0].seen_offset, -3);
  EXPECT_EQ(script->commands[1].offset, 1U);
  EXPECT_EQ(script->commands[1].seen_offset, -2);
  EXPECT_EQ(script->commands[2].offset, 4U);
  EXPECT_EQ(script->commands[2].command.movement.direction, Vec3(0.5F, 0.0F, 0.0F));
  EXPECT_EQ(script->commands[0].command.yaw, 0.5F);
  EXPECT_FALSE(script->leave.has_value());
  EXPECT_FALSE(script->torn);
}

// Requirements: US-21
TEST_F(ReadScriptTest, EveryScriptHoldsTheCapturesDeathsAndMatchEnd) {
  const auto script = Read(CaptureAMatch(), 1);

  ASSERT_TRUE(script.has_value());
  EXPECT_EQ(script->character, "soldier");
  ASSERT_EQ(script->commands.size(), 1U);
  EXPECT_TRUE(script->commands[0].command.fire);
  EXPECT_EQ(script->leave, 7U);
  ASSERT_EQ(script->deaths.size(), 1U);
  EXPECT_EQ(script->deaths[0].offset, 5U);
  EXPECT_EQ(script->deaths[0].victim, 2);
  EXPECT_EQ(script->deaths[0].killer, 1);
  ASSERT_TRUE(script->end.has_value());
  EXPECT_EQ(script->end->offset, 8U);
  EXPECT_EQ(script->end->winner, CapturedPlayer{1});
}

// Requirements: US-21
TEST_F(ReadScriptTest, ACaptureWithoutThePlayerAskedForGivesNoScript) {
  const std::filesystem::path path = CaptureAMatch();
  for (const CapturedPlayer player : {CapturedPlayer{0}, CapturedPlayer{3}}) {
    std::ifstream in(path, std::ios::binary);
    EXPECT_EQ(ReadScript(in, player).error(), ScriptError{.capture = std::nullopt}) << +player;
  }
}

TEST_F(ReadScriptTest, AFileThatIsNoCaptureGivesNoScript) {
  std::istringstream pack("AUGPACK\r\n and the rest");
  EXPECT_EQ(ReadScript(pack, 1).error(), ScriptError{.capture = ReadError::kNotACapture});
  std::ifstream missing(directory_ / "missing.capture", std::ios::binary);
  EXPECT_EQ(ReadScript(missing, 1).error(), ScriptError{.capture = ReadError::kUnreadable});
}

TEST_F(ReadScriptTest, ACaptureCutShortKeepsEveryWholeRecordAndSaysItIsTorn) {
  const std::filesystem::path path = CaptureAMatch();
  std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);

  const auto script = Read(path, 1);

  ASSERT_TRUE(script.has_value());
  EXPECT_TRUE(script->torn);
  EXPECT_FALSE(script->end.has_value());
  EXPECT_EQ(script->leave, 7U);
}

// --- When a Command reaches the World ---

TEST(DueTickTest, ACommandBehindOthersIsHandedOneTickAfterEachOfThem) {
  EXPECT_EQ(DueTick(100, 1, 0), 101U);
  EXPECT_EQ(DueTick(100, 4, 2), 104U);
}

TEST(DueTickTest, ACommandIsNeverHandedBeforeItCanReachTheServer) {
  EXPECT_EQ(DueTick(100, 1, 5), 106U);
  EXPECT_EQ(DueTick(100, 0, 0), 101U);
}

TEST(RoundTripTicksTest, APingIsCountedInWholeTicksRoundedUp) {
  EXPECT_EQ(augusta::harness::RoundTripTicks(0, kTickRate), 0U);
  EXPECT_EQ(augusta::harness::RoundTripTicks(-1, kTickRate), 0U);
  EXPECT_EQ(augusta::harness::RoundTripTicks(1, kTickRate), 1U);
  EXPECT_EQ(augusta::harness::RoundTripTicks(50, kTickRate), 3U);
  EXPECT_EQ(augusta::harness::RoundTripTicks(100, 30), 3U);
}

// --- Pacing ---

// commands, one at each of offsets, walking at 1, 2, 3 ...
std::vector<CapturedCommand> AtOffsets(const std::vector<std::uint32_t>& offsets) {
  std::vector<CapturedCommand> commands;
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    commands.push_back(CapturedCommand{.offset = offsets[i],
                                       .seen_offset = static_cast<std::int32_t>(offsets[i]) - 2,
                                       .command = Walk(static_cast<float>(i + 1))});
  }
  return commands;
}

// Requirements: US-21
TEST(PacerTest, EachCommandGoesOutForTheTickOfItsOffsetWithItsSeenTimesDelayKept) {
  Pacer pacer(AtOffsets({0, 1, 2}), kTickRate);

  for (std::uint32_t offset = 0; offset < 3; ++offset) {
    const Command sent = pacer.Next(kFirstTick, kFirstTick + 10 + offset);
    EXPECT_EQ(sent.movement.direction.x, static_cast<float>(offset + 1)) << offset;
    EXPECT_EQ(sent.seen_tick, kFirstTick + 10 + offset - 2) << offset;
  }
  EXPECT_EQ(pacer.Sent(), 3U);
}

// Requirements: US-21
TEST(PacerTest, BeforeItsFirstCommandIsDueThePacerSendsIdleCommands) {
  Pacer pacer(AtOffsets({3}), kTickRate);

  for (Tick due = kFirstTick; due < kFirstTick + 3; ++due) {
    const Command idle = pacer.Next(kFirstTick, due);
    EXPECT_EQ(idle.movement.direction, Vec3()) << due;
    EXPECT_FALSE(idle.fire) << due;
  }
  EXPECT_EQ(pacer.Next(kFirstTick, kFirstTick + 3).movement.direction.x, 1.0F);
}

// The gaps of a capture are where the server's queue held or idled: the pacer
// fills them the same way, so every Command after one stays on its offset.
// Requirements: US-21
TEST(PacerTest, AGapHoldsTheLastMovementWithoutItsActionsForTheHoldThenIdles) {
  std::vector<CapturedCommand> commands = AtOffsets({0, 20});
  commands[0].command.fire = true;
  commands[0].command.reload = true;
  Pacer pacer(commands, kTickRate);
  ASSERT_TRUE(pacer.Next(kFirstTick, kFirstTick).fire);

  for (int i = 1; i <= kHeldTicks; ++i) {
    const Command held = pacer.Next(kFirstTick, kFirstTick + i);
    EXPECT_EQ(held.movement.direction.x, 1.0F) << i;
    EXPECT_EQ(held.yaw, 0.5F) << i;
    EXPECT_FALSE(held.fire) << i;
    EXPECT_FALSE(held.reload) << i;
  }
  const Command idle = pacer.Next(kFirstTick, kFirstTick + kHeldTicks + 1);
  EXPECT_EQ(idle.movement.direction, Vec3());
  EXPECT_EQ(idle.yaw, 0.5F);
}

// Requirements: US-21
TEST(PacerTest, ALateCommandIsNeverDroppedAndTheGapsAfterItCloseUp) {
  Pacer pacer(AtOffsets({0, 1, 5}), kTickRate);

  // Due three ticks late: each goes out in its turn, none skipped.
  EXPECT_EQ(pacer.Next(kFirstTick, kFirstTick + 3).movement.direction.x, 1.0F);
  EXPECT_EQ(pacer.Next(kFirstTick, kFirstTick + 4).movement.direction.x, 2.0F);
  // The gap from offset 2 to 4 is gone: the third is on its offset again.
  EXPECT_EQ(pacer.Next(kFirstTick, kFirstTick + 5).movement.direction.x, 3.0F);
  EXPECT_EQ(pacer.Sent(), 3U);
}

// Requirements: US-21
TEST(PacerTest, ACommandDueEarlyWaitsForItsOffsetBehindFillers) {
  Pacer pacer(AtOffsets({0, 1}), kTickRate);
  ASSERT_EQ(pacer.Next(kFirstTick, kFirstTick).movement.direction.x, 1.0F);

  // The queue grew: the next send is due on offset 0 again, so it holds.
  const Command filler = pacer.Next(kFirstTick, kFirstTick);
  EXPECT_FALSE(filler.fire);
  EXPECT_EQ(pacer.Sent(), 1U);
  EXPECT_EQ(pacer.Next(kFirstTick, kFirstTick + 1).movement.direction.x, 2.0F);
}

// --- A run ---

// A Script of player 2 of three, spawned in a row, with Commands on offsets
// 0 and 1, a Death of player 3 by 1 at 30, its Leave at 40 and a Match end
// at 50, won by player 1.
Script ThreePlayerScript() {
  Script script;
  script.tick_rate_hz = kTickRate;
  script.player = 2;
  script.character = "soldier";
  script.spawns = {Vec3(1, 0, 0), Vec3(2, 0, 0), Vec3(3, 0, 0)};
  script.spawn = script.spawns[1];
  script.commands = AtOffsets({0, 1});
  script.deaths = {{.offset = 30, .victim = 3, .killer = 1}};
  script.leave = 40;
  script.end = augusta::harness::CapturedEnd{.offset = 50, .winner = 1};
  return script;
}

// A view of the first Match, its players at the Script's spawns with
// sessions 11, 12 and 13 and bodies 21, 22 and 23, its newest State of tick
// (none if nullopt) acknowledging acknowledged.
ServerView InMatch(std::optional<Tick> tick, augusta::command::Sequence acknowledged = 0) {
  ServerView view;
  view.matches_started = 1;
  view.in_match = true;
  MatchStart start{.players = {}, .first_tick = kFirstTick};
  for (std::uint32_t i = 0; i < 3; ++i) {
    start.players.push_back(MatchPlayer{.session = SessionId{11 + i},
                                        .entity = EntityId{21 + i},
                                        .character = "soldier",
                                        .spawn = Vec3(static_cast<float>(i + 1), 0, 0)});
  }
  view.match_start = start;
  if (tick.has_value()) {
    augusta::harness::AuthoritativeState state;
    state.tick = *tick;
    state.acknowledged_sequence = acknowledged;
    view.authoritative = state;
  }
  return view;
}

// Requirements: US-21
TEST(ReenactmentTest, OutsideItsMatchItSendsNothingButIdleCommands) {
  Reenactment run(ThreePlayerScript());
  ServerView lobby;

  EXPECT_EQ(run.NextCommand(lobby, 1, 0).movement.direction, Vec3());
  EXPECT_EQ(run.Check(lobby, 0), Progress::kPlaying);
}

// Requirements: US-21
TEST(ReenactmentTest, BeforeTheFirstStateTheServerIsTakenToHaveRunTheMatchsFirstTick) {
  Reenactment run(ThreePlayerScript());
  Script late = ThreePlayerScript();
  late.commands = AtOffsets({1});
  Reenactment on_time(late);

  // The server runs the Match's first tick as it sends Match start, so the
  // first Command can reach it on the second at the soonest.
  EXPECT_EQ(on_time.NextCommand(InMatch(std::nullopt), 1, 0).movement.direction.x, 1.0F);
  EXPECT_EQ(run.NextCommand(InMatch(std::nullopt), 1, 0).movement.direction.x, 1.0F);
  // State 1000 handed neither sequence 1 nor 2 to the World: the second is
  // due on 1002, late for its offset, and goes all the same.
  EXPECT_EQ(run.NextCommand(InMatch(kFirstTick, 0), 2, 0).movement.direction.x, 2.0F);
}

// Requirements: US-21
TEST(ReenactmentTest, ItCountsTheCommandsAheadOfItsOwnFromTheNewestState) {
  Reenactment run(ThreePlayerScript());
  ASSERT_EQ(run.NextCommand(InMatch(std::nullopt), 5, 0).movement.direction.x, 1.0F);

  // State 1000 acknowledged sequence 5, the first: sequence 6 is due on 1001, offset 1.
  EXPECT_EQ(run.NextCommand(InMatch(kFirstTick, 5), 6, 0).movement.direction.x, 2.0F);
}

// Requirements: US-21
TEST(ReenactmentTest, ItLeavesWhenItsCapturedLeaveIsDue) {
  const Reenactment run(ThreePlayerScript());

  EXPECT_EQ(run.Check(InMatch(kFirstTick + 38), 0), Progress::kPlaying);
  EXPECT_EQ(run.Check(InMatch(kFirstTick + 39), 0), Progress::kLeave);
  // A slower connection disconnects as many ticks sooner.
  EXPECT_EQ(run.Check(InMatch(kFirstTick + 36), 3), Progress::kLeave);
}

// Requirements: US-21
TEST(ReenactmentTest, ItEndsAtTheCapturesMatchEndOrTheServersWhicheverComesFirst) {
  Script script = ThreePlayerScript();
  script.leave.reset();
  const Reenactment run(script);

  EXPECT_EQ(run.Check(InMatch(kFirstTick + 49), 0), Progress::kPlaying);
  EXPECT_EQ(run.Check(InMatch(kFirstTick + 50), 0), Progress::kEnded);
  ServerView ended = InMatch(std::nullopt);
  ended.in_match = false;
  ended.match_end = augusta::harness::MatchEnd{.winner = SessionId{13}};
  EXPECT_EQ(run.Check(ended, 0), Progress::kEnded);
}

// Requirements: US-21
TEST(ReenactmentTest, ItsOutcomePutsEachCapturedDeathAndTheMatchEndBesideWhatTheServerTold) {
  const Reenactment run(ThreePlayerScript());
  ServerView ended = InMatch(std::nullopt);
  ended.in_match = false;
  ended.match_end = augusta::harness::MatchEnd{.winner = SessionId{13}};
  const std::vector<ObservedDeath> observed = {
      {.tick = kFirstTick + 31, .victim = EntityId{23}, .killer = EntityId{21}},
      {.tick = kFirstTick + 45, .victim = EntityId{21}, .killer = EntityId{23}},
  };

  const std::vector<std::string> lines = run.Outcome(ended, observed, kFirstTick + 52);

  EXPECT_EQ(lines, (std::vector<std::string>{
                       "event=death victim=3 killer=1 captured_offset=30 observed_offset=31 observed_killer=1",
                       "event=death victim=1 killer=3 captured=none observed_offset=45",
                       "event=match_end captured_offset=50 captured_winner=1 observed_offset=52 observed_winner=3",
                   }));
}

// Requirements: US-21
TEST(ReenactmentTest, ACapturedDeathTheServerNeverToldIsSaidSo) {
  const Reenactment run(ThreePlayerScript());

  const std::vector<std::string> lines = run.Outcome(InMatch(kFirstTick + 50), {}, kFirstTick + 50);

  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(lines[0], "event=death victim=3 killer=1 captured_offset=30 observed=none");
  EXPECT_EQ(lines[1], "event=match_end captured_offset=50 captured_winner=1 observed=none observed_offset=50");
}

// Requirements: US-21
TEST(ReenactmentTest, AServerOfAnotherPlayerCountIsSaidSo) {
  const Reenactment run(ThreePlayerScript());
  augusta::harness::Admission admission;

  admission.parameters.player_count = 3;
  EXPECT_FALSE(run.PlayerCountMismatch(admission).has_value());
  admission.parameters.player_count = 2;
  EXPECT_EQ(run.PlayerCountMismatch(admission), "event=player_count_differs server=2 capture=3");
}

}  // namespace
