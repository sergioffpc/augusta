#include "recording.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "content.h"
#include "replay.h"
#include "simulation_mapping.h"

// A match recording and its replay (ADR-0048), on the example scenario as the
// server loads it from its golden server pack: its Map, its characters and its
// Game policy, whose last player standing wins.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::server::Content;
using augusta::server::DivergenceKind;
using augusta::server::kAcrossBuilds;
using augusta::server::kSameBuild;
using augusta::server::RecordedEntrant;
using augusta::server::RecordedSimulation;
using augusta::server::Recorder;
using augusta::server::Recording;
using augusta::server::RecordingError;
using augusta::server::RecordingHeader;
using augusta::server::TickOutcome;
using augusta::simulation::EntityId;
using augusta::simulation::MatchPlayer;
using augusta::simulation::PlayerCommand;
using augusta::simulation::SessionId;
using augusta::simulation::TickResult;

constexpr std::uint8_t kTickRate = 60;
constexpr float kDeltaTime = 1.0F / kTickRate;

// Long enough for the scripted shooter to kill, at any fire rate the example's Parameters give.
constexpr int kMaxMatchTicks = 600;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};

augusta::assets::Pack LoadExamplePack() {
  auto pack = augusta::assets::LoadVerifiedPack(kPacks / "server.pack", kPacks / "test.pub");
  if (!pack.has_value()) {
    throw std::runtime_error("the example server pack does not load");
  }
  return *std::move(pack);
}

Content LoadExampleContent() {
  auto content = augusta::server::LoadServerContent(LoadExamplePack(), kTickRate);
  if (!content.has_value()) {
    throw std::runtime_error("the example server pack's content does not load");
  }
  return *std::move(content);
}

RecordingHeader ExampleHeader() {
  return RecordingHeader{.engine_version = std::string(augusta::EngineVersion()),
                         .server_pack = LoadExamplePack().Hash(),
                         .tick_rate_hz = kTickRate};
}

// A World on the example's content, recording to recorder if it is given one.
RecordedSimulation ExampleSimulation(Content content, std::optional<Recorder> recorder) {
  return RecordedSimulation(
      augusta::server::BuildSimulation(content.parameters, kTickRate, content.scenario, std::move(content.policy)),
      std::move(recorder));
}

// The two players of the scripted match, sessions 1 and 2.
constexpr EntityId kFirst{1};
constexpr EntityId kSecond{2};

std::vector<MatchPlayer> ExampleEntrants(const Content& content) {
  const auto characters = augusta::server::ToSimulation(content.scenario.characters);
  const std::string& path = content.scenario.characters.front().path;
  return {
      MatchPlayer{
          .entity = kFirst, .identity = {.session = SessionId{1}, .character = path}, .character = characters.at(path)},
      MatchPlayer{.entity = kSecond,
                  .identity = {.session = SessionId{2}, .character = path},
                  .character = characters.at(path)},
  };
}

// The view, on the angle grid, that looks from from to to.
Command Aiming(const Vec3& from, const Vec3& to) {
  const Vec3 along = to - from;
  Command command;
  command.yaw = augusta::math::SnapAngle(std::atan2(-along.x, -along.z));
  command.pitch = augusta::math::SnapAngle(std::atan2(along.y, std::hypot(along.x, along.z)));
  return command;
}

// The scripted match, a duel: each player stands, looks at the other's chest
// and taps its trigger every tenth tick, until they kill each other on the same
// tick and Game policy ends the Match as a Draw; then the Match is ended in the
// world, as the server ends it after the tick, and one more tick runs. Returns
// every tick's result.
std::vector<TickResult> PlayScriptedMatch(RecordedSimulation& simulation, const Content& content) {
  constexpr int kTapEvery = 10;
  const std::vector<Vec3> spawns = simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  const Vec3 eye = content.scenario.characters.front().eye;
  const Vec3 chest = eye * 0.75F;
  const Command first_aim = Aiming(spawns[0] + eye, spawns[1] + chest);
  const Command second_aim = Aiming(spawns[1] + eye, spawns[0] + chest);
  std::vector<TickResult> results;
  augusta::tick::Tick last_tick = 0;
  for (int i = 0; i < kMaxMatchTicks; ++i) {
    Command first = first_aim;
    first.fire = i % kTapEvery == 0;
    first.seen_tick = last_tick;
    Command second = second_aim;
    second.fire = first.fire;
    second.seen_tick = last_tick;
    results.push_back(simulation.Tick(
        {PlayerCommand{.entity = kFirst, .command = first}, PlayerCommand{.entity = kSecond, .command = second}},
        kDeltaTime));
    last_tick = results.back().state.tick;
    if (!results.back().actions.empty()) {
      break;
    }
  }
  simulation.EndMatch();
  results.push_back(simulation.Tick({}, kDeltaTime));
  return results;
}

// The scripted match, recorded.
std::string RecordScriptedMatch() {
  std::ostringstream out(std::ios::binary);
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), Recorder(out, ExampleHeader()));
  PlayScriptedMatch(simulation, content);
  return std::move(out).str();
}

// Both players walking the same way for a second, recorded: bodies that move
// on every tick.
std::string RecordAWalk() {
  constexpr int kWalkTicks = 60;
  std::ostringstream out(std::ios::binary);
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), Recorder(out, ExampleHeader()));
  simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  Command walk;
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  for (int i = 0; i < kWalkTicks; ++i) {
    simulation.Tick(
        {PlayerCommand{.entity = kFirst, .command = walk}, PlayerCommand{.entity = kSecond, .command = walk}},
        kDeltaTime);
  }
  return std::move(out).str();
}

Recording Read(const std::string& bytes) {
  std::istringstream in(bytes, std::ios::binary);
  auto recording = augusta::server::ReadRecording(in);
  EXPECT_TRUE(recording.has_value());
  return recording.value_or(Recording{});
}

// A divergence's tick, kind and both sides' bodies, every float in hex so a
// last-bit difference shows: what a failing replay test reports.
std::string Describe(const augusta::server::Divergence& divergence) {
  std::string text =
      std::format("diverged on tick {}: {}", divergence.tick, augusta::server::DescribeDivergenceKind(divergence.kind));
  for (const auto* side : {&divergence.recorded, &divergence.replayed}) {
    text += side == &divergence.recorded ? "\n recorded" : "\n replayed";
    for (const auto& body : side->bodies) {
      const augusta::physics::BodyState& state = body.body;
      text += std::format(
          "\n  body {}: position ({:a}, {:a}, {:a}) velocity ({:a}, {:a}, {:a}) stance {} stamina {:a} exhausted {} "
          "yaw {:a} health {:a} cooldown {:a} reload {:a} recoil ({:a}, {:a}) rounds {} burst {}",
          static_cast<std::uint32_t>(body.entity), state.position.x, state.position.y, state.position.z,
          state.velocity.x, state.velocity.y, state.velocity.z, static_cast<int>(state.stance), state.stamina,
          state.exhausted, body.yaw, body.health, body.rifle.cooldown, body.rifle.reload_remaining,
          body.rifle.recoil.pitch, body.rifle.recoil.yaw, body.rifle.rounds, body.rifle.burst_index);
    }
  }
  return text;
}

TEST(RecordingTest, TheScriptedMatchEndsInADraw) {
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), std::nullopt);
  const std::vector<TickResult> results = PlayScriptedMatch(simulation, content);
  ASSERT_GE(results.size(), 2U);
  const TickResult& ending = results[results.size() - 2];
  ASSERT_EQ(ending.actions.size(), 1U);
  EXPECT_EQ(std::get<augusta::simulation::MatchEnd>(ending.actions.front()).winner, std::nullopt);
  EXPECT_EQ(ending.state.deaths.size(), 2U);
}

TEST(RecordingTest, ARecordingStartsWithItsHeader) { EXPECT_EQ(Read(RecordScriptedMatch()).header, ExampleHeader()); }

TEST(RecordingTest, EveryTickIsRecordedWithTheCommandsItRanOn) {
  const Recording recording = Read(RecordScriptedMatch());
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), std::nullopt);
  const std::vector<TickResult> results = PlayScriptedMatch(simulation, content);
  ASSERT_EQ(recording.ticks.size(), results.size());
  const auto& first = recording.ticks.front().input;
  ASSERT_EQ(first.commands.size(), 2U);
  EXPECT_EQ(first.commands[0].entity, kFirst);
  EXPECT_TRUE(first.commands[0].command.fire);
  EXPECT_FALSE(recording.ticks[1].input.commands[0].command.fire);
  EXPECT_EQ(first.delta_time, kDeltaTime);
  EXPECT_FALSE(recording.torn);
}

TEST(RecordingTest, AMatchStartIsRecordedBeforeItsFirstTickWithWhereItsPlayersSpawned) {
  const Recording recording = Read(RecordScriptedMatch());
  const auto& first = recording.ticks.front();
  ASSERT_EQ(first.input.match_start.size(), 2U);
  EXPECT_EQ(first.input.match_start[1].entity, kSecond);
  EXPECT_EQ(first.input.match_start[1].identity.session, SessionId{2});
  EXPECT_EQ(first.outcome.spawns.size(), 2U);
  EXPECT_TRUE(recording.ticks[1].input.match_start.empty());
}

TEST(RecordingTest, TheMatchPolicyEndsAndTheEndingOfTheMatchInTheWorldAreRecorded) {
  const Recording recording = Read(RecordScriptedMatch());
  const auto& ending = recording.ticks[recording.ticks.size() - 2];
  ASSERT_TRUE(ending.outcome.match_end.has_value());
  EXPECT_EQ(ending.outcome.match_end->winner, std::nullopt);
  EXPECT_EQ(ending.outcome.deaths.size(), 2U);
  EXPECT_FALSE(ending.outcome.hits.empty());
  EXPECT_TRUE(recording.ticks.back().input.match_ended);
  EXPECT_FALSE(ending.input.match_ended);
}

TEST(RecordingTest, ARemovedBodyIsRecordedBeforeTheTickItLeftOn) {
  std::ostringstream out(std::ios::binary);
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), Recorder(out, ExampleHeader()));
  simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  simulation.Tick({}, kDeltaTime);
  simulation.RemovePlayer(kSecond);
  simulation.Tick({}, kDeltaTime);
  const Recording recording = Read(std::move(out).str());
  ASSERT_EQ(recording.ticks.size(), 2U);
  EXPECT_TRUE(recording.ticks[0].input.removed.empty());
  EXPECT_EQ(recording.ticks[1].input.removed, std::vector<EntityId>{kSecond});
  EXPECT_EQ(recording.ticks[1].outcome.bodies.size(), 1U);
}

TEST(RecordingTest, ALastRecordCutShortIsDroppedAndReported) {
  const std::string bytes = RecordScriptedMatch();
  const Recording whole = Read(bytes);
  const Recording torn = Read(bytes.substr(0, bytes.size() - 3));
  EXPECT_TRUE(torn.torn);
  EXPECT_EQ(torn.ticks.size(), whole.ticks.size() - 1);
}

TEST(RecordingTest, AStreamThatDoesNotStartWithAHeaderIsNoRecording) {
  std::istringstream empty(std::string{}, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(empty).error(), RecordingError::kNoHeader);
  const std::string garbage("\x02\x00\x00\x00\x09\x09", 6);
  std::istringstream in(garbage, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kNoHeader);
}

TEST(RecordingTest, ARecordThatDoesNotDecodeIsMalformed) {
  std::string bytes = RecordScriptedMatch();
  // The first tick's type byte, after the header's length and payload and the tick's length.
  const auto header_size = static_cast<std::size_t>(static_cast<unsigned char>(bytes[0]));
  bytes[4 + header_size + 4] = '\x07';
  std::istringstream in(bytes, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kMalformed);
}

TEST(RecordingTest, AFileThatCannotBeOpenedIsUnreadable) {
  std::ifstream in(std::filesystem::temp_directory_path() / "augusta_no_such_recording.rec", std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kUnreadable);
}

TEST(RecordingTest, ARecordLongerThanAnyTickCanMakeIsMalformedAndNotReadIn) {
  std::string bytes = RecordScriptedMatch();
  const auto header_size = static_cast<std::size_t>(static_cast<unsigned char>(bytes[0]));
  // The first tick's length, made about 2 GB.
  bytes.replace(4 + header_size, 4, std::string("\xFF\xFF\xFF\x7F", 4));
  std::istringstream in(bytes, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kMalformed);
}

// A build that rounds a moving body one grid step further on every tick: each
// tick's recorded bodies are one more step along x than the last's. Replayed
// across builds, every tick starts from the recorded bodies, so each is a
// single step off and the difference never grows; replayed exactly, the first
// step is a divergence.
TEST(ReplayTest, AcrossBuildsAStepOffOnEveryTickDoesNotCompound) {
  constexpr float kStep = 1.0F / 1024.0F;
  Recording recording = Read(RecordAWalk());
  for (std::size_t i = 0; i < recording.ticks.size(); ++i) {
    for (augusta::simulation::EntityState& body : recording.ticks[i].outcome.bodies) {
      body.body.position.x += kStep * static_cast<float>(i + 1);
    }
  }

  const auto across = augusta::server::Replay(recording, LoadExampleContent(), kAcrossBuilds);
  ASSERT_TRUE(across.has_value()) << "diverged on tick " << across.error().tick << ": "
                                  << augusta::server::DescribeDivergenceKind(across.error().kind);
  EXPECT_EQ(*across, recording.ticks.size());

  const auto exact = augusta::server::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_FALSE(exact.has_value());
  EXPECT_EQ(exact.error().tick, 1U);
}

TEST(ReplayTest, ATickTheWorldNumbersOtherwiseDiverges) {
  Recording recording = Read(RecordScriptedMatch());
  recording.ticks[3].outcome.tick = 7;
  const auto replayed = augusta::server::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_FALSE(replayed.has_value());
  EXPECT_EQ(replayed.error().kind, DivergenceKind::kTick) << Describe(replayed.error());
}

TEST(ReplayTest, ARecordingReplaysToExactlyTheSameOutcomeOnTheBuildThatMadeIt) {
  const Recording recording = Read(RecordScriptedMatch());
  const auto replayed = augusta::server::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_TRUE(replayed.has_value()) << Describe(replayed.error());
  EXPECT_EQ(*replayed, recording.ticks.size());
}

TEST(ReplayTest, AnOutcomeThatDiffersIsTheFirstDivergence) {
  Recording recording = Read(RecordScriptedMatch());
  recording.ticks[5].outcome.bodies[1].health -= 1.0F;
  recording.ticks[9].outcome.bodies[0].body.position.x += 1.0F;
  const auto replayed = augusta::server::Replay(recording, LoadExampleContent(), kAcrossBuilds);
  ASSERT_FALSE(replayed.has_value());
  EXPECT_EQ(replayed.error().tick, 6U) << Describe(replayed.error());
  EXPECT_EQ(replayed.error().kind, DivergenceKind::kBodies);
}

TEST(ReplayTest, ACharacterTheContentLacksStopsTheReplay) {
  Recording recording = Read(RecordScriptedMatch());
  recording.ticks[0].input.match_start[0].identity.character = "characters/nobody";
  const auto replayed = augusta::server::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_FALSE(replayed.has_value());
  EXPECT_EQ(replayed.error().kind, DivergenceKind::kUnknownCharacter);
}

// What FindDivergence compares, apart from a World.
class DivergenceTest : public ::testing::Test {
 protected:
  static TickOutcome OneBody() {
    TickOutcome outcome;
    outcome.bodies.push_back(augusta::simulation::EntityState{.entity = kFirst,
                                                              .body = {.position = Vec3(1.0F, 0.0F, 2.0F),
                                                                       .velocity = Vec3(0.0F, 0.0F, 3.0F),
                                                                       .stance = augusta::physics::Stance::kStanding,
                                                                       .stamina = 1.0F,
                                                                       .exhausted = false},
                                                              .yaw = 0.5F,
                                                              .health = 100.0F,
                                                              .rifle = {}});
    outcome.shots.push_back(
        augusta::simulation::Shot{.shooter = kFirst, .origin = Vec3(1.0F, 1.5F, 2.0F), .yaw = 0.5F, .pitch = 0.0F});
    return outcome;
  }

  static constexpr float kStep = 1.0F / 1024.0F;
};

TEST_F(DivergenceTest, TheSameOutcomeHasNone) {
  EXPECT_EQ(augusta::server::FindDivergence(OneBody(), OneBody(), kDeltaTime, kSameBuild), std::nullopt);
}

TEST_F(DivergenceTest, OnTheSameBuildAPositionAStepOffDiverges) {
  TickOutcome replayed = OneBody();
  replayed.bodies[0].body.position.y += kStep;
  EXPECT_EQ(augusta::server::FindDivergence(OneBody(), replayed, kDeltaTime, kSameBuild), DivergenceKind::kBodies);
}

TEST_F(DivergenceTest, AcrossBuildsAPositionAndAShotOriginAStepOffDoNotDivergeButTwoStepsDo) {
  TickOutcome replayed = OneBody();
  replayed.bodies[0].body.position.y += kStep;
  replayed.shots[0].origin.y += kStep;
  // Within what both ends of a tick's displacement a step off make, 0.117 m/s at 60 Hz.
  replayed.bodies[0].body.velocity.z += 0.1F;
  EXPECT_EQ(augusta::server::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), std::nullopt);
  replayed.shots[0].origin.y += kStep;
  EXPECT_EQ(augusta::server::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kShots);
}

TEST_F(DivergenceTest, AcrossBuildsEveryOtherValueMustStillBeEqual) {
  TickOutcome replayed = OneBody();
  replayed.bodies[0].yaw += 1.0F / 2097152.0F;
  EXPECT_EQ(augusta::server::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kBodies);
}

TEST_F(DivergenceTest, ABodyMissingDiverges) {
  TickOutcome replayed = OneBody();
  replayed.bodies.clear();
  EXPECT_EQ(augusta::server::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kBodies);
}

TEST_F(DivergenceTest, AMatchEndWithAnotherWinnerDiverges) {
  TickOutcome recorded = OneBody();
  recorded.match_end = augusta::simulation::MatchEnd{.winner = SessionId{1}};
  TickOutcome replayed = recorded;
  replayed.match_end->winner = std::nullopt;
  EXPECT_EQ(augusta::server::FindDivergence(recorded, replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kMatchEnd);
}

// The golden match (ADR-0013, ADR-0048): the scripted match as recorded in the
// repository replays to its recorded outcome on any build, within a grid step
// of position. A deliberate change to the simulation rewrites it with one build
// target, and the diff is reviewed like the golden trajectories'.
TEST(GoldenMatchTest, TheGoldenMatchReplaysToItsRecordedOutcome) {
  std::ifstream in(AUGUSTA_GOLDEN_MATCH, std::ios::binary);
  ASSERT_TRUE(in) << "missing " << AUGUSTA_GOLDEN_MATCH;
  const auto recording = augusta::server::ReadRecording(in);
  ASSERT_TRUE(recording.has_value()) << augusta::server::DescribeRecordingError(recording.error());
  ASSERT_EQ(recording->header.server_pack, LoadExamplePack().Hash()) << "regenerate " << AUGUSTA_GOLDEN_MATCH;
  const auto replayed = augusta::server::Replay(*recording, LoadExampleContent(), kAcrossBuilds);
  ASSERT_TRUE(replayed.has_value()) << Describe(replayed.error());
  EXPECT_EQ(*replayed, recording->ticks.size());
}

// Not a check: rewrites the golden match when the simulation changes on
// purpose, as the augusta_golden_match build target does (tests/CMakeLists.txt).
TEST(GoldenMatchTest, DISABLED_RegenerateGoldenMatch) {
  const std::string bytes = RecordScriptedMatch();
  std::ofstream out(AUGUSTA_GOLDEN_MATCH, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good());
}

}  // namespace
