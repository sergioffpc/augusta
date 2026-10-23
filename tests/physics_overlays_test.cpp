#include "augusta/physics_overlays.h"

#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
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
#include "augusta/math.h"
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

TEST(HitboxesTest, TheHitboxesAreTurnedAndLoweredAsTheServerPlacesThem) {
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

}  // namespace
}  // namespace augusta::presentation
