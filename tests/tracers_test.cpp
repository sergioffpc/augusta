#include "augusta/tracers.h"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/effects.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// Against a real Map (physics::World) and the real ballistics module: what a
// tracer draws is checked against the trajectory the server computes for the
// same Shot.
namespace {

using augusta::ballistics::Outcome;
using augusta::ballistics::StepResult;
using augusta::math::Vec3;
using augusta::presentation::Effect;
using augusta::presentation::kImpactSeconds;
using augusta::presentation::Tracer;
using augusta::presentation::TracerRules;
using augusta::presentation::Tracers;

constexpr float kTick = 1.0F / 60.0F;
constexpr float kTolerance = 1e-3F;
const Vec3 kEye(0.0F, 1.7F, 0.0F);
// A Shot aimed a little up and to the left, so its path both climbs and drops.
const Vec3 kAim = augusta::command::ViewDirection(0.2F, 0.05F);
constexpr TracerRules kRules{
    .muzzle_velocity = 300.0F, .bullet = {.gravity = 9.81F, .max_range = 1000.0F}, .tick_duration = kTick};

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// A wall facing the origin across the whole view, depth meters away down -Z.
augusta::physics::CollisionMesh WallAt(float depth) {
  constexpr float kHalfSize = 500.0F;
  return {.points = {Vec3(-kHalfSize, -kHalfSize, -depth), Vec3(kHalfSize, -kHalfSize, -depth),
                     Vec3(kHalfSize, kHalfSize, -depth), Vec3(-kHalfSize, kHalfSize, -depth)},
          .indices = {0, 1, 2, 0, 2, 3}};
}

// The server's view of the same Shot: every tick's result until it resolves.
std::vector<StepResult> Trajectory(const augusta::physics::World& map) {
  augusta::ballistics::World reference;
  const auto bullet = reference.Fire(kEye, kAim, kRules.muzzle_velocity, kRules.bullet);
  std::vector<StepResult> steps;
  do {
    steps.push_back(reference.Step(bullet, kTick, map, {}));
  } while (steps.back().outcome == Outcome::kInFlight);
  return steps;
}

class TracersTest : public ::testing::Test {
 protected:
  augusta::physics::World map_{augusta::physics::StaminaConfig{}};
};

TEST_F(TracersTest, ATracerIsDrawnAlongTheShotsTrajectoryTickByTick) {
  ASSERT_TRUE(map_.AddCollisionMesh(WallAt(60.0F)).has_value());
  const std::vector<StepResult> trajectory = Trajectory(map_);
  ASSERT_GT(trajectory.size(), 3U);
  Tracers tracers(map_);
  tracers.Fire(kEye, kAim, kRules);

  Vec3 behind = kEye;
  for (std::size_t tick = 0; tick + 1 < trajectory.size(); ++tick) {
    tracers.Advance(kTick);
    const std::vector<Tracer> drawn = tracers.Drawn();
    ASSERT_EQ(drawn.size(), 1U) << tick;
    ExpectNear(drawn.front().head, trajectory[tick].state.position);
    ExpectNear(drawn.front().tail, behind);
    behind = trajectory[tick].state.position;
  }
}

TEST_F(TracersTest, BetweenTwoTicksATracerIsPartWayAlongTheTicksPath) {
  const std::vector<StepResult> trajectory = Trajectory(map_);
  Tracers tracers(map_);
  tracers.Fire(kEye, kAim, kRules);

  tracers.Advance(kTick);
  tracers.Advance(kTick / 4.0F);

  ExpectNear(tracers.Drawn().front().head,
             augusta::math::Lerp(trajectory[0].state.position, trajectory[1].state.position, 0.25F));
}

TEST_F(TracersTest, ATracerThatMeetsTheMapEndsInAnImpactWhereItStruck) {
  ASSERT_TRUE(map_.AddCollisionMesh(WallAt(20.0F)).has_value());
  const std::vector<StepResult> trajectory = Trajectory(map_);
  ASSERT_EQ(trajectory.back().outcome, Outcome::kHitMap);
  Tracers tracers(map_);
  tracers.Fire(kEye, kAim, kRules);

  for (std::size_t tick = 0; tick < trajectory.size(); ++tick) {
    EXPECT_TRUE(tracers.Impacts().empty()) << tick;
    tracers.Advance(kTick);
  }

  EXPECT_TRUE(tracers.Drawn().empty());
  ASSERT_EQ(tracers.Impacts().size(), 1U);
  ExpectNear(tracers.Impacts().front().position, trajectory.back().impact_point);
}

TEST_F(TracersTest, AnImpactFadesOnceItsTimeIsUp) {
  ASSERT_TRUE(map_.AddCollisionMesh(WallAt(20.0F)).has_value());
  Tracers tracers(map_);
  tracers.Fire(kEye, kAim, kRules);
  while (tracers.Impacts().empty()) {
    tracers.Advance(kTick);
  }

  tracers.Advance(kImpactSeconds);

  EXPECT_TRUE(tracers.Impacts().empty());
}

// A tracer is a visual only: it is handed no player to hit (ADR-0044), so only
// the Map ends one early, and one that meets nothing flies to its max range
// and leaves no impact.
TEST_F(TracersTest, ATracerThatMeetsNothingEndsAtItsMaxRangeWithNoImpact) {
  constexpr TracerRules kShortRange{
      .muzzle_velocity = 300.0F, .bullet = {.gravity = 0.0F, .max_range = 50.0F}, .tick_duration = kTick};
  Tracers tracers(map_);
  tracers.Fire(kEye, kAim, kShortRange);

  tracers.Advance(1.0F);

  EXPECT_TRUE(tracers.Drawn().empty());
  EXPECT_TRUE(tracers.Impacts().empty());
}

TEST_F(TracersTest, EveryShotHasATracerOfItsOwn) {
  Tracers tracers(map_);
  tracers.Fire(kEye, kAim, kRules);
  tracers.Advance(kTick);
  tracers.Fire(kEye + Vec3(1.0F, 0.0F, 0.0F), kAim, kRules);

  tracers.Advance(kTick);

  EXPECT_EQ(tracers.Drawn().size(), 2U);
}

}  // namespace
