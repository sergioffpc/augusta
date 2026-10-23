#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/tick.h"

// PredictionWorld's Collision lines (Development build only, ADR-0074): the
// client World's visualization, stepped once per tick and carried in the
// Prediction State for the Collision Debug overlay.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::BodyState;
using augusta::physics::CollisionMesh;
using augusta::physics::Visualization;
using augusta::prediction::Acknowledgement;
using augusta::prediction::State;
using augusta::prediction::World;

constexpr float kFixedTick = 1.0F / 60.0F;

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

Visualization Collision() {
  Visualization visualization;
  visualization.on = true;
  visualization.cull_half_extent = 20.0F;
  return visualization;
}

TEST(PredictionCollisionTest, WithCollisionOnTheStateCarriesLinesAfterATick) {
  World world;
  ASSERT_TRUE(world.AddCollisionMesh(Floor()).has_value());
  world.Start(Vec3(0.0F, 0.0F, 0.0F), Parameters{});
  world.SetVisualization(Collision());

  const State state = world.Tick(Walking(), 1, std::nullopt, kFixedTick);

  ASSERT_NE(state.collision_lines, nullptr);
  EXPECT_FALSE(state.collision_lines->empty());
}

TEST(PredictionCollisionTest, WithCollisionOffTheStateCarriesNoLines) {
  World world;
  ASSERT_TRUE(world.AddCollisionMesh(Floor()).has_value());
  world.Start(Vec3(0.0F, 0.0F, 0.0F), Parameters{});

  const State state = world.Tick(Walking(), 1, std::nullopt, kFixedTick);

  EXPECT_EQ(state.collision_lines, nullptr);
}

TEST(PredictionCollisionTest, TurnedOffAgainTheStateCarriesNoLines) {
  World world;
  ASSERT_TRUE(world.AddCollisionMesh(Floor()).has_value());
  world.Start(Vec3(0.0F, 0.0F, 0.0F), Parameters{});
  world.SetVisualization(Collision());
  ASSERT_NE(world.Tick(Walking(), 1, std::nullopt, kFixedTick).collision_lines, nullptr);

  world.SetVisualization(Visualization{});
  const State state = world.Tick(Walking(), 2, std::nullopt, kFixedTick);

  EXPECT_EQ(state.collision_lines, nullptr);
}

// The bodies a client predicts walking, the server answering each command 3
// ticks later with the player half a metre to the side of where the client
// had it, so every answer is a Reconciliation replay; with Collision on when
// collision is set.
std::vector<BodyState> PredictedWithReplays(bool collision) {
  World world;
  EXPECT_TRUE(world.AddCollisionMesh(Floor()).has_value());
  world.Start(Vec3(0.0F, 0.0F, 0.0F), Parameters{});
  if (collision) {
    world.SetVisualization(Collision());
  }
  std::vector<BodyState> bodies;
  for (augusta::tick::Sequence sequence = 1; sequence <= 60; ++sequence) {
    std::optional<Acknowledgement> acknowledgement;
    if (sequence > 3) {
      BodyState server = bodies[sequence - 4];
      server.position.z += 0.5F;
      acknowledgement = Acknowledgement{.sequence = sequence - 3, .body = server, .rifle = {}};
    }
    bodies.push_back(world.Tick(Walking(), sequence, acknowledgement, kFixedTick).local_body);
  }
  return bodies;
}

TEST(PredictionCollisionTest, AReplayWithCollisionOnPredictsTheSameBodyAsWithItOff) {
  const std::vector<BodyState> off = PredictedWithReplays(false);
  const std::vector<BodyState> on = PredictedWithReplays(true);

  EXPECT_EQ(on, off);
  // The server's answers did move the body.
  EXPECT_GT(off.back().position.z, 1.0F);
}

}  // namespace
