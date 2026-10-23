#include "augusta/physics_overlays.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_index.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/tick.h"

// The Physics overlays' state, panel and shapes (ADR-0075): pure, so each
// frame's input is built here by hand, as ClientRuntime converts it.
namespace augusta::presentation {
namespace {

using debug::Capsule;
using debug::ControlEvent;
using debug::Panel;
using debug::Shapes;
using debug::Toggle;
using math::Vec3;
using physics::BodyState;
using physics::Stance;

constexpr EntityId kLocal{1};
constexpr EntityId kOther{2};

BodyState BodyAt(const Vec3& position, Stance stance = Stance::kStanding) {
  return BodyState{.position = position, .velocity = {}, .stance = stance};
}

WorldSnapshot Update(tick::Tick tick, std::vector<DynamicBody> bodies) {
  return WorldSnapshot{.tick = tick, .tick_duration = 1.0 / 60.0, .bodies = std::move(bodies)};
}

// A frame of updates, oldest first, with the local player's predicted body.
FrameInput FrameOf(const std::vector<WorldSnapshot>& updates, const BodyState& predicted = {}) {
  FrameInput input;
  input.ticks.latest.local_body = predicted;
  input.local_entity = kLocal;
  input.updates = updates;
  return input;
}

PhysicsOverlays ServerBodiesOn() { return PhysicsOverlays(PhysicsOverlaysStart{.server_bodies = true}); }

ControlEvent PanelEvent(std::string control, bool on) {
  return ControlEvent{.panel = std::string(kPhysicsPanelId), .control = std::move(control), .value = on};
}

// The toggle of panel whose id is id, or nullptr if it has none.
const Toggle* ToggleOf(const Panel& panel, const std::string& id) {
  for (const debug::Element& element : panel.elements) {
    if (const auto* toggle = std::get_if<Toggle>(&element); toggle != nullptr && toggle->id == id) {
      return toggle;
    }
  }
  return nullptr;
}

void ExpectCapsuleOf(const Capsule& drawn, const BodyState& body) {
  const physics::Capsule expected = physics::CapsuleOf(body);
  EXPECT_EQ(drawn.from, expected.bottom);
  EXPECT_EQ(drawn.to, expected.top);
  EXPECT_FLOAT_EQ(drawn.radius, expected.radius);
}

TEST(ServerBodiesTest, EveryBodyIsItsCapsuleAtTheNewestUpdateNotBetweenTheUpdates) {
  const std::vector<WorldSnapshot> updates{
      Update(10, {{.entity = kOther, .state = BodyAt(Vec3(0.0F, 0.0F, 0.0F)), .yaw = 0.0F}}),
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F)), .yaw = 0.0F}}),
  };

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates));

  ASSERT_EQ(shapes.capsules.size(), 1U);
  ExpectCapsuleOf(shapes.capsules.front(), BodyAt(Vec3(2.0F, 0.0F, 0.0F)));
  EXPECT_EQ(shapes.capsules.front().color, kServerBodyColor);
}

TEST(ServerBodiesTest, EachPlayerIsDrawnInItsStance) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F), Stance::kCrouching), .yaw = 0.0F},
                  {.entity = EntityId{3}, .state = BodyAt(Vec3(-2.0F, 0.0F, 0.0F), Stance::kProne), .yaw = 0.0F}}),
  };

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates));

  ASSERT_EQ(shapes.capsules.size(), 2U);
  ExpectCapsuleOf(shapes.capsules[0], BodyAt(Vec3(2.0F, 0.0F, 0.0F), Stance::kCrouching));
  ExpectCapsuleOf(shapes.capsules[1], BodyAt(Vec3(-2.0F, 0.0F, 0.0F), Stance::kProne));
}

TEST(ServerBodiesTest, TheLocalPlayersServerBodyIsDrawnBesideItsPredictedOne) {
  const BodyState server = BodyAt(Vec3(5.0F, 0.0F, 0.0F));
  const BodyState predicted = BodyAt(Vec3(5.5F, 0.0F, 0.0F), Stance::kCrouching);
  const std::vector<WorldSnapshot> updates{Update(11, {{.entity = kLocal, .state = server, .yaw = 0.0F}})};

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates, predicted));

  ASSERT_EQ(shapes.capsules.size(), 2U);
  ExpectCapsuleOf(shapes.capsules[0], server);
  EXPECT_EQ(shapes.capsules[0].color, kServerBodyColor);
  ExpectCapsuleOf(shapes.capsules[1], predicted);
  EXPECT_EQ(shapes.capsules[1].color, kPredictedBodyColor);
}

TEST(ServerBodiesTest, NoPredictedBodyIsDrawnForALocalPlayerTheUpdateNoLongerLists) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F)), .yaw = 0.0F}})};

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates, BodyAt(Vec3(5.0F, 0.0F, 0.0F))));

  ASSERT_EQ(shapes.capsules.size(), 1U);
  EXPECT_EQ(shapes.capsules.front().color, kServerBodyColor);
}

TEST(ServerBodiesTest, NothingIsDrawnWithoutAnUpdate) {
  EXPECT_EQ(ServerBodiesOn().BuildShapes(FrameOf({})), Shapes{});
}

TEST(ServerBodiesTest, NothingIsDrawnWhileTheOverlayIsOff) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F)), .yaw = 0.0F}})};
  // The control: on, the same frame draws.
  ASSERT_FALSE(ServerBodiesOn().BuildShapes(FrameOf(updates)).capsules.empty());

  EXPECT_EQ(PhysicsOverlays(PhysicsOverlaysStart{}).BuildShapes(FrameOf(updates)), Shapes{});
}

TEST(ServerBodiesTest, TheBodiesAreHiddenBehindTheWorldUntilThroughWallsIsOn) {
  const std::vector<WorldSnapshot> updates{Update(11, {{.entity = kLocal, .state = BodyAt({}), .yaw = 0.0F}})};
  PhysicsOverlays overlays = ServerBodiesOn();
  const Shapes hidden = overlays.BuildShapes(FrameOf(updates));
  ASSERT_EQ(hidden.capsules.size(), 2U);
  for (const Capsule& capsule : hidden.capsules) {
    EXPECT_FALSE(capsule.through_walls);
  }

  overlays.Update(PanelEvent("server_bodies.through_walls", true));

  const Shapes shapes = overlays.BuildShapes(FrameOf(updates));
  ASSERT_EQ(shapes.capsules.size(), 2U);
  for (const Capsule& capsule : shapes.capsules) {
    EXPECT_TRUE(capsule.through_walls);
  }
}

PhysicsOverlays CollisionOn() { return PhysicsOverlays(PhysicsOverlaysStart{.collision = true}); }

// A frame whose newest Prediction State carries lines, as Commit leaves them
// with Collision on.
FrameInput FrameWithLines(std::vector<physics::VisualizationLine> lines) {
  FrameInput input = FrameOf({});
  input.ticks.latest.collision_lines =
      std::make_shared<const std::vector<physics::VisualizationLine>>(std::move(lines));
  return input;
}

ControlEvent SliderEvent(std::string control, float value) {
  return ControlEvent{.panel = std::string(kPhysicsPanelId), .control = std::move(control), .value = value};
}

const debug::Slider* SliderOf(const Panel& panel, const std::string& id) {
  for (const debug::Element& element : panel.elements) {
    if (const auto* slider = std::get_if<debug::Slider>(&element); slider != nullptr && slider->id == id) {
      return slider;
    }
  }
  return nullptr;
}

TEST(CollisionTest, ThePredictionStatesLinesAreSegmentsInTheirOwnColours) {
  const FrameInput input = FrameWithLines({
      {.from = Vec3(0.0F, 0.0F, 0.0F), .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFFFF0000U},
      {.from = Vec3(0.0F, 1.0F, 0.0F), .to = Vec3(0.0F, 1.0F, 2.0F), .argb = 0xFF00FF00U},
      {.from = Vec3(3.0F, 0.0F, 0.0F), .to = Vec3(3.0F, 3.0F, 0.0F), .argb = 0xFF0000FFU},
  });

  const Shapes shapes = CollisionOn().BuildShapes(input);

  ASSERT_EQ(shapes.segments.size(), 3U);
  EXPECT_EQ(shapes.segments[0].from, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(shapes.segments[0].to, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(shapes.segments[0].color, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(shapes.segments[1].color, Vec3(0.0F, 1.0F, 0.0F));
  EXPECT_EQ(shapes.segments[2].from, Vec3(3.0F, 0.0F, 0.0F));
  EXPECT_EQ(shapes.segments[2].to, Vec3(3.0F, 3.0F, 0.0F));
  EXPECT_EQ(shapes.segments[2].color, Vec3(0.0F, 0.0F, 1.0F));
  for (const debug::Segment& segment : shapes.segments) {
    EXPECT_FLOAT_EQ(segment.alpha, 1.0F);
    EXPECT_FALSE(segment.through_walls);
  }
}

TEST(CollisionTest, AGreyLineKeepsItsShade) {
  const Shapes shapes =
      CollisionOn().BuildShapes(FrameWithLines({{.from = {}, .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFF336699U}}));

  ASSERT_EQ(shapes.segments.size(), 1U);
  EXPECT_FLOAT_EQ(shapes.segments[0].color.x, 0x33 / 255.0F);
  EXPECT_FLOAT_EQ(shapes.segments[0].color.y, 0x66 / 255.0F);
  EXPECT_FLOAT_EQ(shapes.segments[0].color.z, 0x99 / 255.0F);
}

TEST(CollisionTest, NothingIsDrawnWhileCollisionIsOff) {
  const FrameInput input = FrameWithLines({{.from = {}, .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFFFF0000U}});
  // The control: on, the same frame draws.
  ASSERT_FALSE(CollisionOn().BuildShapes(input).segments.empty());

  EXPECT_EQ(PhysicsOverlays(PhysicsOverlaysStart{}).BuildShapes(input), Shapes{});
}

TEST(CollisionTest, NothingIsDrawnWhileThePredictionStateCarriesNoLines) {
  EXPECT_EQ(CollisionOn().BuildShapes(FrameOf({})), Shapes{});
}

TEST(CollisionTest, ThroughWallsDrawsTheLinesOverTheWorld) {
  PhysicsOverlays overlays = CollisionOn();
  overlays.Update(PanelEvent("collision.through_walls", true));

  const Shapes shapes =
      overlays.BuildShapes(FrameWithLines({{.from = {}, .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFFFF0000U}}));

  ASSERT_EQ(shapes.segments.size(), 1U);
  EXPECT_TRUE(shapes.segments[0].through_walls);
}

TEST(CollisionTest, TheVisualizationIsOnWithItsFlagsAndABoxOfTheRadiusAroundTheCamera) {
  const physics::Visualization visualization = CollisionOn().Visualization(Vec3(4.0F, 1.5F, -2.0F));

  EXPECT_TRUE(visualization.on);
  EXPECT_TRUE(visualization.collision_shapes);
  EXPECT_FALSE(visualization.bounds);
  EXPECT_FALSE(visualization.contacts);
  EXPECT_FALSE(visualization.joints);
  EXPECT_TRUE(visualization.controller_volumes);
  EXPECT_EQ(visualization.cull_center, Vec3(4.0F, 1.5F, -2.0F));
  EXPECT_FLOAT_EQ(visualization.cull_half_extent, 20.0F);
}

TEST(CollisionTest, WithTheOverlayClosedTheVisualizationIsOff) {
  PhysicsOverlays overlays = CollisionOn();
  // The control: open, it is on.
  ASSERT_TRUE(overlays.Visualization(Vec3(4.0F, 1.5F, -2.0F)).on);

  overlays.Update(PanelEvent("collision", false));

  EXPECT_FALSE(overlays.Visualization(Vec3(4.0F, 1.5F, -2.0F)).on);
  EXPECT_FALSE(PhysicsOverlays(PhysicsOverlaysStart{}).Visualization(Vec3(4.0F, 1.5F, -2.0F)).on);
}

TEST(PhysicsPanelTest, TheCollisionFlagsComeBackAsStateChanges) {
  PhysicsOverlays overlays = CollisionOn();

  overlays.Update(PanelEvent("collision.shapes", false));
  overlays.Update(PanelEvent("collision.bounds", true));
  overlays.Update(PanelEvent("collision.contacts", true));
  overlays.Update(PanelEvent("collision.joints", true));
  overlays.Update(PanelEvent("collision.controller_volumes", false));

  const physics::Visualization visualization = overlays.Visualization({});
  EXPECT_FALSE(visualization.collision_shapes);
  EXPECT_TRUE(visualization.bounds);
  EXPECT_TRUE(visualization.contacts);
  EXPECT_TRUE(visualization.joints);
  EXPECT_FALSE(visualization.controller_volumes);
  const Panel panel = overlays.BuildPanel();
  for (const char* flag : {"collision.bounds", "collision.contacts", "collision.joints"}) {
    ASSERT_NE(ToggleOf(panel, flag), nullptr) << flag;
    EXPECT_TRUE(ToggleOf(panel, flag)->on) << flag;
  }
  for (const char* flag : {"collision.shapes", "collision.controller_volumes"}) {
    ASSERT_NE(ToggleOf(panel, flag), nullptr) << flag;
    EXPECT_FALSE(ToggleOf(panel, flag)->on) << flag;
  }
}

TEST(PhysicsPanelTest, ShowsTheCollisionToggleAndItsRadiusSliderFrom1To100Metres) {
  const Panel panel = CollisionOn().BuildPanel();

  const Toggle* on = ToggleOf(panel, "collision");
  ASSERT_NE(on, nullptr);
  EXPECT_EQ(on->label, "Collision");
  EXPECT_TRUE(on->on);
  ASSERT_NE(ToggleOf(panel, "collision.through_walls"), nullptr);
  const debug::Slider* radius = SliderOf(panel, "collision.radius");
  ASSERT_NE(radius, nullptr);
  EXPECT_FLOAT_EQ(radius->value, 20.0F);
  EXPECT_FLOAT_EQ(radius->min, 1.0F);
  EXPECT_FLOAT_EQ(radius->max, 100.0F);
}

TEST(PhysicsPanelTest, TheRadiusComesBackAsAStateChangeKeptWithin1To100Metres) {
  PhysicsOverlays overlays = CollisionOn();

  overlays.Update(SliderEvent("collision.radius", 35.0F));
  EXPECT_FLOAT_EQ(overlays.Visualization({}).cull_half_extent, 35.0F);
  EXPECT_FLOAT_EQ(SliderOf(overlays.BuildPanel(), "collision.radius")->value, 35.0F);

  overlays.Update(SliderEvent("collision.radius", 0.2F));
  EXPECT_FLOAT_EQ(overlays.Visualization({}).cull_half_extent, 1.0F);

  overlays.Update(SliderEvent("collision.radius", 500.0F));
  EXPECT_FLOAT_EQ(overlays.Visualization({}).cull_half_extent, 100.0F);
}

TEST(PhysicsPanelTest, ShowsEachOverlaysToggleAndThroughWallsToggleAsTheStateHasThem) {
  const Panel panel = ServerBodiesOn().BuildPanel();

  EXPECT_EQ(panel.id, kPhysicsPanelId);
  EXPECT_EQ(panel.title, "Physics");
  const Toggle* on = ToggleOf(panel, "server_bodies");
  const Toggle* through_walls = ToggleOf(panel, "server_bodies.through_walls");
  ASSERT_NE(on, nullptr);
  ASSERT_NE(through_walls, nullptr);
  EXPECT_EQ(on->label, "Server bodies");
  EXPECT_TRUE(on->on);
  EXPECT_FALSE(through_walls->on);
}

TEST(PhysicsPanelTest, ItsTogglesComeBackAsStateChanges) {
  PhysicsOverlays overlays(PhysicsOverlaysStart{});
  ASSERT_FALSE(overlays.IsOn(PhysicsOverlay::kServerBodies));

  overlays.Update(PanelEvent("server_bodies", true));
  overlays.Update(PanelEvent("server_bodies.through_walls", true));

  EXPECT_TRUE(overlays.IsOn(PhysicsOverlay::kServerBodies));
  EXPECT_TRUE(overlays.ThroughWalls(PhysicsOverlay::kServerBodies));
  const Panel panel = overlays.BuildPanel();
  EXPECT_TRUE(ToggleOf(panel, "server_bodies")->on);
  EXPECT_TRUE(ToggleOf(panel, "server_bodies.through_walls")->on);

  overlays.Update(PanelEvent("server_bodies", false));
  EXPECT_FALSE(overlays.IsOn(PhysicsOverlay::kServerBodies));
  EXPECT_TRUE(overlays.ThroughWalls(PhysicsOverlay::kServerBodies));
}

TEST(PhysicsPanelTest, AnotherPanelsEventChangesNothing) {
  PhysicsOverlays overlays(PhysicsOverlaysStart{});

  overlays.Update(ControlEvent{.panel = "profiling", .control = "server_bodies", .value = true});

  EXPECT_FALSE(overlays.IsOn(PhysicsOverlay::kServerBodies));
}

TEST(PhysicsPanelTest, AnOverlayStaysDrawnWithThePanelClosed) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F)), .yaw = 0.0F}})};
  debug::DebugIndex index({{.id = std::string(kPhysicsPanelId), .name = "Physics", .on = true}});
  PhysicsOverlays overlays(PhysicsOverlaysStart{});
  overlays.Update(PanelEvent("server_bodies", true));

  index.Update(ControlEvent{
      .panel = std::string(debug::kDebugIndexPanelId), .control = std::string(kPhysicsPanelId), .value = false});

  ASSERT_FALSE(index.IsOn(kPhysicsPanelId));
  EXPECT_EQ(overlays.BuildShapes(FrameOf(updates)).capsules.size(), 1U);
}

}  // namespace
}  // namespace augusta::presentation
