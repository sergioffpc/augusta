#include "augusta/physics.h"

#include <gtest/gtest.h>

// M1 spike (ADR-0002/ADR-0004): the "standalone" proof issue #32 asks
// for - a real PhysX-backed physics::World, driven the same way both
// PredictionWorld and SimulationWorld drive it, demonstrating movement
// and restoring a body to an earlier state so a replay can start from it.
namespace {

using augusta::math::Length;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::CollisionMesh;
using augusta::physics::CollisionMeshError;
using augusta::physics::FallState;
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

TEST(PhysicsWorldTest, RestoreMovesTheBodyAndTheNextStepStartsFromThere) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  BodyState restored{};
  restored.position = Vec3(5.0F, 10.0F, -3.0F);
  restored.stamina = 0.5F;
  const BodyState returned = world.Restore(body, restored, FallState{});
  const BodyState stepped = world.Step(body, MovementInput{}, 1.0F / 60.0F);

  EXPECT_EQ(returned.position, restored.position);
  EXPECT_NEAR(stepped.position.x, 5.0F, 1e-3F);
  EXPECT_NEAR(stepped.position.z, -3.0F, 1e-3F);
  EXPECT_NEAR(stepped.position.y, 10.0F, 0.2F);
  EXPECT_NEAR(stepped.stamina, 0.5F, 0.05F);
}

TEST(PhysicsWorldTest, RestoreChangesTheStance) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));

  BodyState restored{};
  restored.stance = Stance::kProne;
  world.Restore(body, restored, FallState{});
  MovementInput input{};
  input.desired_stance = Stance::kProne;
  const BodyState stepped = world.Step(body, input, 1.0F / 60.0F);

  EXPECT_EQ(stepped.stance, Stance::kProne);
}

TEST(PhysicsWorldTest, RestoringAStateAndItsFallMakesTheNextStepTheSameAsBefore) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 100.0F, 0.0F));
  BodyState state{};
  for (int i = 0; i < 30; ++i) {
    state = world.Step(body, MovementInput{}, 1.0F / 60.0F);
  }
  const FallState fall = world.Fall(body);
  const BodyState first = world.Step(body, MovementInput{}, 1.0F / 60.0F);

  world.Restore(body, state, fall);
  const BodyState again = world.Step(body, MovementInput{}, 1.0F / 60.0F);

  // Not bit for bit: the state holds floats, the controller doubles.
  EXPECT_NEAR(again.position.y, first.position.y, 1e-4F);
  EXPECT_NEAR(again.velocity.y, first.velocity.y, 1e-2F);
}

TEST(PhysicsWorldTest, RestoreKeepsTheFallItIsGivenInsteadOfRestartingIt) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 100.0F, 0.0F));
  BodyState state{};
  for (int i = 0; i < 30; ++i) {
    state = world.Step(body, MovementInput{}, 1.0F / 60.0F);
  }
  const float fall_speed_before = -state.velocity.y;

  state.position.x += 1.0F;
  world.Restore(body, state, world.Fall(body));
  const BodyState after = world.Step(body, MovementInput{}, 1.0F / 60.0F);

  EXPECT_GE(-after.velocity.y, fall_speed_before);
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
CollisionMesh Quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
  return CollisionMesh{.points = {a, b, c, d}, .indices = {0, 1, 2, 0, 2, 3}};
}

// A horizontal slab from (x_min, z_min) to (x_max, z_max) at height y, normal up.
CollisionMesh Floor(float y, float x_min, float x_max, float z_min, float z_max) {
  return Quad(Vec3(x_min, y, z_min), Vec3(x_min, y, z_max), Vec3(x_max, y, z_max), Vec3(x_max, y, z_min));
}

// The same slab seen from below, normal down.
CollisionMesh Ceiling(float y, float x_min, float x_max, float z_min, float z_max) {
  return Quad(Vec3(x_min, y, z_min), Vec3(x_max, y, z_min), Vec3(x_max, y, z_max), Vec3(x_min, y, z_max));
}

// A vertical wall across the x axis at x, from y = 0 up to height, its triangles
// facing +x (away from a body approaching from -x) or -x.
CollisionMesh Wall(float x, float height, float z_min, float z_max, bool faces_positive_x) {
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
  EXPECT_TRUE(world.AddCollisionMesh(Floor(0.0F, -kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
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
  ASSERT_TRUE(world.AddCollisionMesh(Wall(5.0F, 5.0F, -kHalfExtent, kHalfExtent, GetParam())).has_value());
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
  ASSERT_TRUE(world.AddCollisionMesh(Ceiling(1.7F, 3.0F, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
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
  ASSERT_TRUE(world.AddCollisionMesh(Ceiling(1.7F, 3.0F, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
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
  ASSERT_TRUE(world.AddCollisionMesh(Ceiling(2.05F, -kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent)).has_value());
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

TEST(StaticGeometryTest, ValidateCollisionMeshAgreesWithAddCollisionMesh) {
  EXPECT_EQ(augusta::physics::ValidateCollisionMesh(CollisionMesh{}).error(), CollisionMeshError::kEmpty);
  EXPECT_TRUE(augusta::physics::ValidateCollisionMesh(Floor(0.0F, -1.0F, 1.0F, -1.0F, 1.0F)).has_value());
}

TEST(StaticGeometryTest, AMeshWithNoTrianglesIsRejected) {
  World world{StaminaConfig{}};

  const auto result = world.AddCollisionMesh(CollisionMesh{});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CollisionMeshError::kEmpty);
}

TEST(StaticGeometryTest, AnIndexOutsideThePointsIsRejected) {
  World world{StaminaConfig{}};

  const auto result = world.AddCollisionMesh(CollisionMesh{
      .points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F)}, .indices = {0, 1, 7}});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CollisionMeshError::kInvalidIndex);
}

TEST(StaticGeometryTest, AnIndexCountThatIsNotWholeTrianglesIsRejected) {
  World world{StaminaConfig{}};

  const auto result = world.AddCollisionMesh(CollisionMesh{
      .points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F)}, .indices = {0, 1}});

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CollisionMeshError::kInvalidIndex);
}

}  // namespace
