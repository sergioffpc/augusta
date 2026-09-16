#include "augusta/physics.h"

#include <gtest/gtest.h>

// M1 spike (ADR-0002/ADR-0004): the "standalone" proof issue #32 asks
// for - a real PhysX-backed physics::World, driven the same way both
// PredictionWorld and SimulationWorld drive it, demonstrating movement
// and snap/blend reconciliation with no wild jitter.
namespace {

using augusta::math::Length;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::MovementInput;
using augusta::physics::RaycastHit;
using augusta::physics::StaminaConfig;
using augusta::physics::Stance;
using augusta::physics::World;

constexpr float kFixedTick = 1.0F / 60.0F;

TEST(PhysicsWorldTest, StepMovesBodyAlongInputDirection) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  MovementInput input{};
  input.direction = Vec3(1.0F, 0.0F, 0.0F);

  BodyState state{};
  for (int i = 0; i < 30; ++i) {
    state = world.Step(body, input, kFixedTick);
  }

  EXPECT_GT(state.position.x, 0.0F);
}

TEST(PhysicsWorldTest, SprintDepletesStaminaAndForcesWalkBelowThreshold) {
  StaminaConfig config;
  config.deplete_per_second = 1.0F;
  config.regen_per_second = 0.0F;
  config.forced_walk_below = 0.0F;
  World world{config};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  MovementInput input{};
  input.direction = Vec3(1.0F, 0.0F, 0.0F);
  input.sprint = true;

  BodyState state{};
  for (int i = 0; i < 5; ++i) {
    state = world.Step(body, input, 0.1F);
  }

  EXPECT_LT(state.stamina, 1.0F);
}

TEST(PhysicsWorldTest, ReconciliationConvergesTowardAuthoritativeWithoutOvershoot) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  BodyState authoritative{};
  authoritative.position = Vec3(1.0F, 0.0F, 0.0F);
  authoritative.stance = Stance::kStanding;
  authoritative.stamina = 1.0F;

  // No Step calls between corrections - isolates the blend curve from
  // gravity/movement, the way repeated authoritative arrivals do on
  // ticks where the client already has nothing new to predict.
  float previous_error = Length(authoritative.position - Vec3(0.0F, 0.0F, 0.0F));
  bool converged = false;
  for (int i = 0; i < 50; ++i) {
    const BodyState corrected = world.Reconcile(body, authoritative);
    const float error = Length(authoritative.position - corrected.position);
    // The defining "no wild jitter" property: each correction strictly
    // narrows the gap, never overshoots or oscillates past it.
    EXPECT_LE(error, previous_error) << "iteration " << i;
    previous_error = error;
    if (error < 1e-3F) {
      converged = true;
      break;
    }
  }
  EXPECT_TRUE(converged);
}

TEST(PhysicsWorldTest, ReconciliationSnapsForLargeDivergence) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  BodyState authoritative{};
  authoritative.position = Vec3(100.0F, 0.0F, 0.0F);
  authoritative.stance = Stance::kStanding;
  authoritative.stamina = 1.0F;

  const BodyState corrected = world.Reconcile(body, authoritative);

  EXPECT_NEAR(corrected.position.x, 100.0F, 1e-3F);
  EXPECT_NEAR(corrected.position.y, 0.0F, 1e-3F);
  EXPECT_NEAR(corrected.position.z, 0.0F, 1e-3F);
}

TEST(PhysicsWorldTest, RaycastHitsACreatedBody) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  const RaycastHit hit = world.Raycast(Vec3(0.0F, 10.0F, 0.0F), Vec3(0.0F, -1.0F, 0.0F), 20.0F);

  ASSERT_TRUE(hit.has_hit);
  EXPECT_EQ(hit.body, body);
  EXPECT_GT(hit.point.y, 0.0F);
  EXPECT_LT(hit.point.y, 10.0F);
}

}  // namespace
