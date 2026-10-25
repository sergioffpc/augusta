#include "augusta/physics_overlays.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/debug_index.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/interpolation.h"
#include "augusta/judged_rounds.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/tick.h"

// The Physics overlays' state, panel and shapes (ADR-0075): pure, so each
// frame's input is built here by hand, as ClientRuntime converts it.
namespace augusta::presentation {
namespace {

using body_part::BodyPart;
using debug::Capsule;
using debug::ControlEvent;
using debug::Panel;
using debug::Segment;
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

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates), State{});

  ASSERT_EQ(shapes.capsules.size(), 1U);
  ExpectCapsuleOf(shapes.capsules.front(), BodyAt(Vec3(2.0F, 0.0F, 0.0F)));
  EXPECT_EQ(shapes.capsules.front().color, kServerBodyColor);
}

TEST(ServerBodiesTest, EachPlayerIsDrawnInItsStance) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F), Stance::kCrouching), .yaw = 0.0F},
                  {.entity = EntityId{3}, .state = BodyAt(Vec3(-2.0F, 0.0F, 0.0F), Stance::kProne), .yaw = 0.0F}}),
  };

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates), State{});

  ASSERT_EQ(shapes.capsules.size(), 2U);
  ExpectCapsuleOf(shapes.capsules[0], BodyAt(Vec3(2.0F, 0.0F, 0.0F), Stance::kCrouching));
  ExpectCapsuleOf(shapes.capsules[1], BodyAt(Vec3(-2.0F, 0.0F, 0.0F), Stance::kProne));
}

TEST(ServerBodiesTest, TheLocalPlayersServerBodyIsDrawnBesideItsPredictedOne) {
  const BodyState server = BodyAt(Vec3(5.0F, 0.0F, 0.0F));
  const BodyState predicted = BodyAt(Vec3(5.5F, 0.0F, 0.0F), Stance::kCrouching);
  const std::vector<WorldSnapshot> updates{Update(11, {{.entity = kLocal, .state = server, .yaw = 0.0F}})};

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates, predicted), State{});

  ASSERT_EQ(shapes.capsules.size(), 2U);
  ExpectCapsuleOf(shapes.capsules[0], server);
  EXPECT_EQ(shapes.capsules[0].color, kServerBodyColor);
  ExpectCapsuleOf(shapes.capsules[1], predicted);
  EXPECT_EQ(shapes.capsules[1].color, kPredictedBodyColor);
}

TEST(ServerBodiesTest, NoPredictedBodyIsDrawnForALocalPlayerTheUpdateNoLongerLists) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F)), .yaw = 0.0F}})};

  const Shapes shapes = ServerBodiesOn().BuildShapes(FrameOf(updates, BodyAt(Vec3(5.0F, 0.0F, 0.0F))), State{});

  ASSERT_EQ(shapes.capsules.size(), 1U);
  EXPECT_EQ(shapes.capsules.front().color, kServerBodyColor);
}

TEST(ServerBodiesTest, NothingIsDrawnWithoutAnUpdate) {
  EXPECT_EQ(ServerBodiesOn().BuildShapes(FrameOf({}), State{}), Shapes{});
}

TEST(ServerBodiesTest, NothingIsDrawnWhileTheOverlayIsOff) {
  const std::vector<WorldSnapshot> updates{
      Update(11, {{.entity = kOther, .state = BodyAt(Vec3(2.0F, 0.0F, 0.0F)), .yaw = 0.0F}})};
  // The control: on, the same frame draws.
  ASSERT_FALSE(ServerBodiesOn().BuildShapes(FrameOf(updates), State{}).capsules.empty());

  EXPECT_EQ(PhysicsOverlays(PhysicsOverlaysStart{}).BuildShapes(FrameOf(updates), State{}), Shapes{});
}

TEST(ServerBodiesTest, TheBodiesAreHiddenBehindTheWorldUntilThroughWallsIsOn) {
  const std::vector<WorldSnapshot> updates{Update(11, {{.entity = kLocal, .state = BodyAt({}), .yaw = 0.0F}})};
  PhysicsOverlays overlays = ServerBodiesOn();
  const Shapes hidden = overlays.BuildShapes(FrameOf(updates), State{});
  ASSERT_EQ(hidden.capsules.size(), 2U);
  for (const Capsule& capsule : hidden.capsules) {
    EXPECT_FALSE(capsule.through_walls);
  }

  overlays.Update(PanelEvent("server_bodies.through_walls", true));

  const Shapes shapes = overlays.BuildShapes(FrameOf(updates), State{});
  ASSERT_EQ(shapes.capsules.size(), 2U);
  for (const Capsule& capsule : shapes.capsules) {
    EXPECT_TRUE(capsule.through_walls);
  }
}

// A Character's Hitboxes, standing in its root space: one triangle each, the
// head's high and the limb's low.
const std::vector<hitbox::CharacterHitbox> kSoldierHitboxes{
    {.part = BodyPart::kHead,
     .triangles = {{.a = Vec3(0.0F, 1.7F, 0.0F), .b = Vec3(0.2F, 1.7F, 0.0F), .c = Vec3(0.0F, 1.7F, 0.2F)}}},
    {.part = BodyPart::kLimb,
     .triangles = {{.a = Vec3(0.0F, 0.3F, 0.0F), .b = Vec3(0.2F, 0.3F, 0.0F), .c = Vec3(0.0F, 0.3F, 0.2F)}}},
};

PhysicsOverlays HitboxesOn() {
  PhysicsOverlays overlays(PhysicsOverlaysStart{.hitboxes = true});
  overlays.SetCharacterHitboxes("soldier", kSoldierHitboxes);
  return overlays;
}

// A frame's Presentation State showing one other player as character, at body.
State ShowingOther(const RemoteBody& body, std::string character = "soldier") {
  State state;
  state.remote_players.push_back({.entity = kOther, .body = body, .character = std::move(character)});
  return state;
}

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  constexpr float kTolerance = 1e-5F;
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// segments, in order, are the three edges of each of placed's triangles,
// Hitbox by Hitbox.
void ExpectOutlinesOf(const std::vector<Segment>& segments, const std::vector<hitbox::PlacedHitbox>& placed) {
  std::vector<std::pair<Vec3, Vec3>> edges;
  for (const hitbox::PlacedHitbox& hitbox : placed) {
    for (const ballistics::Triangle& triangle : hitbox.triangles) {
      edges.insert(edges.end(), {{triangle.a, triangle.b}, {triangle.b, triangle.c}, {triangle.c, triangle.a}});
    }
  }
  ASSERT_EQ(segments.size(), edges.size());
  for (std::size_t i = 0; i < edges.size(); ++i) {
    ExpectNear(segments[i].from, edges[i].first);
    ExpectNear(segments[i].to, edges[i].second);
  }
}

TEST(HitboxesTest, AnotherPlayersHitboxesAreOutlinedWhereItsInterpolatedPoseStands) {
  const Shapes shapes =
      HitboxesOn().BuildShapes(FrameOf({}), ShowingOther({.position = Vec3(3.0F, 0.0F, -1.0F), .velocity = {}}));

  // Standing and facing where a view of yaw 0 looks, the Hitboxes are only moved.
  ASSERT_EQ(shapes.segments.size(), 6U);
  ExpectNear(shapes.segments[0].from, Vec3(3.0F, 1.7F, -1.0F));
  ExpectNear(shapes.segments[0].to, Vec3(3.2F, 1.7F, -1.0F));
  ExpectNear(shapes.segments[1].to, Vec3(3.0F, 1.7F, -0.8F));
  ExpectNear(shapes.segments[3].from, Vec3(3.0F, 0.3F, -1.0F));
}

TEST(HitboxesTest, TheHitboxesAreTurnedAsTheServerPlacesThem) {
  const RemoteBody body{.position = Vec3(-2.0F, 0.5F, 4.0F),
                        .velocity = Vec3(1.0F, 0.0F, 0.0F),
                        .yaw = std::numbers::pi_v<float> / 2.0F,
                        .stance = Stance::kCrouching};

  const Shapes shapes = HitboxesOn().BuildShapes(FrameOf({}), ShowingOther(body));

  // The server's Lag compensation places them with this one function (ADR-0044).
  ExpectOutlinesOf(shapes.segments, hitbox::Place(kSoldierHitboxes,
                                                  {.position = body.position, .yaw = body.yaw, .stance = body.stance}));
}

TEST(HitboxesTest, EachHitboxIsColouredByItsBodyPart) {
  const Shapes shapes = HitboxesOn().BuildShapes(FrameOf({}), ShowingOther({}));

  ASSERT_EQ(shapes.segments.size(), 6U);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(shapes.segments[i].color, HitboxColorOf(BodyPart::kHead));
    EXPECT_EQ(shapes.segments[3 + i].color, HitboxColorOf(BodyPart::kLimb));
  }
  EXPECT_NE(HitboxColorOf(BodyPart::kHead), HitboxColorOf(BodyPart::kTorso));
  EXPECT_NE(HitboxColorOf(BodyPart::kTorso), HitboxColorOf(BodyPart::kLimb));
  EXPECT_NE(HitboxColorOf(BodyPart::kLimb), HitboxColorOf(BodyPart::kHead));
}

TEST(HitboxesTest, APlayerWhoseCharacterHasNoHitboxesKnownDrawsNothing) {
  // The control: the soldier's are drawn.
  ASSERT_FALSE(HitboxesOn().BuildShapes(FrameOf({}), ShowingOther({})).segments.empty());

  EXPECT_EQ(HitboxesOn().BuildShapes(FrameOf({}), ShowingOther({}, "medic")), Shapes{});
}

TEST(HitboxesTest, NothingIsDrawnWhileTheOverlayIsOff) {
  // The control: on, the same frame draws.
  ASSERT_FALSE(HitboxesOn().BuildShapes(FrameOf({}), ShowingOther({})).segments.empty());
  PhysicsOverlays overlays(PhysicsOverlaysStart{});
  overlays.SetCharacterHitboxes("soldier", kSoldierHitboxes);

  EXPECT_EQ(overlays.BuildShapes(FrameOf({}), ShowingOther({})), Shapes{});
}

TEST(HitboxesTest, TheHitboxesAreHiddenBehindTheWorldUntilThroughWallsIsOn) {
  PhysicsOverlays overlays = HitboxesOn();
  const Shapes hidden = overlays.BuildShapes(FrameOf({}), ShowingOther({}));
  ASSERT_FALSE(hidden.segments.empty());
  for (const Segment& segment : hidden.segments) {
    EXPECT_FALSE(segment.through_walls);
  }

  overlays.Update(PanelEvent("hitboxes.through_walls", true));

  const Shapes shapes = overlays.BuildShapes(FrameOf({}), ShowingOther({}));
  ASSERT_FALSE(shapes.segments.empty());
  for (const Segment& segment : shapes.segments) {
    EXPECT_TRUE(segment.through_walls);
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

  const Shapes shapes = CollisionOn().BuildShapes(input, State{});

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
  const Shapes shapes = CollisionOn().BuildShapes(
      FrameWithLines({{.from = {}, .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFF336699U}}), State{});

  ASSERT_EQ(shapes.segments.size(), 1U);
  EXPECT_FLOAT_EQ(shapes.segments[0].color.x, 0x33 / 255.0F);
  EXPECT_FLOAT_EQ(shapes.segments[0].color.y, 0x66 / 255.0F);
  EXPECT_FLOAT_EQ(shapes.segments[0].color.z, 0x99 / 255.0F);
}

TEST(CollisionTest, NothingIsDrawnWhileCollisionIsOff) {
  const FrameInput input = FrameWithLines({{.from = {}, .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFFFF0000U}});
  // The control: on, the same frame draws.
  ASSERT_FALSE(CollisionOn().BuildShapes(input, State{}).segments.empty());

  EXPECT_EQ(PhysicsOverlays(PhysicsOverlaysStart{}).BuildShapes(input, State{}), Shapes{});
}

TEST(CollisionTest, NothingIsDrawnWhileThePredictionStateCarriesNoLines) {
  EXPECT_EQ(CollisionOn().BuildShapes(FrameOf({}), State{}), Shapes{});
}

TEST(CollisionTest, ThroughWallsDrawsTheLinesOverTheWorld) {
  PhysicsOverlays overlays = CollisionOn();
  overlays.Update(PanelEvent("collision.through_walls", true));

  const Shapes shapes =
      overlays.BuildShapes(FrameWithLines({{.from = {}, .to = Vec3(1.0F, 0.0F, 0.0F), .argb = 0xFFFF0000U}}), State{});

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

TEST(PhysicsPanelTest, TheHitboxesTogglesComeBackAsStateChanges) {
  PhysicsOverlays overlays(PhysicsOverlaysStart{});
  const Panel before = overlays.BuildPanel();
  ASSERT_NE(ToggleOf(before, "hitboxes"), nullptr);
  ASSERT_NE(ToggleOf(before, "hitboxes.through_walls"), nullptr);
  EXPECT_EQ(ToggleOf(before, "hitboxes")->label, "Hitboxes");
  EXPECT_FALSE(ToggleOf(before, "hitboxes")->on);
  EXPECT_FALSE(ToggleOf(before, "hitboxes.through_walls")->on);

  overlays.Update(PanelEvent("hitboxes", true));
  overlays.Update(PanelEvent("hitboxes.through_walls", true));

  EXPECT_TRUE(overlays.IsOn(PhysicsOverlay::kHitboxes));
  EXPECT_TRUE(overlays.ThroughWalls(PhysicsOverlay::kHitboxes));
  EXPECT_FALSE(overlays.IsOn(PhysicsOverlay::kServerBodies));
  const Panel after = overlays.BuildPanel();
  EXPECT_TRUE(ToggleOf(after, "hitboxes")->on);
  EXPECT_TRUE(ToggleOf(after, "hitboxes.through_walls")->on);
}

TEST(PhysicsPanelTest, AnotherPanelsEventChangesNothing) {
  // The control: the same event of the Physics panel switches it.
  PhysicsOverlays control(PhysicsOverlaysStart{});
  control.Update(PanelEvent("server_bodies", true));
  ASSERT_TRUE(control.IsOn(PhysicsOverlay::kServerBodies));
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
  EXPECT_EQ(overlays.BuildShapes(FrameOf(updates), State{}).capsules.size(), 1U);
}

using Clock = PhysicsOverlays::Clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

const Clock::time_point kNow{seconds{1000}};

// The Map a judgement's flight is flown against: none, so it flies on to the
// point struck.
const physics::World& NoMap() {
  static const physics::World map{physics::StaminaConfig{}};
  return map;
}
constexpr tick::Tick kShotTick = 100;
// The other player as the match named it.
const std::vector<PlayerCharacter> kCharacters{{.entity = kOther, .character = "soldier"}};

FrameInput FiredSoFar(std::uint32_t total);

// Shot judgements on, after a first frame of the match, which names its
// players' characters.
PhysicsOverlays ShotJudgementsOn() {
  PhysicsOverlays overlays(PhysicsOverlaysStart{.shot_judgements = true});
  overlays.SetCharacterHitboxes("soldier", kSoldierHitboxes);
  overlays.Advance(FiredSoFar(0), State{}, kNow);
  return overlays;
}

// A frame whose newest Prediction State has the local rifle's running total
// of rounds fired at total.
FrameInput FiredSoFar(std::uint32_t total) {
  FrameInput input = FrameOf({});
  input.ticks.latest.total_rounds_fired = total;
  input.characters = kCharacters;
  return input;
}

// A frame's Presentation State showing the other player at position, as of
// the Seen time seen_tick + fraction.
State SeenAt(const Vec3& position, tick::Tick seen_tick, float fraction = 0.0F) {
  State state = ShowingOther({.position = position, .velocity = {}});
  state.seen_time = SeenTime{.tick = seen_tick, .fraction = fraction};
  return state;
}

// The local rifle fires one round on a frame showing state, after a frame
// that only started the count.
void FireOneRound(PhysicsOverlays& overlays, const State& state, Clock::time_point now,
                  std::uint32_t fired_before = 0) {
  overlays.Advance(FiredSoFar(fired_before), state, now);
  overlays.Advance(FiredSoFar(fired_before + 1), state, now);
}

// A judgement of the round fired on kShotTick with a Shooter's delay of delay
// ticks, which flew past the other player standing at position and struck nothing.
ShotJudgement JudgedPast(const Vec3& position, float delay) {
  return ShotJudgement{
      .shot_tick = kShotTick,
      .shooters_delay = delay,
      .struck = Struck::kNothing,
      .part = BodyPart::kTorso,
      .point = {},
      .target = std::nullopt,
      .entries = {{.players = {{.entity = kOther, .pose = {.position = position, .yaw = 0.0F}}}}},
      .entries_left_out = false,
  };
}

// Whether panel shows text on a line of its own.
bool Shows(const Panel& panel, std::string_view text) {
  for (const debug::Element& element : panel.elements) {
    if (const auto* line = std::get_if<debug::Text>(&element); line != nullptr && line->text == text) {
      return true;
    }
  }
  return false;
}

// The segments of shapes in color.
std::vector<Segment> SegmentsIn(const Shapes& shapes, const Vec3& color) {
  std::vector<Segment> in;
  for (const Segment& segment : shapes.segments) {
    if (segment.color == color) {
      in.push_back(segment);
    }
  }
  return in;
}

std::vector<hitbox::PlacedHitbox> SoldierAt(const Vec3& position) {
  return hitbox::Place(kSoldierHitboxes, {.position = position, .yaw = 0.0F});
}

TEST(ShotJudgementsTest, ARoundFiredKeepsTheSeenHitboxesBesideTheJudgedOnes) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(3.0F, 0.0F, -1.0F), 98), kNow);

  overlays.Judge(JudgedPast(Vec3(3.5F, 0.0F, -1.0F), 2.0F), NoMap(), kNow + milliseconds{200});
  overlays.Advance(FiredSoFar(1), State{}, kNow + milliseconds{210});
  const Shapes shapes = overlays.BuildShapes(FiredSoFar(1), State{});

  ExpectOutlinesOf(SegmentsIn(shapes, kSeenHitboxColor), SoldierAt(Vec3(3.0F, 0.0F, -1.0F)));
  ExpectOutlinesOf(SegmentsIn(shapes, kJudgedHitboxColor), SoldierAt(Vec3(3.5F, 0.0F, -1.0F)));
}

TEST(ShotJudgementsTest, AJudgementPairsWithTheRecordOfItsSeenTime) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 97, 0.5F), kNow);
  FireOneRound(overlays, SeenAt(Vec3(2.0F, 0.0F, 0.0F), 98, 0.5F), kNow + milliseconds{16}, 1);
  FireOneRound(overlays, SeenAt(Vec3(3.0F, 0.0F, 0.0F), 99, 0.5F), kNow + milliseconds{32}, 2);

  // Fired on tick 100, seen 1.4 ticks before: nearest the round seen at 98.5.
  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.4F), NoMap(), kNow + milliseconds{200});

  ExpectOutlinesOf(SegmentsIn(overlays.BuildShapes(FiredSoFar(3), State{}), kSeenHitboxColor),
                   SoldierAt(Vec3(2.0F, 0.0F, 0.0F)));
}

TEST(ShotJudgementsTest, AJudgementWithNoRecordWithinATickIsDrawnJudgedOnly) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 90), kNow);

  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.0F), NoMap(), kNow + milliseconds{200});

  const Shapes shapes = overlays.BuildShapes(FiredSoFar(1), State{});
  EXPECT_TRUE(SegmentsIn(shapes, kSeenHitboxColor).empty());
  ExpectOutlinesOf(SegmentsIn(shapes, kJudgedHitboxColor), SoldierAt(Vec3(9.0F, 0.0F, 0.0F)));
}

TEST(ShotJudgementsTest, ARoundFiredWithTheOverlayOffKeepsNoSeenHitboxes) {
  // The control: fired with it on, the round keeps them.
  PhysicsOverlays control = ShotJudgementsOn();
  FireOneRound(control, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow);
  control.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.0F), NoMap(), kNow + milliseconds{200});
  ASSERT_FALSE(SegmentsIn(control.BuildShapes(FiredSoFar(1), State{}), kSeenHitboxColor).empty());
  PhysicsOverlays overlays(PhysicsOverlaysStart{});
  overlays.SetCharacterHitboxes("soldier", kSoldierHitboxes);
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow);
  overlays.Update(PanelEvent("shot_judgements", true));

  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.0F), NoMap(), kNow + milliseconds{200});

  const Shapes shapes = overlays.BuildShapes(FiredSoFar(1), State{});
  EXPECT_TRUE(SegmentsIn(shapes, kSeenHitboxColor).empty());
  EXPECT_FALSE(SegmentsIn(shapes, kJudgedHitboxColor).empty());
}

// A judgement of the round fired on kShotTick, seen at its Seen time, that
// struck the other player's head standing at position on its second judged
// tick, at point.
ShotJudgement StruckHead(const Vec3& position, const Vec3& point) {
  ShotJudgement judgement = JudgedPast(Vec3(position.x - 1.0F, position.y, position.z), 0.0F);
  judgement.struck = Struck::kHitbox;
  judgement.part = BodyPart::kHead;
  judgement.point = point;
  judgement.target = kOther;
  judgement.entries.push_back({.players = {{.entity = kOther, .pose = {.position = position, .yaw = 0.0F}}}});
  return judgement;
}

TEST(ShotJudgementsTest, TheStruckHitboxIsFilled) {
  PhysicsOverlays overlays = ShotJudgementsOn();

  overlays.Judge(StruckHead(Vec3(2.0F, 0.0F, 0.0F), Vec3(2.1F, 1.7F, 0.0F)), NoMap(), kNow);

  const Shapes shapes = overlays.BuildShapes(FiredSoFar(0), State{});
  const hitbox::PlacedHitbox head = SoldierAt(Vec3(2.0F, 0.0F, 0.0F))[0];
  ASSERT_EQ(head.part, BodyPart::kHead);
  ASSERT_EQ(shapes.triangles.size(), 1U);
  ExpectNear(shapes.triangles[0].corners[0], head.triangles[0].a);
  ExpectNear(shapes.triangles[0].corners[1], head.triangles[0].b);
  ExpectNear(shapes.triangles[0].corners[2], head.triangles[0].c);
  EXPECT_EQ(shapes.triangles[0].color, kJudgedHitboxColor);
  // Both judged ticks are outlined: two soldiers' Hitboxes.
  EXPECT_EQ(SegmentsIn(shapes, kJudgedHitboxColor).size(), 12U);
}

TEST(ShotJudgementsTest, AMissFillsNoHitbox) {
  // The control: a round that struck the head fills it.
  PhysicsOverlays control = ShotJudgementsOn();
  control.Judge(StruckHead(Vec3(2.0F, 0.0F, 0.0F), Vec3(2.1F, 1.7F, 0.0F)), NoMap(), kNow);
  ASSERT_FALSE(control.BuildShapes(FiredSoFar(0), State{}).triangles.empty());
  PhysicsOverlays overlays = ShotJudgementsOn();

  overlays.Judge(JudgedPast(Vec3(2.0F, 0.0F, 0.0F), 0.0F), NoMap(), kNow);

  EXPECT_TRUE(overlays.BuildShapes(FiredSoFar(0), State{}).triangles.empty());
}

TEST(ShotJudgementsTest, ThePointStruckIsASmallSphere) {
  PhysicsOverlays overlays = ShotJudgementsOn();

  overlays.Judge(StruckHead(Vec3(2.0F, 0.0F, 0.0F), Vec3(2.1F, 1.7F, 0.0F)), NoMap(), kNow);

  const Shapes shapes = overlays.BuildShapes(FiredSoFar(0), State{});
  ASSERT_EQ(shapes.spheres.size(), 1U);
  EXPECT_EQ(shapes.spheres[0].center, Vec3(2.1F, 1.7F, 0.0F));
  EXPECT_FLOAT_EQ(shapes.spheres[0].radius, kStruckPointRadius);
  EXPECT_EQ(shapes.spheres[0].style, debug::Style::kFilled);
}

// Parameters under which a round flies level at 60 m/s: 1 m a tick of 1/60 s.
parameters::Parameters LevelFlight() {
  parameters::Parameters parameters;
  parameters.rifle.muzzle_velocity = 60.0F;
  parameters.ammo.flight = {.gravity = 0.0F, .max_range = 1000.0F};
  return parameters;
}

// Shot judgements on, flying level, after a frame announcing the local
// player's Shot of kShotTick from (0, 1.5, 0) down -Z, where a view of yaw 0
// looks, beside another player's.
PhysicsOverlays FlyingOwnShot() {
  PhysicsOverlays overlays = ShotJudgementsOn();
  overlays.SetParameters(LevelFlight(), 1.0F / 60.0F);
  FrameInput shot = FiredSoFar(0);
  shot.shots = {{.shooter = kLocal, .tick = kShotTick, .origin = Vec3(0.0F, 1.5F, 0.0F), .yaw = 0.0F, .pitch = 0.0F},
                {.shooter = kOther, .tick = kShotTick, .origin = Vec3(9.0F, 1.5F, 0.0F), .yaw = 0.0F, .pitch = 0.0F}};
  overlays.Advance(shot, State{}, kNow);
  return overlays;
}

TEST(ShotJudgementsTest, TheRoundsFlightIsFlownFromItsShotToThePointStruck) {
  PhysicsOverlays overlays = FlyingOwnShot();

  ShotJudgement judgement = JudgedPast(Vec3(0.0F, 0.0F, -3.5F), 0.0F);
  judgement.struck = Struck::kMap;
  judgement.point = Vec3(0.0F, 1.5F, -3.5F);
  overlays.Judge(judgement, NoMap(), kNow + milliseconds{100});

  // Down -Z, where a view of yaw 0 looks, a metre a tick, to the point struck.
  const std::vector<Segment> flight = SegmentsIn(overlays.BuildShapes(FiredSoFar(0), State{}), kFlightColor);
  const std::vector<Vec3> points{Vec3(0.0F, 1.5F, 0.0F), Vec3(0.0F, 1.5F, -1.0F), Vec3(0.0F, 1.5F, -2.0F),
                                 Vec3(0.0F, 1.5F, -3.0F), Vec3(0.0F, 1.5F, -3.5F)};
  ASSERT_EQ(flight.size(), points.size() - 1);
  for (std::size_t i = 0; i < flight.size(); ++i) {
    ExpectNear(flight[i].from, points[i]);
    ExpectNear(flight[i].to, points[i + 1]);
  }
}

TEST(ShotJudgementsTest, NoFlightIsDrawnWithoutItsShot) {
  // The control: with its Shot announced, the same judgement draws one.
  PhysicsOverlays control = FlyingOwnShot();
  control.Judge(StruckHead(Vec3(2.0F, 0.0F, 0.0F), Vec3(2.1F, 1.7F, 0.0F)), NoMap(), kNow);
  ASSERT_FALSE(SegmentsIn(control.BuildShapes(FiredSoFar(0), State{}), kFlightColor).empty());
  PhysicsOverlays overlays = ShotJudgementsOn();
  overlays.SetParameters(LevelFlight(), 1.0F / 60.0F);

  overlays.Judge(StruckHead(Vec3(2.0F, 0.0F, 0.0F), Vec3(2.1F, 1.7F, 0.0F)), NoMap(), kNow);

  EXPECT_TRUE(SegmentsIn(overlays.BuildShapes(FiredSoFar(0), State{}), kFlightColor).empty());
}

// A wall across the flight of FlyingOwnShot's round, facing it, 3.5 m away.
physics::World WallAhead() {
  physics::World map{physics::StaminaConfig{}};
  const physics::CollisionMesh wall{
      .points = {Vec3(-5.0F, 0.0F, -3.5F), Vec3(5.0F, 0.0F, -3.5F), Vec3(5.0F, 5.0F, -3.5F), Vec3(-5.0F, 5.0F, -3.5F)},
      .indices = {0, 1, 2, 0, 2, 3}};
  EXPECT_TRUE(map.AddCollisionMesh(wall, physics::SurfaceId{1}).has_value());
  return map;
}

// The flight a judgement that struck nothing is drawn with, flown against map.
std::vector<Segment> MissFlownAgainst(const physics::World& map) {
  PhysicsOverlays overlays = FlyingOwnShot();
  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 0.0F), map, kNow + milliseconds{100});
  return SegmentsIn(overlays.BuildShapes(FiredSoFar(0), State{}), kFlightColor);
}

// The Map a tracer meets, as Tracers fly it: a miss is not drawn through walls.
TEST(ShotJudgementsTest, AMissIsFlownOnlyAsFarAsTheMap) {
  // The control: with no Map, it flies on past where the wall would be.
  const std::vector<Segment> open = MissFlownAgainst(NoMap());
  ASSERT_FALSE(open.empty());
  ASSERT_LT(open.back().to.z, -10.0F);
  const physics::World wall = WallAhead();

  const std::vector<Segment> flight = MissFlownAgainst(wall);

  ASSERT_FALSE(flight.empty());
  ExpectNear(flight.back().to, Vec3(0.0F, 1.5F, -3.5F));
}

TEST(ShotJudgementsTest, AtMostTheNewest8RoundsAreKept) {
  PhysicsOverlays overlays = ShotJudgementsOn();

  for (int i = 0; i < 9; ++i) {
    overlays.Judge(JudgedPast(Vec3(static_cast<float>(i), 0.0F, 0.0F), 0.0F), NoMap(), kNow + milliseconds{i});
  }

  const std::vector<Segment> judged = SegmentsIn(overlays.BuildShapes(FiredSoFar(0), State{}), kJudgedHitboxColor);
  ASSERT_EQ(judged.size(), 8U * 6U);
  // The first, at x 0, went to keep the ninth.
  ExpectNear(judged.front().from, SoldierAt(Vec3(1.0F, 0.0F, 0.0F))[0].triangles[0].a);
}

TEST(ShotJudgementsTest, EachRoundIsGoneAfter5Seconds) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  overlays.Judge(JudgedPast(Vec3(1.0F, 0.0F, 0.0F), 0.0F), NoMap(), kNow);
  overlays.Judge(JudgedPast(Vec3(2.0F, 0.0F, 0.0F), 0.0F), NoMap(), kNow + seconds{1});

  overlays.Advance(FiredSoFar(0), State{}, kNow + milliseconds{4999});
  EXPECT_EQ(SegmentsIn(overlays.BuildShapes(FiredSoFar(0), State{}), kJudgedHitboxColor).size(), 12U);

  overlays.Advance(FiredSoFar(0), State{}, kNow + seconds{5});
  ExpectOutlinesOf(SegmentsIn(overlays.BuildShapes(FiredSoFar(0), State{}), kJudgedHitboxColor),
                   SoldierAt(Vec3(2.0F, 0.0F, 0.0F)));
}

TEST(ShotJudgementsTest, ASeenRecordNeverPairedIsDroppedOnceItExpires) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow);

  overlays.Advance(FiredSoFar(1), State{}, kNow + kSeenHitboxesWait);
  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.0F), NoMap(), kNow + kSeenHitboxesWait);

  EXPECT_TRUE(SegmentsIn(overlays.BuildShapes(FiredSoFar(1), State{}), kSeenHitboxColor).empty());
}

TEST(ShotJudgementsTest, IsDrawnThroughWallsByDefault) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  ASSERT_TRUE(overlays.ThroughWalls(PhysicsOverlay::kShotJudgements));
  overlays.Judge(StruckHead(Vec3(2.0F, 0.0F, 0.0F), Vec3(2.1F, 1.7F, 0.0F)), NoMap(), kNow);

  const Shapes shown = overlays.BuildShapes(FiredSoFar(0), State{});
  ASSERT_FALSE(shown.segments.empty());
  EXPECT_TRUE(shown.segments.front().through_walls);
  EXPECT_TRUE(shown.triangles.front().through_walls);
  EXPECT_TRUE(shown.spheres.front().through_walls);

  overlays.Update(PanelEvent("shot_judgements.through_walls", false));
  const Shapes hidden = overlays.BuildShapes(FiredSoFar(0), State{});
  EXPECT_FALSE(hidden.segments.front().through_walls);
  EXPECT_FALSE(hidden.triangles.front().through_walls);
  EXPECT_FALSE(hidden.spheres.front().through_walls);
}

TEST(ShotJudgementsTest, AJudgedRoundIsGoneOnceTheOverlayIsSwitchedOffAndOnAgain) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  overlays.Judge(JudgedPast(Vec3(2.0F, 0.0F, 0.0F), 0.0F), NoMap(), kNow);
  // The control: within its 5 seconds, it is drawn.
  ASSERT_NE(overlays.BuildShapes(FiredSoFar(0), State{}), Shapes{});

  overlays.Update(PanelEvent("shot_judgements", false));
  overlays.Advance(FiredSoFar(0), State{}, kNow + seconds{1});
  overlays.Update(PanelEvent("shot_judgements", true));
  overlays.Advance(FiredSoFar(0), State{}, kNow + seconds{2});

  EXPECT_EQ(overlays.BuildShapes(FiredSoFar(0), State{}), Shapes{});
  EXPECT_TRUE(Shows(overlays.BuildPanel(), "Shot judgements kept: 0"));
}

TEST(ShotJudgementsTest, NothingIsDrawnWhileTheOverlayIsOff) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  overlays.Judge(JudgedPast(Vec3(2.0F, 0.0F, 0.0F), 0.0F), NoMap(), kNow);
  ASSERT_NE(overlays.BuildShapes(FiredSoFar(0), State{}), Shapes{});

  overlays.Update(PanelEvent("shot_judgements", false));

  EXPECT_EQ(overlays.BuildShapes(FiredSoFar(0), State{}), Shapes{});
}

TEST(ShotJudgementsTest, TheNoJudgementsLineShows2SecondsAfterAnUnansweredRound) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow);

  overlays.Advance(FiredSoFar(1), State{}, kNow + milliseconds{1999});
  EXPECT_FALSE(Shows(overlays.BuildPanel(), kNoShotJudgementsLine));

  overlays.Advance(FiredSoFar(1), State{}, kNow + seconds{2});
  EXPECT_TRUE(Shows(overlays.BuildPanel(), kNoShotJudgementsLine));
}

TEST(ShotJudgementsTest, TheNoJudgementsLineClearsOnTheNextJudgement) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow);
  overlays.Advance(FiredSoFar(1), State{}, kNow + seconds{3});
  ASSERT_TRUE(Shows(overlays.BuildPanel(), kNoShotJudgementsLine));

  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.0F), NoMap(), kNow + seconds{3});
  overlays.Advance(FiredSoFar(1), State{}, kNow + seconds{4});

  EXPECT_FALSE(Shows(overlays.BuildPanel(), kNoShotJudgementsLine));
}

// Each judgement answers the oldest round unanswered: one fired after it is
// still timed.
TEST(ShotJudgementsTest, TheNoJudgementsLineShows2SecondsAfterARoundFiredAfterTheJudgedOne) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow);
  FireOneRound(overlays, SeenAt(Vec3(1.0F, 0.0F, 0.0F), 99), kNow + milliseconds{100}, 1);
  overlays.Judge(JudgedPast(Vec3(9.0F, 0.0F, 0.0F), 1.0F), NoMap(), kNow + milliseconds{200});

  overlays.Advance(FiredSoFar(2), State{}, kNow + milliseconds{2099});
  EXPECT_FALSE(Shows(overlays.BuildPanel(), kNoShotJudgementsLine));

  overlays.Advance(FiredSoFar(2), State{}, kNow + milliseconds{2100});
  EXPECT_TRUE(Shows(overlays.BuildPanel(), kNoShotJudgementsLine));
}

TEST(ShotJudgementsTest, ThePanelShowsTheRoundsKeptAndWhenAJudgementLeftPosesOut) {
  PhysicsOverlays overlays = ShotJudgementsOn();
  overlays.Judge(JudgedPast(Vec3(1.0F, 0.0F, 0.0F), 0.0F), NoMap(), kNow);
  const Panel whole = overlays.BuildPanel();
  EXPECT_TRUE(Shows(whole, "Shot judgements kept: 1"));
  EXPECT_FALSE(Shows(whole, "A Shot judgement left Hitbox poses out"));

  ShotJudgement partial = JudgedPast(Vec3(2.0F, 0.0F, 0.0F), 0.0F);
  partial.entries_left_out = true;
  overlays.Judge(partial, NoMap(), kNow);

  const Panel after = overlays.BuildPanel();
  EXPECT_TRUE(Shows(after, "Shot judgements kept: 2"));
  EXPECT_TRUE(Shows(after, "A Shot judgement left Hitbox poses out"));
}

TEST(ShotJudgementsTest, TurningTheOverlayOnAsksForShotJudgementsAndOffStopsThem) {
  PhysicsOverlays overlays(PhysicsOverlaysStart{});
  EXPECT_FALSE(overlays.WantsShotJudgements());

  overlays.Update(PanelEvent("shot_judgements", true));
  EXPECT_TRUE(overlays.WantsShotJudgements());
  EXPECT_TRUE(ToggleOf(overlays.BuildPanel(), "shot_judgements")->on);

  overlays.Update(PanelEvent("shot_judgements", false));
  EXPECT_FALSE(overlays.WantsShotJudgements());
}

TEST(ShotJudgementsTest, StartsOnAndAsksAsItsConfigKeySays) {
  EXPECT_TRUE(PhysicsOverlays(PhysicsOverlaysStart{.shot_judgements = true}).WantsShotJudgements());
}

}  // namespace
}  // namespace augusta::presentation
