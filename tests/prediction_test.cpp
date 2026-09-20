#include "augusta/prediction.h"

#include <cmath>
#include <optional>

#include <gtest/gtest.h>

#include "augusta/math.h"

// M1 spike (ADR-0002/ADR-0004, issue #32): exercises PredictionWorld's
// public Tick() surface end-to-end - the "one entity under prediction"
// the spike asks for - rather than physics::World directly (see
// physics_test.cpp for that).
namespace {

using augusta::input::Command;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::StaminaConfig;
using augusta::prediction::State;
using augusta::prediction::World;

constexpr float kFixedTick = 1.0F / 60.0F;

TEST(PredictionWorldTest, TicksTheLocalEntityForwardEachCall) {
  World world{StaminaConfig{}};
  Command command{};
  command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);

  State state;
  for (int i = 0; i < 30; ++i) {
    state = world.Tick(command, std::nullopt, kFixedTick);
  }

  EXPECT_GT(state.local_body.position.x, 0.0F);
}

TEST(PredictionWorldTest, ReconcilesTowardAuthoritativeStateWithoutOvershoot) {
  World world{StaminaConfig{}};
  const Command command{};  // No movement input - isolates the x axis from gravity's y drift.

  BodyState authoritative{};
  authoritative.position = Vec3(1.0F, 0.0F, 0.0F);

  float previous_error = 1.0F;
  bool converged = false;
  for (int i = 0; i < 50; ++i) {
    const State state = world.Tick(command, authoritative, kFixedTick);
    const float error = std::abs(authoritative.position.x - state.local_body.position.x);
    EXPECT_LE(error, previous_error + 1e-4F) << "tick " << i;
    previous_error = error;
    if (error < 1e-3F) {
      converged = true;
      break;
    }
  }
  EXPECT_TRUE(converged);
}

}  // namespace
