#include "augusta/prediction.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/correction.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/weapon.h"

// Exercises PredictionWorld's public Tick() surface end-to-end - the local
// entity, its sequenced commands and its reconciliation against what the
// server says - rather than physics::World directly (see physics_test.cpp for
// that) or the pure history (see reconciliation_test.cpp).
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::BodyState;
using augusta::physics::CollisionMesh;
using augusta::prediction::Acknowledgement;
using augusta::prediction::State;
using augusta::prediction::World;
using augusta::presentation::Correction;

constexpr float kFixedTick = 1.0F / 60.0F;
constexpr int kSettleTicks = 30;
constexpr float kWalkStep = 3.0F * kFixedTick;  // Metres per tick at walking speed (3 m/s).

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

Command Walking() {
  Command walk{};
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  return walk;
}

TEST(PredictionWorldTest, TicksTheLocalEntityForwardEachCall) {
  World world;

  State state;
  for (int i = 0; i < 30; ++i) {
    state = world.Tick(Walking(), 0, std::nullopt, kFixedTick);
  }

  EXPECT_GT(state.local_body.position.x, 0.0F);
}

TEST(PredictionWorldTest, StartPutsTheLocalPlayerAtTheSpawnPointOnTheFloor) {
  World world;
  ASSERT_TRUE(world.AddCollisionMesh(Floor()).has_value());

  world.Start(Vec3(5.0F, 0.0F, 7.0F), Parameters{});
  State state;
  for (int i = 0; i < kSettleTicks; ++i) {
    state = world.Tick(Command{}, 0, std::nullopt, kFixedTick);
  }

  EXPECT_NEAR(state.local_body.position.x, 5.0F, 0.05F);
  EXPECT_NEAR(state.local_body.position.y, 0.0F, 0.05F);
  EXPECT_NEAR(state.local_body.position.z, 7.0F, 0.05F);
}

TEST(PredictionWorldTest, StartReplacesTheStaminaRulesTheWorldWasBuiltWith) {
  World world;
  Command sprint = Walking();
  sprint.movement.sprint = true;
  EXPECT_FLOAT_EQ(world.Tick(sprint, 0, std::nullopt, kFixedTick).local_body.stamina, 1.0F);

  world.Start(Vec3{},
              Parameters{.stamina = {.deplete_per_second = 1.0F, .regen_per_second = 0.0F, .forced_walk_below = 0.0F}});

  EXPECT_LT(world.Tick(sprint, 0, std::nullopt, kFixedTick).local_body.stamina, 1.0F);
}

// A resting player on a floor, ticked with sequences 1, 2, ... and no input.
class ReconciliationTest : public ::testing::Test {
 protected:
  ReconciliationTest() {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
    for (int i = 0; i < kSettleTicks; ++i) {
      Tick(std::nullopt);
    }
    rest_ = states_.back();
  }

  State Tick(const std::optional<Acknowledgement>& acknowledgement, const Command& command = Command{}) {
    ++sequence_;
    latest_ = world_.Tick(command, sequence_, acknowledgement, kFixedTick);
    states_.push_back(latest_.local_body);
    return latest_;
  }

  // What the server would say after command number sequence, if it had the
  // player somewhere other than the rest position.
  Acknowledgement ServerSays(std::uint32_t sequence, const Vec3& offset) const {
    BodyState body = rest_;
    body.position += offset;
    return Acknowledgement{.sequence = sequence, .body = body};
  }

  // What the client itself predicted after command number sequence, moved by offset.
  [[nodiscard]] Acknowledgement ServerAgreesWithTheClientExcept(std::uint32_t sequence, const Vec3& offset) const {
    BodyState body = states_[sequence - 1];
    body.position += offset;
    return Acknowledgement{.sequence = sequence, .body = body};
  }

  World world_;
  std::uint32_t sequence_ = 0;
  std::vector<BodyState> states_;
  State latest_;
  BodyState rest_{};
};

TEST_F(ReconciliationTest, AnInjectedDivergenceIsAdoptedAtOnce) {
  constexpr float kDivergence = 0.5F;

  // The server answers a command 3 ticks behind the client's newest, as it does at ~50 ms.
  const State state = Tick(ServerSays(sequence_ - 2, Vec3(kDivergence, 0.0F, 0.0F)));

  EXPECT_NEAR(state.local_body.position.x, rest_.position.x + kDivergence, 0.02F);
}

TEST_F(ReconciliationTest, ADivergenceOfAnySizeIsAdoptedAtOnce) {
  for (const float divergence : {0.1F, 3.0F, 10.0F}) {
    const State state = Tick(ServerSays(sequence_, Vec3(divergence, 0.0F, 0.0F)));

    EXPECT_NEAR(state.local_body.position.x, rest_.position.x + divergence, 0.05F) << divergence;

    // Back to where it was, for the next divergence.
    Tick(ServerSays(sequence_, Vec3(0.0F, 0.0F, 0.0F)));
  }
}

TEST_F(ReconciliationTest, ADivergenceUnderAMillimetreIsNotCorrected) {
  const float before = states_.back().position.x;

  const State state = Tick(ServerSays(sequence_, Vec3(0.0005F, 0.0F, 0.0F)));

  EXPECT_EQ(state.total_correction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_NEAR(state.local_body.position.x, before, 0.0001F);
}

TEST_F(ReconciliationTest, ADivergenceOverAMillimetreIsCorrected) {
  const State state = Tick(ServerSays(sequence_, Vec3(0.002F, 0.0F, 0.0F)));

  EXPECT_NEAR(state.total_correction.x, 0.002F, 0.0005F);
}

TEST_F(ReconciliationTest, AStaminaThatDiffersIsCorrectedEvenWhereThePositionAgrees) {
  constexpr float kServerStamina = 0.5F;
  Acknowledgement ack = ServerSays(sequence_, Vec3(0.0F, 0.0F, 0.0F));
  ack.body.stamina = kServerStamina;

  const State state = Tick(ack);

  EXPECT_NEAR(state.local_body.stamina, kServerStamina, 0.01F);
}

// Sprinting forward: the rules the fixture's world runs on never drain, so
// only an exhausted flag can turn this into a walk.
Command SprintingForward() {
  Command sprint = Walking();
  sprint.movement.sprint = true;
  return sprint;
}

constexpr float kSprintStep = 4.8F * kFixedTick;  // Metres per tick at sprinting speed (4.8 m/s).

TEST_F(ReconciliationTest, AnExhaustedFlagThatDiffersIsCorrectedEvenWhereEverythingElseAgrees) {
  Acknowledgement ack = ServerAgreesWithTheClientExcept(sequence_, Vec3(0.0F, 0.0F, 0.0F));
  ASSERT_FALSE(ack.body.exhausted);
  ack.body.exhausted = true;

  const State state = Tick(ack, SprintingForward());

  // Put back at the server's exhausted body, the tick's sprint is walked.
  EXPECT_NEAR(state.local_body.position.x - ack.body.position.x, kWalkStep, 0.01F);
}

TEST_F(ReconciliationTest, AnExhaustedFlagThatAgreesChangesNothing) {
  const Acknowledgement ack = ServerAgreesWithTheClientExcept(sequence_, Vec3(0.0F, 0.0F, 0.0F));

  const State state = Tick(ack, SprintingForward());

  EXPECT_EQ(state.total_correction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_NEAR(state.local_body.position.x - ack.body.position.x, kSprintStep, 0.01F);
}

TEST_F(ReconciliationTest, TheJumpTheReplayMadeIsTheTotalCorrection) {
  constexpr float kDivergence = 0.5F;
  EXPECT_EQ(latest_.total_correction, Vec3(0.0F, 0.0F, 0.0F));

  const State state = Tick(ServerSays(sequence_ - 2, Vec3(kDivergence, 0.0F, 0.0F)));

  EXPECT_NEAR(state.total_correction.x, kDivergence, 0.02F);
}

TEST_F(ReconciliationTest, StartingOverKeepsTheTotalCorrection) {
  const State corrected = Tick(ServerSays(sequence_, Vec3(0.5F, 0.0F, 0.0F)));
  ASSERT_GT(corrected.total_correction.x, 0.4F);

  world_.Start(Vec3(5.0F, 0.0F, 7.0F), Parameters{});
  const State restarted = Tick(std::nullopt);

  EXPECT_EQ(restarted.total_correction, corrected.total_correction);
}

TEST_F(ReconciliationTest, TheFirstFrameOfTheNextMatchHasNoOffset) {
  constexpr float kFrame = 1.0F / 60.0F;
  constexpr int kFadeFrames = 60;
  Correction smoothing;
  static_cast<void>(smoothing.Update(latest_.total_correction, kFrame));
  // A Match ends with a correction made and long since faded from view.
  const State corrected = Tick(ServerSays(sequence_, Vec3(0.5F, 0.0F, 0.0F)));
  ASSERT_GT(corrected.total_correction.x, 0.4F);
  for (int i = 0; i < kFadeFrames; ++i) {
    static_cast<void>(smoothing.Update(corrected.total_correction, kFrame));
  }

  world_.Start(Vec3(5.0F, 0.0F, 7.0F), Parameters{});
  const Vec3 offset = smoothing.Update(Tick(std::nullopt).total_correction, kFrame);

  EXPECT_NEAR(offset.x, 0.0F, 1e-6F);
  EXPECT_NEAR(offset.y, 0.0F, 1e-6F);
  EXPECT_NEAR(offset.z, 0.0F, 1e-6F);
}

TEST_F(ReconciliationTest, TheSameAcknowledgementRepeatedIsActedOnOnce) {
  const Acknowledgement ack = ServerSays(sequence_, Vec3(1.0F, 0.0F, 0.0F));
  const State first = Tick(ack, Walking());

  State last = first;
  constexpr int kTicksWalked = 10;
  for (int i = 0; i < kTicksWalked; ++i) {
    last = Tick(ack, Walking());
  }

  // Were it acted on every tick, the body would be put back at the server's state each time and never get anywhere.
  EXPECT_NEAR(last.local_body.position.x - first.local_body.position.x, kTicksWalked * kWalkStep, 0.1F);
}

TEST_F(ReconciliationTest, AnAcknowledgementForACommandNeverSentChangesNothing) {
  const float before = states_.back().position.x;

  const State state = Tick(ServerSays(sequence_ + 1000, Vec3(1.0F, 0.0F, 0.0F)));

  EXPECT_NEAR(state.local_body.position.x, before, 0.01F);
  EXPECT_EQ(state.total_correction, Vec3(0.0F, 0.0F, 0.0F));
}

TEST_F(ReconciliationTest, TheServerComparedAtTheCommandItAnswersNotAtTheClientsNewestState) {
  // The client walks; the server, running behind by 5 commands, reports the
  // state the client itself predicted at that command. Comparing it with the
  // newest state instead would pull the client back 5 ticks of walking.
  for (int i = 0; i < 30; ++i) {
    Tick(ServerAgreesWithTheClientExcept(sequence_ - 4, Vec3(0.0F, 0.0F, 0.0F)), Walking());
  }

  // 30 ticks of walking at 3 m/s.
  EXPECT_NEAR(states_.back().position.x - rest_.position.x, 30 * kWalkStep, 0.05F);
}

TEST_F(ReconciliationTest, ADivergenceIsCarriedThroughTheCommandsSentSince) {
  for (int i = 0; i < 10; ++i) {
    Tick(std::nullopt, Walking());
  }
  const BodyState before = states_.back();

  // The server has the player 1 m to the side, as of 2 commands ago.
  const State state = Tick(ServerAgreesWithTheClientExcept(sequence_ - 2, Vec3(0.0F, 0.0F, 1.0F)), Walking());

  EXPECT_NEAR(state.local_body.position.z, before.position.z + 1.0F, 0.02F);
  EXPECT_NEAR(state.local_body.position.x, before.position.x + kWalkStep, 0.02F);
}

TEST_F(ReconciliationTest, TheCommandsSentSinceAreSteppedAgainFromTheServersState) {
  // The client sprints, at full stamina. The server, 5 commands behind, says the
  // player was out of stamina and so could only walk: the commands sent since
  // are stepped again with that, not just moved by the position error (which is none).
  Command sprint = Walking();
  sprint.movement.sprint = true;
  for (int i = 0; i < 10; ++i) {
    Tick(std::nullopt, sprint);
  }
  const float before = states_.back().position.x;
  Acknowledgement ack = ServerAgreesWithTheClientExcept(sequence_ - 5, Vec3(0.0F, 0.0F, 0.0F));
  ack.body.stamina = 0.0F;

  const State state = Tick(ack, sprint);

  // 5 commands stepped at walking instead of sprinting speed, then this tick's, also walking:
  // 6 * 0.05 m from the server's state, against the 5 * 0.08 m the client had gone.
  EXPECT_LT(state.local_body.position.x, before);
  EXPECT_NEAR(state.local_body.stamina, 0.0F, 0.01F);
}

// A rifle of 600 rounds a minute, a round every six ticks, with a magazine of
// 15 that takes half a second, 30 ticks, to reload.
constexpr std::uint8_t kMagazine = 15;
constexpr int kTicksPerRound = 6;
constexpr int kReloadTicks = 30;

Parameters Armed() {
  Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.magazine_capacity = kMagazine;
  parameters.rifle.reload_seconds = 0.5F;
  return parameters;
}

Command Firing() {
  Command fire{};
  fire.fire = true;
  return fire;
}

Command Reloading() {
  Command reload{};
  reload.reload = true;
  return reload;
}

// A player started in a match with the rifle of Armed, ticked with sequences 1, 2, ...
class WeaponPredictionTest : public ::testing::Test {
 protected:
  WeaponPredictionTest() { world_.Start(Vec3{}, Armed()); }

  State Tick(const Command& command, const std::optional<Acknowledgement>& acknowledgement = std::nullopt) {
    ++sequence_;
    latest_ = world_.Tick(command, sequence_, acknowledgement, kFixedTick);
    states_.push_back(latest_);
    return latest_;
  }

  State Run(int ticks, const Command& command) {
    for (int i = 0; i < ticks; ++i) {
      Tick(command);
    }
    return latest_;
  }

  // What the client itself predicted after command number sequence.
  [[nodiscard]] Acknowledgement ServerAgreesWithTheClient(std::uint32_t sequence) const {
    const State& state = states_[sequence - 1];
    return Acknowledgement{.sequence = sequence, .body = state.local_body, .rifle = state.rifle};
  }

  World world_;
  std::uint32_t sequence_ = 0;
  std::vector<State> states_;
  State latest_;
};

TEST_F(WeaponPredictionTest, AMatchStartsWithAFullMagazineOfTheServersRifle) {
  const State state = Tick(Command{});

  EXPECT_EQ(state.rifle.rounds, kMagazine);
  EXPECT_EQ(state.rounds_fired, 0);
}

TEST(PredictionWorldTest, BeforeAMatchStartsNothingFires) {
  World world;

  const State state = world.Tick(Firing(), 0, std::nullopt, kFixedTick);

  EXPECT_EQ(state.rounds_fired, 0);
  EXPECT_EQ(state.rifle.rounds, 0);
}

TEST_F(WeaponPredictionTest, ATickThatFiresTakesARoundOffTheMagazineAndSaysSo) {
  const State fired = Tick(Firing());
  EXPECT_EQ(fired.rounds_fired, 1);
  EXPECT_EQ(fired.rifle.rounds, kMagazine - 1);

  // The rifle is not ready again on the next tick, fire held or not.
  const State waiting = Tick(Firing());
  EXPECT_EQ(waiting.rounds_fired, 0);
  EXPECT_EQ(waiting.rifle.rounds, kMagazine - 1);
}

TEST_F(WeaponPredictionTest, HoldingFireFiresAtTheRiflesRate) {
  int fired = 0;
  for (int i = 0; i < 10 * kTicksPerRound; ++i) {
    fired += Tick(Firing()).rounds_fired;
  }

  EXPECT_EQ(fired, 10);
  EXPECT_EQ(latest_.rifle.rounds, kMagazine - 10);
}

// A frame that sees only some ticks still learns of every round fired between
// two it saw, which is what draws each muzzle flash.
TEST_F(WeaponPredictionTest, TheRunningTotalOfRoundsFiredCountsEveryRoundAndOutlivesStartingOver) {
  Run(10 * kTicksPerRound, Firing());
  EXPECT_EQ(latest_.total_rounds_fired, 10U);

  world_.Start(Vec3{}, Armed());

  EXPECT_EQ(Tick(Command{}).total_rounds_fired, 10U);
  EXPECT_EQ(Tick(Firing()).total_rounds_fired, 11U);
}

TEST_F(WeaponPredictionTest, AReloadFillsTheMagazineOnceItsTimeHasPassed) {
  Run(3 * kTicksPerRound, Firing());
  ASSERT_EQ(latest_.rifle.rounds, kMagazine - 3);

  Tick(Reloading());
  EXPECT_EQ(Run(kReloadTicks - 2, Command{}).rifle.rounds, kMagazine - 3);
  EXPECT_EQ(Tick(Command{}).rifle.rounds, kMagazine);
}

TEST_F(WeaponPredictionTest, StartingOverLoadsTheRifleAgain) {
  Run(3 * kTicksPerRound, Firing());
  ASSERT_LT(latest_.rifle.rounds, kMagazine);

  world_.Start(Vec3{}, Armed());

  EXPECT_EQ(Tick(Command{}).rifle.rounds, kMagazine);
}

TEST_F(WeaponPredictionTest, ARifleTheServerAgreesWithIsNeverCorrected) {
  // The server answers each command three ticks after it was sent.
  for (int i = 0; i < 20 * kTicksPerRound; ++i) {
    const Command command = i == 8 * kTicksPerRound ? Reloading() : Firing();
    Tick(command, sequence_ > 3 ? std::optional(ServerAgreesWithTheClient(sequence_ - 3)) : std::nullopt);
  }

  EXPECT_EQ(latest_.rifle_corrections, 0U);
}

// A rifle with a full magazine, ready to fire.
constexpr augusta::weapon::State kFullRifle{.cooldown = 0.0F, .reload_remaining = 0.0F, .rounds = kMagazine};

TEST_F(WeaponPredictionTest, AReplayAddsNoRoundToTheRunningTotal) {
  Run(kTicksPerRound, Firing());
  Acknowledgement refused = ServerAgreesWithTheClient(1);
  refused.rifle = kFullRifle;

  // The replay fires the round the server refused again, but no new one is
  // fired: nothing more is drawn.
  EXPECT_EQ(Tick(Firing(), refused).total_rounds_fired, 1U);
}

// The server refused the round the client fired on its first command: it says
// the rifle was still full and ready after it.
TEST_F(WeaponPredictionTest, ARifleTheServerDisagreesWithIsPutAtTheServersAndTheCommandsSinceReplayedFromIt) {
  Run(kTicksPerRound, Firing());
  ASSERT_EQ(latest_.rifle.rounds, kMagazine - 1);
  Acknowledgement refused = ServerAgreesWithTheClient(1);
  refused.rifle = kFullRifle;

  // Left alone, the client would fire its second round on this tick. Replayed
  // from the server's rifle, the second command fires the first round instead,
  // and this tick finds the rifle a tick short of ready.
  const State corrected = Tick(Firing(), refused);

  EXPECT_EQ(corrected.rifle_corrections, 1U);
  EXPECT_EQ(corrected.rifle.rounds, kMagazine - 1);
  EXPECT_EQ(corrected.rounds_fired, 0);
  EXPECT_EQ(Tick(Firing()).rounds_fired, 1);
}

TEST_F(WeaponPredictionTest, ACorrectedRifleIsWhatTheNextAcknowledgementIsComparedWith) {
  Run(kTicksPerRound, Firing());
  Acknowledgement refused = ServerAgreesWithTheClient(1);
  refused.rifle = kFullRifle;
  Tick(Firing(), refused);

  // The server, its rifle full after the first command, fired on the second:
  // what the replay predicted too.
  Acknowledgement second = ServerAgreesWithTheClient(2);
  second.rifle = augusta::weapon::Step(Armed().rifle, refused.rifle, Firing(), kFixedTick).state;
  const State state = Tick(Firing(), second);

  EXPECT_EQ(state.rifle_corrections, 1U);
}

TEST_F(WeaponPredictionTest, ABodyTheServerDisagreesWithIsCorrectedWithoutCountingARifleCorrection) {
  Run(kTicksPerRound, Firing());
  Acknowledgement moved = ServerAgreesWithTheClient(sequence_);
  moved.body.position.x += 0.5F;

  const State state = Tick(Firing(), moved);

  EXPECT_NEAR(state.total_correction.x, 0.5F, 0.02F);
  EXPECT_EQ(state.rifle_corrections, 0U);
  EXPECT_EQ(state.rifle.rounds, kMagazine - 2);
}

}  // namespace
