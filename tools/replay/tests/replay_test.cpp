#include "replay.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "recording.h"
#include "scripted_match.h"

// Replaying a match recording (ADR-0048), on the example scenario as the server
// loads it from its golden server pack, and the golden match recorded from it.
namespace {

using augusta::math::Vec3;
using augusta::replay::DivergenceKind;
using augusta::replay::kAcrossBuilds;
using augusta::replay::kSameBuild;
using augusta::scripted_match::kDeltaTime;
using augusta::scripted_match::kFirst;
using augusta::scripted_match::LoadExampleContent;
using augusta::scripted_match::LoadExamplePack;
using augusta::scripted_match::Read;
using augusta::scripted_match::RecordAWalk;
using augusta::scripted_match::RecordScriptedMatch;
using augusta::server::Recording;
using augusta::server::TickOutcome;
using augusta::simulation::SessionId;

// A divergence's tick, kind and both sides' bodies, every float in hex so a
// last-bit difference shows: what a failing replay test reports.
std::string Describe(const augusta::replay::Divergence& divergence) {
  std::string text =
      std::format("diverged on tick {}: {}", divergence.tick, augusta::replay::DescribeDivergenceKind(divergence.kind));
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

  const auto across = augusta::replay::Replay(recording, LoadExampleContent(), kAcrossBuilds);
  ASSERT_TRUE(across.has_value()) << "diverged on tick " << across.error().tick << ": "
                                  << augusta::replay::DescribeDivergenceKind(across.error().kind);
  EXPECT_EQ(*across, recording.ticks.size());

  const auto exact = augusta::replay::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_FALSE(exact.has_value());
  EXPECT_EQ(exact.error().tick, 1U);
}

TEST(ReplayTest, ATickTheWorldNumbersOtherwiseDiverges) {
  Recording recording = Read(RecordScriptedMatch());
  recording.ticks[3].outcome.tick = 7;
  const auto replayed = augusta::replay::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_FALSE(replayed.has_value());
  EXPECT_EQ(replayed.error().kind, DivergenceKind::kTick) << Describe(replayed.error());
}

TEST(ReplayTest, ARecordingReplaysToExactlyTheSameOutcomeOnTheBuildThatMadeIt) {
  const Recording recording = Read(RecordScriptedMatch());
  const auto replayed = augusta::replay::Replay(recording, LoadExampleContent(), kSameBuild);
  ASSERT_TRUE(replayed.has_value()) << Describe(replayed.error());
  EXPECT_EQ(*replayed, recording.ticks.size());
}

TEST(ReplayTest, AnOutcomeThatDiffersIsTheFirstDivergence) {
  Recording recording = Read(RecordScriptedMatch());
  recording.ticks[5].outcome.bodies[1].health -= 1.0F;
  recording.ticks[9].outcome.bodies[0].body.position.x += 1.0F;
  const auto replayed = augusta::replay::Replay(recording, LoadExampleContent(), kAcrossBuilds);
  ASSERT_FALSE(replayed.has_value());
  EXPECT_EQ(replayed.error().tick, 6U) << Describe(replayed.error());
  EXPECT_EQ(replayed.error().kind, DivergenceKind::kBodies);
}

TEST(ReplayTest, ACharacterTheContentLacksStopsTheReplay) {
  Recording recording = Read(RecordScriptedMatch());
  recording.ticks[0].input.match_start[0].identity.character = "characters/nobody";
  const auto replayed = augusta::replay::Replay(recording, LoadExampleContent(), kSameBuild);
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
  EXPECT_EQ(augusta::replay::FindDivergence(OneBody(), OneBody(), kDeltaTime, kSameBuild), std::nullopt);
}

TEST_F(DivergenceTest, OnTheSameBuildAPositionAStepOffDiverges) {
  TickOutcome replayed = OneBody();
  replayed.bodies[0].body.position.y += kStep;
  EXPECT_EQ(augusta::replay::FindDivergence(OneBody(), replayed, kDeltaTime, kSameBuild), DivergenceKind::kBodies);
}

TEST_F(DivergenceTest, AcrossBuildsAPositionAndAShotOriginAStepOffDoNotDivergeButTwoStepsDo) {
  TickOutcome replayed = OneBody();
  replayed.bodies[0].body.position.y += kStep;
  replayed.shots[0].origin.y += kStep;
  // Within what both ends of a tick's displacement a step off make, 0.117 m/s at 60 Hz.
  replayed.bodies[0].body.velocity.z += 0.1F;
  EXPECT_EQ(augusta::replay::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), std::nullopt);
  replayed.shots[0].origin.y += kStep;
  EXPECT_EQ(augusta::replay::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kShots);
}

TEST_F(DivergenceTest, AcrossBuildsEveryOtherValueMustStillBeEqual) {
  TickOutcome replayed = OneBody();
  replayed.bodies[0].yaw += 1.0F / 2097152.0F;
  EXPECT_EQ(augusta::replay::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kBodies);
}

TEST_F(DivergenceTest, ABodyMissingDiverges) {
  TickOutcome replayed = OneBody();
  replayed.bodies.clear();
  EXPECT_EQ(augusta::replay::FindDivergence(OneBody(), replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kBodies);
}

TEST_F(DivergenceTest, AMatchEndWithAnotherWinnerDiverges) {
  TickOutcome recorded = OneBody();
  recorded.match_end = augusta::simulation::MatchEnd{.winner = SessionId{1}};
  TickOutcome replayed = recorded;
  replayed.match_end->winner = std::nullopt;
  EXPECT_EQ(augusta::replay::FindDivergence(recorded, replayed, kDeltaTime, kAcrossBuilds), DivergenceKind::kMatchEnd);
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
  const auto replayed = augusta::replay::Replay(*recording, LoadExampleContent(), kAcrossBuilds);
  ASSERT_TRUE(replayed.has_value()) << Describe(replayed.error());
  EXPECT_EQ(*replayed, recording->ticks.size());
}

// Not a check: rewrites the golden match when the simulation changes on
// purpose, as the augusta_golden_match build target does (tools/replay/tests/CMakeLists.txt).
TEST(GoldenMatchTest, DISABLED_RegenerateGoldenMatch) {
  const std::string bytes = RecordScriptedMatch();
  std::ofstream out(AUGUSTA_GOLDEN_MATCH, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good());
}

}  // namespace
