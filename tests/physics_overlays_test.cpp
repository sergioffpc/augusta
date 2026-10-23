#include "augusta/physics_overlays.h"

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
