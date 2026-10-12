#include "augusta/reenactment.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/capture_error.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/tick.h"
#include "capture.h"
#include "command_queue.h"
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
using augusta::harness::PairDeaths;
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

// The x its movement walks at, which tells the Commands of AtOffsets apart; 0 for none sent.
float SpeedOf(const std::optional<Command>& sent) { return sent.has_value() ? sent->movement.direction.x : 0.0F; }

// Requirements: US-21
TEST(PacerTest, EachCommandGoesOutForTheTickOfItsOffsetWithItsSeenTimesDelayKept) {
  Pacer pacer(AtOffsets({10, 11, 12}), kTickRate, std::nullopt);

  for (std::uint32_t offset = 10; offset < 13; ++offset) {
    const std::optional<Command> sent = pacer.Next(kFirstTick, kFirstTick + offset);
    ASSERT_TRUE(sent.has_value()) << offset;
    EXPECT_EQ(sent->movement.direction.x, static_cast<float>(offset - 9)) << offset;
    EXPECT_EQ(sent->seen_tick, kFirstTick + offset - 2) << offset;
  }
  EXPECT_EQ(pacer.Sent(), 3U);
}

// Requirements: US-21
TEST(PacerTest, BeforeItsFirstCommandIsDueThePacerSendsIdleCommands) {
  Pacer pacer(AtOffsets({3}), kTickRate, std::nullopt);

  for (Tick due = kFirstTick; due < kFirstTick + 3; ++due) {
    const std::optional<Command> idle = pacer.Next(kFirstTick, due);
    ASSERT_TRUE(idle.has_value()) << due;
    EXPECT_EQ(idle->movement.direction, Vec3()) << due;
    EXPECT_FALSE(idle->fire) << due;
  }
  EXPECT_EQ(SpeedOf(pacer.Next(kFirstTick, kFirstTick + 3)), 1.0F);
}

// The gaps of a capture are where the server's queue held or idled: the pacer
// fills them the same way, so every Command after one stays on its offset.
// Requirements: US-21
TEST(PacerTest, AGapHoldsTheLastMovementWithoutItsActionsForTheHoldThenIdles) {
  std::vector<CapturedCommand> commands = AtOffsets({0, 20});
  commands[0].command.fire = true;
  commands[0].command.reload = true;
  Pacer pacer(commands, kTickRate, std::nullopt);
  ASSERT_TRUE(pacer.Next(kFirstTick, kFirstTick)->fire);

  for (int i = 1; i <= kHeldTicks; ++i) {
    const std::optional<Command> held = pacer.Next(kFirstTick, kFirstTick + i);
    ASSERT_TRUE(held.has_value()) << i;
    EXPECT_EQ(held->movement.direction.x, 1.0F) << i;
    EXPECT_EQ(held->yaw, 0.5F) << i;
    EXPECT_FALSE(held->fire) << i;
    EXPECT_FALSE(held->reload) << i;
  }
  const std::optional<Command> idle = pacer.Next(kFirstTick, kFirstTick + kHeldTicks + 1);
  ASSERT_TRUE(idle.has_value());
  EXPECT_EQ(idle->movement.direction, Vec3());
  EXPECT_EQ(idle->yaw, 0.5F);
}

// Requirements: US-21
TEST(PacerTest, ALateCommandIsNeverDroppedAndTheGapsAfterItCloseUp) {
  Pacer pacer(AtOffsets({0, 1, 5}), kTickRate, std::nullopt);

  // Due three ticks late: each goes out in its turn, none skipped.
  EXPECT_EQ(SpeedOf(pacer.Next(kFirstTick, kFirstTick + 3)), 1.0F);
  EXPECT_EQ(SpeedOf(pacer.Next(kFirstTick, kFirstTick + 4)), 2.0F);
  // The gap from offset 2 to 4 is gone: the third is on its offset again.
  EXPECT_EQ(SpeedOf(pacer.Next(kFirstTick, kFirstTick + 5)), 3.0F);
  EXPECT_EQ(pacer.Sent(), 3U);
}

// One Command for each server tick at most: a client ticking faster than
// the server finds the tick its Command would be for already sent for.
// Requirements: US-21
TEST(PacerTest, NothingGoesForATickACommandAlreadyWentFor) {
  Pacer pacer(AtOffsets({0, 1}), kTickRate, std::nullopt);
  ASSERT_EQ(SpeedOf(pacer.Next(kFirstTick, kFirstTick)), 1.0F);

  EXPECT_FALSE(pacer.Next(kFirstTick, kFirstTick).has_value());
  EXPECT_EQ(pacer.Sent(), 1U);
  EXPECT_EQ(SpeedOf(pacer.Next(kFirstTick, kFirstTick + 1)), 2.0F);
}

// Requirements: US-21
TEST(PacerTest, NothingGoesForTheLeavesTickOrAfterIt) {
  Pacer pacer(AtOffsets({0}), kTickRate, 2);
  ASSERT_TRUE(pacer.Next(kFirstTick, kFirstTick).has_value());
  ASSERT_TRUE(pacer.Next(kFirstTick, kFirstTick + 1).has_value());
  EXPECT_TRUE(pacer.DoneBeforeLeave());

  EXPECT_FALSE(pacer.Next(kFirstTick, kFirstTick + 2).has_value());
  EXPECT_FALSE(pacer.Next(kFirstTick, kFirstTick + 3).has_value());
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
  script.deaths = {{.offset = 30, .victim = 3, .killer = 1}, {.offset = 45, .victim = 1, .killer = 3}};
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

  EXPECT_EQ(run.NextCommand(lobby, 1, 0).value().movement.direction, Vec3());
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
  EXPECT_EQ(SpeedOf(on_time.NextCommand(InMatch(std::nullopt), 1, 0)), 1.0F);
  EXPECT_EQ(SpeedOf(run.NextCommand(InMatch(std::nullopt), 1, 0)), 1.0F);
  // State 1000 handed neither sequence 1 nor 2 to the World: the second is
  // due on 1002, late for its offset, and goes all the same.
  EXPECT_EQ(SpeedOf(run.NextCommand(InMatch(kFirstTick, 0), 2, 0)), 2.0F);
}

// Requirements: US-21
TEST(ReenactmentTest, ItCountsTheCommandsAheadOfItsOwnFromTheNewestState) {
  Reenactment run(ThreePlayerScript());
  // State 999 acknowledged sequence 4, of a Match before: sequence 5 is due on 1000, offset 0.
  ASSERT_EQ(SpeedOf(run.NextCommand(InMatch(kFirstTick - 1, 4), 5, 0)), 1.0F);

  // State 1000 acknowledged sequence 5: sequence 6 is due on 1001, offset 1.
  EXPECT_EQ(SpeedOf(run.NextCommand(InMatch(kFirstTick, 5), 6, 0)), 2.0F);
}

// Requirements: US-21
TEST(ReenactmentTest, ItSendsNothingWhileAsManyCommandsAsItKeepsQueuedAreAlreadyWaiting) {
  Reenactment run(ThreePlayerScript());
  const augusta::tick::Tick round_trip = 2;
  // State 1000 handed sequence 1: 2 to 6, five, are in flight or queued, as
  // many as a round trip and kMaxCommandsQueued hold.
  const ServerView view = InMatch(kFirstTick, 1);

  EXPECT_FALSE(run.NextCommand(view, 7, round_trip).has_value());
  EXPECT_TRUE(run.NextCommand(InMatch(kFirstTick + 1, 2), 7, round_trip).has_value());
}

// Leaving drops whatever the server still holds of the player's Commands:
// with more of them on their way than a round trip's worth, it waits for
// every one before its Leave to be handed to the World.
// Requirements: US-21
TEST(ReenactmentTest, ItLeavesOnlyOnceEveryCommandBeforeItsLeaveHasReachedTheWorld) {
  Script script = ThreePlayerScript();
  script.commands = AtOffsets({37, 38, 39});
  Reenactment run(script);
  // Sequences 1 to 3 go for offsets 37 to 39, the last before the Leave at 40.
  for (augusta::command::Sequence sequence = 1; sequence <= 3; ++sequence) {
    ASSERT_TRUE(run.NextCommand(InMatch(kFirstTick + 35 + sequence, sequence - 1), sequence, 0).has_value());
  }
  ASSERT_FALSE(run.NextCommand(InMatch(kFirstTick + 39, 2), 4, 0).has_value());

  // Tick 1039 is late enough, but the server still holds sequence 3.
  EXPECT_EQ(run.Check(InMatch(kFirstTick + 39, 2), 0), Progress::kPlaying);
  EXPECT_EQ(run.Check(InMatch(kFirstTick + 39, 3), 0), Progress::kLeave);
}

// Requirements: US-21
TEST(ReenactmentTest, ItLeavesNoSoonerThanItsDisconnectWouldReachTheServerOnTheLeavesTick) {
  Script script = ThreePlayerScript();
  script.commands = AtOffsets({39});
  Reenactment run(script);
  ASSERT_TRUE(run.NextCommand(InMatch(kFirstTick + 36, 0), 1, 2).has_value());

  EXPECT_EQ(run.Check(InMatch(kFirstTick + 36, 1), 2), Progress::kPlaying);
  EXPECT_EQ(run.Check(InMatch(kFirstTick + 37, 1), 2), Progress::kLeave);
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
TEST(PairDeathsTest, EachCapturedDeathIsPairedWithTheFirstToldOfItsVictimAndTheRestFollow) {
  const Script script = ThreePlayerScript();
  const std::vector<ObservedDeath> observed = {
      {.tick = kFirstTick + 47, .victim = EntityId{21}, .killer = EntityId{23}},
      {.tick = kFirstTick + 31, .victim = EntityId{23}, .killer = EntityId{22}},
      {.tick = kFirstTick + 33, .victim = EntityId{23}, .killer = EntityId{21}},
  };

  const std::vector<augusta::harness::PairedDeath> pairs = PairDeaths(script, InMatch(std::nullopt), observed);

  ASSERT_EQ(pairs.size(), 3U);
  EXPECT_EQ(pairs[0].captured->offset, 30U);
  EXPECT_EQ(pairs[0].observed->offset, 31U);
  EXPECT_EQ(pairs[0].observed->killer, 2);
  EXPECT_EQ(pairs[1].captured->offset, 45U);
  EXPECT_EQ(pairs[1].observed->offset, 47U);
  EXPECT_FALSE(pairs[2].captured.has_value());
  EXPECT_EQ(pairs[2].observed->victim, 3);
  EXPECT_EQ(pairs[2].observed->offset, 33U);
}

// Requirements: US-21
TEST(ReenactmentTest, ItsOutcomePutsEachCapturedDeathAndTheMatchEndBesideWhatTheServerTold) {
  const Reenactment run(ThreePlayerScript());
  ServerView ended = InMatch(std::nullopt);
  ended.in_match = false;
  ended.match_end = augusta::harness::MatchEnd{.winner = SessionId{13}};
  const std::vector<ObservedDeath> observed = {
      {.tick = kFirstTick + 31, .victim = EntityId{23}, .killer = EntityId{21}},
      {.tick = kFirstTick + 35, .victim = EntityId{22}, .killer = EntityId{23}},
  };

  const std::vector<std::string> lines = run.Outcome(ended, observed, kFirstTick + 52, Progress::kEnded);

  EXPECT_EQ(lines, (std::vector<std::string>{
                       "event=death victim=3 killer=1 captured_offset=30 observed_offset=31 observed_killer=1",
                       "event=death victim=1 killer=3 captured_offset=45 observed=none",
                       "event=death victim=2 killer=3 captured=none observed_offset=35",
                       "event=match_end captured_offset=50 captured_winner=1 observed_offset=52 observed_winner=3",
                   }));
}

// What came after its Leave the client no longer saw: it says so, rather
// than that the server never told it.
// Requirements: US-21
TEST(ReenactmentTest, APlayerThatLeftSaysItSawNothingAfterItsLeave) {
  const Reenactment run(ThreePlayerScript());

  const std::vector<std::string> lines = run.Outcome(InMatch(kFirstTick + 40), {}, kFirstTick + 40, Progress::kLeave);

  EXPECT_EQ(lines, (std::vector<std::string>{
                       "event=death victim=3 killer=1 captured_offset=30 observed=none",
                       "event=death victim=1 killer=3 captured_offset=45 observed=left",
                       "event=match_end captured_offset=50 captured_winner=1 observed=left observed_offset=40",
                   }));
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

// --- A run against a server's queue over a link ---

// A Captured player's client and a server's command queue (server::
// CommandQueue) over a link of one_way ticks each way, the server ticking
// every server_period units of time and the client every client_period: how
// many Commands the queue dropped, each one it handed to the World and on
// which tick, and when the player's body was taken out, if it left.
struct LinkRun {
  struct Handed {
    Tick tick = 0;
    Command command{};
  };
  std::vector<Handed> handed;
  int dropped = 0;
  std::size_t most_queued = 0;
  // The Commands still queued when the player's body was taken out, and the tick it was before.
  std::size_t lost_at_leave = 0;
  std::optional<Tick> left_before;
};

LinkRun RunOverALink(const Script& script, int server_period, int client_period, int one_way, int server_ticks) {
  struct Message {
    int arrives = 0;
    augusta::server::SequencedCommand command;
  };
  struct State {
    int arrives = 0;
    Tick tick = 0;
    augusta::command::Sequence acknowledged = 0;
  };
  Reenactment run(script);
  augusta::server::CommandQueue queue(kTickRate);
  LinkRun result;
  std::vector<Message> to_server;
  std::vector<State> to_client;
  std::optional<State> newest;
  std::optional<int> disconnect_arrives;
  bool connected = true;
  augusta::command::Sequence next_sequence = 1;
  const int latency = one_way * server_period;
  const Tick round_trip = 2 * static_cast<Tick>(one_way);
  for (int now = 0; now <= server_ticks * server_period; ++now) {
    std::erase_if(to_server, [&](const Message& message) {
      if (message.arrives > now || !connected) {
        return message.arrives <= now;
      }
      const auto enqueued = queue.TryEnqueue(message.command);
      result.dropped += enqueued == augusta::server::Enqueued::kDroppedOldest ? 1 : 0;
      result.most_queued = std::max(result.most_queued, queue.Queued());
      return true;
    });
    if (now % server_period == 0) {
      const Tick tick = kFirstTick + static_cast<Tick>(now / server_period);
      if (connected && disconnect_arrives.has_value() && *disconnect_arrives <= now) {
        connected = false;
        result.lost_at_leave = queue.Queued();
        result.left_before = tick;
      }
      if (connected) {
        const augusta::server::TickCommand next = queue.Next();
        if (next.sent) {
          result.handed.push_back({.tick = tick, .command = next.command});
        }
        to_client.push_back({.arrives = now + latency, .tick = tick, .acknowledged = next.acknowledged_sequence});
      }
    }
    std::erase_if(to_client, [&](const State& state) {
      if (state.arrives <= now && (!newest.has_value() || state.tick > newest->tick)) {
        newest = state;
      }
      return state.arrives <= now;
    });
    if (now % client_period == 0 && !disconnect_arrives.has_value()) {
      const ServerView view = newest.has_value() ? InMatch(newest->tick, newest->acknowledged) : InMatch(std::nullopt);
      if (run.Check(view, round_trip) == Progress::kLeave) {
        disconnect_arrives = now + latency;
        continue;
      }
      if (const std::optional<Command> command = run.NextCommand(view, next_sequence, round_trip)) {
        to_server.push_back({.arrives = now + latency, .command = {.sequence = next_sequence++, .command = *command}});
      }
    }
  }
  return result;
}

// A Script of player 2 of ThreePlayerScript's, walking with a view of its own
// on every offset from 10 to 59 but for a gap from 30 to 34, each seen 3 ticks
// before it was handed to the World, and no Leave.
Script WalkingScript() {
  Script script = ThreePlayerScript();
  script.commands.clear();
  for (std::uint32_t offset = 10; offset < 60; ++offset) {
    if (offset >= 30 && offset < 35) {
      continue;
    }
    Command command = Walk(1.0F);
    command.yaw = 0.01F * static_cast<float>(offset);
    script.commands.push_back(
        {.offset = offset, .seen_offset = static_cast<std::int32_t>(offset) - 3, .command = command});
  }
  script.leave.reset();
  script.end.reset();
  return script;
}

// Each of script's Commands handed, in order, by the yaw it alone has: its
// offset and its Seen time's delay as run handed it, or nullopt if it never was.
std::vector<std::optional<std::pair<std::int64_t, std::int64_t>>> AsHanded(const Script& script, const LinkRun& run) {
  std::vector<std::optional<std::pair<std::int64_t, std::int64_t>>> handed;
  std::size_t from = 0;
  for (const CapturedCommand& captured : script.commands) {
    std::optional<std::pair<std::int64_t, std::int64_t>> found;
    for (std::size_t i = from; i < run.handed.size(); ++i) {
      if (run.handed[i].command.yaw == captured.command.yaw &&
          run.handed[i].command.movement.direction == captured.command.movement.direction) {
        const auto tick = static_cast<std::int64_t>(run.handed[i].tick);
        found = std::pair{tick - static_cast<std::int64_t>(kFirstTick),
                          tick - static_cast<std::int64_t>(run.handed[i].command.seen_tick)};
        from = i + 1;
        break;
      }
    }
    handed.push_back(found);
  }
  return handed;
}

// A client whose clock runs a fifth fast sends as many Commands as the server
// hands, never more: the queue stays short, none is dropped, and each reaches
// the World on its offset with its Seen time's delay.
// Requirements: US-21
TEST(ReenactmentLinkTest, AClientFasterThanTheServerNeverOverrunsItsQueueAndKeepsEveryCommandOnItsTick) {
  const Script script = WalkingScript();

  const LinkRun run = RunOverALink(script, /*server_period=*/6, /*client_period=*/5, /*one_way=*/3, 80);

  EXPECT_EQ(run.dropped, 0);
  EXPECT_LE(run.most_queued, augusta::harness::kMaxCommandsQueued + 2);
  const auto handed = AsHanded(script, run);
  for (std::size_t i = 0; i < script.commands.size(); ++i) {
    const CapturedCommand& captured = script.commands[i];
    ASSERT_TRUE(handed[i].has_value()) << "the Command of offset " << captured.offset << " never arrived";
    EXPECT_LE(std::abs(handed[i]->first - captured.offset), 2) << "offset " << captured.offset;
    EXPECT_EQ(handed[i]->second, 3) << "the Seen time of offset " << captured.offset;
  }
}

// A client whose clock runs slow falls behind, but drops nothing: every
// Command reaches the World, in order, with its Seen time's delay.
// Requirements: US-21
TEST(ReenactmentLinkTest, AClientSlowerThanTheServerFallsBehindButLosesNoCommandNorItsSeenTimesDelay) {
  const Script script = WalkingScript();

  const LinkRun run = RunOverALink(script, /*server_period=*/6, /*client_period=*/7, /*one_way=*/2, 120);

  EXPECT_EQ(run.dropped, 0);
  const auto handed = AsHanded(script, run);
  for (std::size_t i = 0; i < script.commands.size(); ++i) {
    const CapturedCommand& captured = script.commands[i];
    ASSERT_TRUE(handed[i].has_value()) << "the Command of offset " << captured.offset << " never arrived";
    // Never more than a tick early: the client's newest State is that stale at most.
    EXPECT_GE(handed[i]->first, static_cast<std::int64_t>(captured.offset) - 1) << "offset " << captured.offset;
    EXPECT_LE(std::abs(handed[i]->second - 3), 1) << "the Seen time of offset " << captured.offset;
  }
}

// With a round trip of 6 ticks, more Commands are on their way when the
// Leave comes than the round trip alone tells: every one before it still
// reaches the World, and the body is taken out on the Leave's tick or as
// soon after as the disconnect can reach the server.
// Requirements: US-21
TEST(ReenactmentLinkTest, APlayerThatLeavesLosesNoCommandBeforeItsLeave) {
  Script script = WalkingScript();
  script.commands.erase(std::remove_if(script.commands.begin(), script.commands.end(),
                                       [](const CapturedCommand& command) { return command.offset >= 40; }),
                        script.commands.end());
  script.leave = 40;

  const LinkRun run = RunOverALink(script, /*server_period=*/6, /*client_period=*/5, /*one_way=*/3, 80);

  EXPECT_EQ(run.lost_at_leave, 0U);
  for (const auto& handed : AsHanded(script, run)) {
    EXPECT_TRUE(handed.has_value());
  }
  ASSERT_TRUE(run.left_before.has_value());
  EXPECT_GE(*run.left_before, kFirstTick + 40);
  EXPECT_LE(*run.left_before, kFirstTick + 40 + 3 + 2);
}

}  // namespace
