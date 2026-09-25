#include "augusta/prediction.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/parameters.h"

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

TEST_F(ReconciliationTest, TheJumpTheReplayMadeIsTheTotalCorrection) {
  constexpr float kDivergence = 0.5F;
  EXPECT_EQ(latest_.total_correction, Vec3(0.0F, 0.0F, 0.0F));

  const State state = Tick(ServerSays(sequence_ - 2, Vec3(kDivergence, 0.0F, 0.0F)));

  EXPECT_NEAR(state.total_correction.x, kDivergence, 0.02F);
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

}  // namespace
