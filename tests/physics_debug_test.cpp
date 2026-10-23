#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"

// A World's visualization (Development build only, ADR-0074): PhysX's own
// lines of its collision, stepped once and copied out, for the Collision
// overlay to draw; and the process's OmniPVD recording.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::CollisionMesh;
using augusta::physics::MovementInput;
using augusta::physics::Recording;
using augusta::physics::RecordingError;
using augusta::physics::StaminaConfig;
using augusta::physics::Stance;
using augusta::physics::SurfaceId;
using augusta::physics::Visualization;
using augusta::physics::VisualizationLine;
using augusta::physics::World;

constexpr float kFixedTick = 1.0F / 60.0F;
constexpr SurfaceId kMap{1};
// How far past a shape's own bounds PhysX may draw it, in metres.
constexpr float kSlack = 0.05F;

CollisionMesh Floor(float y, float x_min, float x_max, float z_min, float z_max) {
  return CollisionMesh{
      .points = {Vec3(x_min, y, z_min), Vec3(x_max, y, z_min), Vec3(x_max, y, z_max), Vec3(x_min, y, z_max)},
      .indices = {0, 1, 2, 0, 2, 3}};
}

// Collision shapes and controller volumes, culled to a box of half_extent around center.
Visualization On(const Vec3& center, float half_extent) {
  Visualization visualization;
  visualization.on = true;
  visualization.cull_center = center;
  visualization.cull_half_extent = half_extent;
  return visualization;
}

bool Within(const Vec3& point, const Vec3& min, const Vec3& max) {
  return point.x >= min.x - kSlack && point.y >= min.y - kSlack && point.z >= min.z - kSlack &&
         point.x <= max.x + kSlack && point.y <= max.y + kSlack && point.z <= max.z + kSlack;
}

TEST(PhysicsVisualizationTest, AMeshInsideTheCullingBoxIsDrawnWithinItsBounds) {
  World world{StaminaConfig{}};
  ASSERT_TRUE(world.AddCollisionMesh(Floor(0.0F, -2.0F, 2.0F, -2.0F, 2.0F), kMap).has_value());
  world.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 10.0F));

  const std::optional<std::vector<VisualizationLine>> lines = world.StepVisualization(kFixedTick);

  ASSERT_TRUE(lines.has_value());
  ASSERT_FALSE(lines->empty());
  for (const VisualizationLine& line : *lines) {
    EXPECT_TRUE(Within(line.from, Vec3(-2.0F, 0.0F, -2.0F), Vec3(2.0F, 0.0F, 2.0F)));
    EXPECT_TRUE(Within(line.to, Vec3(-2.0F, 0.0F, -2.0F), Vec3(2.0F, 0.0F, 2.0F)));
  }
}

TEST(PhysicsVisualizationTest, AMeshOutsideTheCullingBoxIsNotDrawn) {
  World world{StaminaConfig{}};
  ASSERT_TRUE(world.AddCollisionMesh(Floor(0.0F, 50.0F, 54.0F, -2.0F, 2.0F), kMap).has_value());
  // The control: a box around it draws it.
  world.SetVisualization(On(Vec3(52.0F, 0.0F, 0.0F), 10.0F));
  ASSERT_FALSE(world.StepVisualization(kFixedTick).value_or(std::vector<VisualizationLine>{}).empty());
  world.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 10.0F));

  const std::optional<std::vector<VisualizationLine>> lines = world.StepVisualization(kFixedTick);

  ASSERT_TRUE(lines.has_value());
  EXPECT_TRUE(lines->empty());
}

TEST(PhysicsVisualizationTest, ABodysCapsuleIsDrawnAroundItsPosition) {
  World world{StaminaConfig{}};
  ASSERT_TRUE(world.AddCollisionMesh(Floor(0.0F, -20.0F, 20.0F, -20.0F, 20.0F), kMap).has_value());
  // Away from the origin, where its controller was made.
  const auto body = world.CreateBody(Vec3(5.0F, 0.0F, 3.0F));
  for (int i = 0; i < 30; ++i) {
    world.Step(body, MovementInput{}, kFixedTick);
  }
  const BodyState state = world.Step(body, MovementInput{}, kFixedTick);
  // Only the body is drawn: the floor lies outside the box.
  Visualization visualization = On(state.position + Vec3(0.0F, 1.0F, 0.0F), 0.9F);
  visualization.controller_volumes = false;
  world.SetVisualization(visualization);

  const std::optional<std::vector<VisualizationLine>> lines = world.StepVisualization(kFixedTick);

  ASSERT_TRUE(lines.has_value());
  ASSERT_FALSE(lines->empty());
  const float height = augusta::physics::StanceHeight(Stance::kStanding);
  const Vec3 min = state.position - Vec3(0.3F, 0.0F, 0.3F);
  const Vec3 max = state.position + Vec3(0.3F, height, 0.3F);
  for (const VisualizationLine& line : *lines) {
    EXPECT_TRUE(Within(line.from, min, max));
    EXPECT_TRUE(Within(line.to, min, max));
  }
}

TEST(PhysicsVisualizationTest, ControllerVolumesAreDrawnWhenAsked) {
  World world{StaminaConfig{}};
  const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));
  Visualization visualization = On(Vec3(100.0F, 100.0F, 100.0F), 1.0F);
  visualization.collision_shapes = false;
  world.SetVisualization(visualization);
  MovementInput walk;
  walk.direction = Vec3(1.0F, 0.0F, 0.0F);
  world.Step(body, walk, kFixedTick);

  const std::optional<std::vector<VisualizationLine>> lines = world.StepVisualization(kFixedTick);

  ASSERT_TRUE(lines.has_value());
  EXPECT_FALSE(lines->empty());
}

TEST(PhysicsVisualizationTest, OffSteppingIsRefused) {
  World world{StaminaConfig{}};
  ASSERT_TRUE(world.AddCollisionMesh(Floor(0.0F, -2.0F, 2.0F, -2.0F, 2.0F), kMap).has_value());
  // The control: another World alike, on, steps.
  World on{StaminaConfig{}};
  ASSERT_TRUE(on.AddCollisionMesh(Floor(0.0F, -2.0F, 2.0F, -2.0F, 2.0F), kMap).has_value());
  on.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 10.0F));
  ASSERT_TRUE(on.StepVisualization(kFixedTick).has_value());

  EXPECT_FALSE(world.StepVisualization(kFixedTick).has_value());
}

// The settings are set every tick but applied to PhysX only when they change:
// the same again keeps what is drawn, and a moved box draws what it now holds.
TEST(PhysicsVisualizationTest, EachTicksSettingsAreDrawnAsLastSet) {
  World world{StaminaConfig{}};
  ASSERT_TRUE(world.AddCollisionMesh(Floor(0.0F, 50.0F, 54.0F, -2.0F, 2.0F), kMap).has_value());
  world.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 10.0F));
  world.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 10.0F));
  const std::optional<std::vector<VisualizationLine>> outside = world.StepVisualization(kFixedTick);
  ASSERT_TRUE(outside.has_value());
  ASSERT_TRUE(outside->empty());

  world.SetVisualization(On(Vec3(52.0F, 0.0F, 0.0F), 10.0F));
  world.SetVisualization(On(Vec3(52.0F, 0.0F, 0.0F), 10.0F));
  EXPECT_FALSE(world.StepVisualization(kFixedTick).value_or(std::vector<VisualizationLine>{}).empty());

  world.SetVisualization(Visualization{});
  world.SetVisualization(Visualization{});
  EXPECT_FALSE(world.StepVisualization(kFixedTick).has_value());

  world.SetVisualization(On(Vec3(52.0F, 0.0F, 0.0F), 10.0F));
  EXPECT_FALSE(world.StepVisualization(kFixedTick).value_or(std::vector<VisualizationLine>{}).empty());
}

TEST(PhysicsVisualizationTest, TurnedOffAgainSteppingIsRefused) {
  World world{StaminaConfig{}};
  ASSERT_TRUE(world.AddCollisionMesh(Floor(0.0F, -2.0F, 2.0F, -2.0F, 2.0F), kMap).has_value());
  world.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 10.0F));
  ASSERT_TRUE(world.StepVisualization(kFixedTick).has_value());

  world.SetVisualization(Visualization{});

  EXPECT_FALSE(world.StepVisualization(kFixedTick).has_value());
}

// The steps a body takes walking over a floor, crouching for a while, then
// falling off its ledge: with the visualization stepped after each when
// visualize is set.
std::vector<std::pair<BodyState, augusta::physics::FallState>> Walk(bool visualize) {
  World world{StaminaConfig{}};
  EXPECT_TRUE(world.AddCollisionMesh(Floor(0.0F, -2.0F, 4.0F, -2.0F, 2.0F), kMap).has_value());
  EXPECT_TRUE(world.AddCollisionMesh(Floor(-1.0F, 4.0F, 20.0F, -2.0F, 2.0F), kMap).has_value());
  if (visualize) {
    world.SetVisualization(On(Vec3(0.0F, 0.0F, 0.0F), 20.0F));
  }
  const auto body = world.CreateBody(Vec3(0.0F, 0.5F, 0.0F));
  std::vector<std::pair<BodyState, augusta::physics::FallState>> steps;
  for (int i = 0; i < 180; ++i) {
    MovementInput input;
    input.direction = Vec3(1.0F, 0.0F, 0.0F);
    input.desired_stance = i >= 60 && i < 90 ? Stance::kCrouching : Stance::kStanding;
    const BodyState state = world.Step(body, input, kFixedTick);
    steps.emplace_back(state, world.Fall(body));
    if (visualize) {
      EXPECT_TRUE(world.StepVisualization(kFixedTick).has_value());
    }
  }
  return steps;
}

TEST(PhysicsVisualizationTest, SteppingTheVisualizationChangesNoBodysLaterStep) {
  const auto without = Walk(false);
  const auto with = Walk(true);

  ASSERT_EQ(with.size(), without.size());
  for (std::size_t i = 0; i < with.size(); ++i) {
    EXPECT_EQ(with[i].first, without[i].first) << "step " << i;
    EXPECT_EQ(with[i].second.vertical_speed, without[i].second.vertical_speed) << "step " << i;
    EXPECT_EQ(with[i].second.grounded, without[i].second.grounded) << "step " << i;
  }
  // The walk did cross the ledge and fall.
  EXPECT_LT(without.back().first.position.y, -0.5F);
}

// Ends recording, if it records, and removes the file it wrote.
void Discard(std::expected<Recording, RecordingError> recording) {
  if (!recording.has_value()) {
    return;
  }
  const std::filesystem::path file = recording->File();
  recording = std::unexpected(RecordingError::kUnavailable);
  std::error_code ignored;
  std::filesystem::remove(file, ignored);
}

// PhysX's OmniPVD build (ports/physx) linked and called: a Debug build with
// OmniPVD's runtime beside the executable records, any other refuses it as
// unavailable; neither crashes, and either way a World runs after it.
TEST(PhysicsRecordingTest, StartsOrIsUnavailable) {
  std::expected<Recording, RecordingError> recording = augusta::physics::StartRecording("augusta_physics_tests");

  if (recording.has_value()) {
    EXPECT_EQ(recording->File().extension(), ".ovd");
  } else {
    EXPECT_EQ(recording.error(), RecordingError::kUnavailable);
    EXPECT_FALSE(augusta::physics::DescribeRecordingError(recording.error()).empty());
  }
  {
    World world{StaminaConfig{}};
    const auto body = world.CreateBody(Vec3(0.0F, 0.0F, 0.0F));
    world.Step(body, MovementInput{}, kFixedTick);
  }
  Discard(std::move(recording));
}

TEST(PhysicsRecordingTest, IsRefusedWhileAWorldExists) {
  // The control: with no World, it is not refused for one.
  std::expected<Recording, RecordingError> alone = augusta::physics::StartRecording("augusta_physics_tests");
  ASSERT_TRUE(alone.has_value() || alone.error() != RecordingError::kWorldExists);
  Discard(std::move(alone));
  const World world{StaminaConfig{}};

  const std::expected<Recording, RecordingError> recording = augusta::physics::StartRecording("augusta_physics_tests");

  ASSERT_FALSE(recording.has_value());
  EXPECT_EQ(recording.error(), RecordingError::kWorldExists);
}

}  // namespace
