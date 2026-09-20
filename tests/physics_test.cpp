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
using augusta::physics::StaticMesh;
using augusta::physics::StaticMeshError;
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

TEST(PhysicsWorldTest, SeveralWorldsCoexistInOneProcess) {
  // A server and its clients run in one process in tests, each with its own
  // World; PhysX allows only one foundation per process, so they must share it.
  World first{StaminaConfig{}};
  World second{StaminaConfig{}};
  const auto first_body = first.CreateBody(Vec3(0.0F, 0.0F, 0.0F));
  const auto second_body = second.CreateBody(Vec3(10.0F, 0.0F, 0.0F));

  MovementInput input{};
  input.direction = Vec3(1.0F, 0.0F, 0.0F);
  BodyState first_state{};
  BodyState second_state{};
  for (int i = 0; i < 30; ++i) {
    first_state = first.Step(first_body, input, kFixedTick);
    second_state = second.Step(second_body, MovementInput{}, kFixedTick);
  }

  EXPECT_GT(first_state.position.x, 0.0F);
  EXPECT_NEAR(second_state.position.x, 10.0F, 0.01F);
}

TEST(PhysicsWorldTest, AWorldCanBeCreatedAfterAllOthersWereDestroyed) {
  {
    World first{StaminaConfig{}};
  }
  World second{StaminaConfig{}};

  const auto body = second.CreateBody(Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_NO_THROW(second.Step(body, MovementInput{}, kFixedTick));
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

// ---- Static map geometry (issue #77) ----

// A flat rectangle as two triangles, corners in counter-clockwise order seen
// from the side the normal points to.
StaticMesh Quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
  return StaticMesh{.points = {a, b, c, d}, .indices = {0, 1, 2, 0, 2, 3}};
}

// A horizontal slab from (x_min, z_min) to (x_max, z_max) at height y, normal up.
StaticMesh Floor(float y, float x_min, float x_max, float z_min, float z_max) {
  return Quad(Vec3(x_min, y, z_min), Vec3(x_min, y, z_max), Vec3(x_max, y, z_max), Vec3(x_max, y, z_min));
}

// The same slab seen from below, normal down.
StaticMesh Ceiling(float y, float x_min, float x_max, float z_min, float z_max) {
  return Quad(Vec3(x_min, y, z_min), Vec3(x_max, y, z_min), Vec3(x_max, y, z_max), Vec3(x_min, y, z_max));
}

// A vertical wall across the x axis at x, from y = 0 up to height, its triangles
// facing +x (away from a body approaching from -x) or -x.
StaticMesh Wall(float x, float height, float z_min, float z_max, bool faces_positive_x) {
  const Vec3 bottom_near(x, 0.0F, z_min);
  const Vec3 top_near(x, height, z_min);
  const Vec3 top_far(x, height, z_max);
  const Vec3 bottom_far(x, 0.0F, z_max);
  return faces_positive_x ? Quad(bottom_near, top_near, top_far, bottom_far)
                          : Quad(bottom_near, bottom_far, top_far, top_near);
}

constexpr float kHalfExtent = 50.0F;

World WorldWithFloor() {
  World world{StaminaConfig{}};
  EXPECT_TRUE(world.AddStaticMesh(Floor(0.0F, -kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
  return world;
}

BodyState Settle(World& world, augusta::physics::BodyHandle body, const MovementInput& input, int ticks) {
  BodyState state{};
  for (int i = 0; i < ticks; ++i) {
    state = world.Step(body, input, kFixedTick);
  }
  return state;
}

TEST(StaticGeometryTest, AFloorHoldsAWalkingBodyAtGroundHeight) {
  World world = WorldWithFloor();
  const auto body = world.CreateBody(Vec3(0.0F, 3.0F, 0.0F));

  const BodyState landed = Settle(world, body, MovementInput{}, 120);
  MovementInput walk{};
  walk.direction = Vec3(1.0F, 0.0F, 0.0F);
  const BodyState walking = Settle(world, body, walk, 60);

  EXPECT_NEAR(landed.position.y, 0.0F, 0.1F);
  EXPECT_NEAR(walking.position.y, 0.0F, 0.1F);
  EXPECT_GT(walking.position.x, landed.position.x);
}

TEST(StaticGeometryTest, WithoutAFloorABodyKeepsFalling) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 3.0F, 0.0F));

  const BodyState state = Settle(world, body, MovementInput{}, 120);

  EXPECT_LT(state.position.y, -3.0F);
}

class WallTest : public ::testing::TestWithParam<bool> {};

// A cooked map's triangles can face either way, so the wall must stop a body
// whichever side its triangles face.
TEST_P(WallTest, AWallStopsAWalkingBody) {
  World world = WorldWithFloor();
  ASSERT_TRUE(world.AddStaticMesh(Wall(5.0F, 5.0F, -kHalfExtent, kHalfExtent, GetParam())).has_value());
  const auto body = world.CreateBody(Vec3(0.0F, 1.2F, 0.0F));
  MovementInput walk{};
  walk.direction = Vec3(1.0F, 0.0F, 0.0F);

  // At 3 m/s, 300 ticks is 15 m: far enough to pass a wall 5 m ahead if it were not there.
  const BodyState state = Settle(world, body, walk, 300);

  EXPECT_LT(state.position.x, 5.0F);
  EXPECT_GT(state.position.x, 3.0F);
}

INSTANTIATE_TEST_SUITE_P(EitherFacing, WallTest, ::testing::Bool());

TEST(StaticGeometryTest, ACrouchedBodyCannotStandUpUnderALowCeiling) {
  World world = WorldWithFloor();
  // A tunnel from x = 3 onward: 1.7 m of headroom fits a crouch (1.3 m, plus the
  // controller's contact skin) but not standing (2.1 m).
  ASSERT_TRUE(world.AddStaticMesh(Ceiling(1.7F, 3.0F, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
  const auto body = world.CreateBody(Vec3(0.0F, 1.2F, 0.0F));
  MovementInput crouch_in{};
  crouch_in.direction = Vec3(1.0F, 0.0F, 0.0F);
  crouch_in.desired_stance = Stance::kCrouching;
  const BodyState in_tunnel = Settle(world, body, crouch_in, 120);
  ASSERT_GT(in_tunnel.position.x, 3.5F);
  ASSERT_EQ(in_tunnel.stance, Stance::kCrouching);

  MovementInput try_stand{};
  try_stand.desired_stance = Stance::kStanding;
  const BodyState after = Settle(world, body, try_stand, 10);

  EXPECT_EQ(after.stance, Stance::kCrouching);
}

TEST(StaticGeometryTest, AProneBodyCannotStandUpUnderALowCeiling) {
  World world = WorldWithFloor();
  ASSERT_TRUE(world.AddStaticMesh(Ceiling(1.7F, 3.0F, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
  const auto body = world.CreateBody(Vec3(0.0F, 1.2F, 0.0F));
  MovementInput crawl_in{};
  crawl_in.direction = Vec3(1.0F, 0.0F, 0.0F);
  crawl_in.desired_stance = Stance::kProne;
  const BodyState in_tunnel = Settle(world, body, crawl_in, 240);
  ASSERT_GT(in_tunnel.position.x, 3.5F);
  ASSERT_EQ(in_tunnel.stance, Stance::kProne);

  MovementInput try_stand{};
  try_stand.desired_stance = Stance::kStanding;

  EXPECT_NE(Settle(world, body, try_stand, 10).stance, Stance::kStanding);
}

TEST(StaticGeometryTest, ACeilingJustAboveStandingHeightStillBlocksStandingUp) {
  World world = WorldWithFloor();
  // Standing is 2.1 m tall; the controller also keeps a contact skin around it.
  ASSERT_TRUE(world.AddStaticMesh(Ceiling(2.05F, -kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
  const auto body = world.CreateBody(Vec3(0.0F, 1.0F, 0.0F));
  MovementInput crouch{};
  crouch.desired_stance = Stance::kCrouching;
  ASSERT_EQ(Settle(world, body, crouch, 30).stance, Stance::kCrouching);

  MovementInput stand{};
  stand.desired_stance = Stance::kStanding;

  EXPECT_EQ(Settle(world, body, stand, 30).stance, Stance::kCrouching);
}

TEST(StaticGeometryTest, ABodyCanStandUpWhereThereIsHeadroom) {
  World world = WorldWithFloor();
  const auto body = world.CreateBody(Vec3(0.0F, 1.2F, 0.0F));
  MovementInput crouch{};
  crouch.desired_stance = Stance::kCrouching;
  ASSERT_EQ(Settle(world, body, crouch, 30).stance, Stance::kCrouching);

  MovementInput stand{};
  stand.desired_stance = Stance::kStanding;

  EXPECT_EQ(Settle(world, body, stand, 30).stance, Stance::kStanding);
}

TEST(StaticGeometryTest, ValidateStaticMeshAgreesWithAddStaticMesh) {
  EXPECT_EQ(augusta::physics::ValidateStaticMesh(StaticMesh{}).error(), StaticMeshError::kEmpty);
  EXPECT_TRUE(augusta::physics::ValidateStaticMesh(Floor(0.0F, -1.0F, 1.0F, -1.0F, 1.0F)).has_value());
}

TEST(StaticGeometryTest, AMeshWithNoTrianglesIsRejected) {
  World world{StaminaConfig{}};

  const auto result = world.AddStaticMesh(StaticMesh{});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), StaticMeshError::kEmpty);
}

TEST(StaticGeometryTest, AnIndexOutsideThePointsIsRejected) {
  World world{StaminaConfig{}};

  const auto result = world.AddStaticMesh(StaticMesh{
      .points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F)}, .indices = {0, 1, 7}});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), StaticMeshError::kInvalidIndex);
}

TEST(StaticGeometryTest, AnIndexCountThatIsNotWholeTrianglesIsRejected) {
  World world{StaminaConfig{}};

  const auto result = world.AddStaticMesh(StaticMesh{
      .points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F)}, .indices = {0, 1}});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), StaticMeshError::kInvalidIndex);
}

}  // namespace
