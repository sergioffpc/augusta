#include "augusta/simulation.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

// SimulationWorld on flat ground: players are entities with bodies, moved by
// the commands of each tick.
namespace {

using augusta::input::Command;
using augusta::math::Vec3;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::simulation::EntityId;
using augusta::simulation::PlayerCommand;
using augusta::simulation::State;
using augusta::simulation::World;

constexpr float kTick = 1.0F / 60.0F;
constexpr int kSettleTicks = 30;
constexpr int kWalkTicks = 60;

constexpr EntityId kAlice = static_cast<EntityId>(1);
constexpr EntityId kBob = static_cast<EntityId>(2);

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

Command Walking(Vec3 direction, Stance stance = Stance::kStanding) {
  Command command;
  command.movement.direction = direction;
  command.movement.desired_stance = stance;
  return command;
}

class SimulationTest : public ::testing::Test {
 protected:
  SimulationTest() : world_(augusta::physics::StaminaConfig{}, "scripts/round.lua") {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
  }

  // Ticks n times with the same commands and returns the last state.
  State Run(int ticks, const std::vector<PlayerCommand>& commands) {
    State state;
    for (int i = 0; i < ticks; ++i) {
      state = world_.Tick(commands, kTick);
    }
    return state;
  }

  static const augusta::physics::BodyState& Body(const State& state, EntityId entity) {
    for (const auto& entry : state.bodies) {
      if (entry.entity == entity) {
        return entry.body;
      }
    }
    ADD_FAILURE() << "player not in state";
    return state.bodies.front().body;
  }

  static float HorizontalSpeed(const augusta::physics::BodyState& body) {
    return std::hypot(body.velocity.x, body.velocity.z);
  }

  World world_;
};

TEST_F(SimulationTest, AWallStopsAWalkingPlayer) {
  constexpr float kWallX = 3.0F;
  constexpr float kHalfWidth = 20.0F;
  constexpr float kHeight = 5.0F;
  constexpr float kBottom = 0.0F;
  ASSERT_TRUE(world_
                  .AddCollisionMesh(
                      CollisionMesh{.points = {Vec3(kWallX, kBottom, -kHalfWidth), Vec3(kWallX, kHeight, -kHalfWidth),
                                               Vec3(kWallX, kHeight, kHalfWidth), Vec3(kWallX, kBottom, kHalfWidth)},
                                    .indices = {0, 1, 2, 0, 2, 3}})
                  .has_value());
  // Dropped a little above the floor: a body placed exactly on it starts overlapping it.
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F));
  Run(kSettleTicks, {});

  const State state = Run(120, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_LT(Body(state, kAlice).position.x, kWallX);
  EXPECT_GT(Body(state, kAlice).position.x, kWallX - 1.0F);
}

TEST_F(SimulationTest, AnEmptyWorldHasAnEmptyState) { EXPECT_TRUE(world_.Tick({}, kTick).bodies.empty()); }

TEST_F(SimulationTest, AddedPlayersAppearInTheStateOrderedById) {
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F));
  world_.AddPlayer(kAlice, Vec3(-5.0F, 0.0F, 0.0F));

  const State state = world_.Tick({}, kTick);

  ASSERT_EQ(state.bodies.size(), 2U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
  EXPECT_EQ(state.bodies[1].entity, kBob);
}

TEST_F(SimulationTest, APlayerStartsStandingWhereItSpawned) {
  world_.AddPlayer(kAlice, Vec3(3.0F, 0.0F, -2.0F));
  Run(kSettleTicks, {});

  const State state = world_.Tick({}, kTick);

  EXPECT_NEAR(Body(state, kAlice).position.x, 3.0F, 0.05F);
  EXPECT_NEAR(Body(state, kAlice).position.z, -2.0F, 0.05F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kStanding);
}

TEST_F(SimulationTest, AForwardCommandMovesThatPlayerOnly) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));
  world_.AddPlayer(kBob, Vec3(0.0F, 0.0F, 10.0F));
  Run(kSettleTicks, {});
  const float start = Body(world_.Tick({}, kTick), kAlice).position.x;
  const float bob_start = Body(world_.Tick({}, kTick), kBob).position.x;

  const State state = Run(kWalkTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_GT(Body(state, kAlice).position.x, start + 2.0F);
  EXPECT_NEAR(Body(state, kBob).position.x, bob_start, 0.01F);
}

TEST_F(SimulationTest, StanceCommandsChangeTheStanceAndTheSpeed) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));
  Run(kSettleTicks, {});
  const auto walk_at = [&](Stance stance) {
    const State state =
        Run(kWalkTicks / 4, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(0.0F, 0.0F, 1.0F), stance)}});
    EXPECT_EQ(Body(state, kAlice).stance, stance);
    return HorizontalSpeed(Body(state, kAlice));
  };

  const float standing = walk_at(Stance::kStanding);
  const float crouching = walk_at(Stance::kCrouching);
  const float prone = walk_at(Stance::kProne);

  EXPECT_GT(standing, crouching + 0.3F);
  EXPECT_GT(crouching, prone + 0.3F);
  EXPECT_GT(prone, 0.1F);
}

TEST_F(SimulationTest, APlayerWithNoCommandStopsAndKeepsItsStance) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));
  Run(kSettleTicks, {});
  Run(kWalkTicks / 2,
      {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F), Stance::kCrouching)}});

  const State state = Run(kWalkTicks / 2, {});

  EXPECT_NEAR(HorizontalSpeed(Body(state, kAlice)), 0.0F, 0.01F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kCrouching);
}

TEST_F(SimulationTest, ACommandForAPlayerNotInTheWorldIsIgnored) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));

  const State state = Run(kSettleTicks, {PlayerCommand{.entity = kBob, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
}

TEST_F(SimulationTest, ARemovedPlayerLeavesTheState) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F));
  Run(kSettleTicks, {});

  world_.RemovePlayer(kAlice);
  const State state = world_.Tick({}, kTick);

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kBob);
}

TEST_F(SimulationTest, RemovingAPlayerNotInTheWorldChangesNothing) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));

  world_.RemovePlayer(kBob);

  EXPECT_EQ(world_.Tick({}, kTick).bodies.size(), 1U);
}

TEST_F(SimulationTest, AddingAPlayerTwiceKeepsOne) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F));
  world_.AddPlayer(kAlice, Vec3(9.0F, 0.0F, 0.0F));

  EXPECT_EQ(world_.Tick({}, kTick).bodies.size(), 1U);
}

}  // namespace
