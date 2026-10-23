#include "augusta/physics_overlays.h"

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"

namespace augusta::presentation {

namespace {

// What the Physics panel shows of one overlay, and how it starts.
struct OverlayControls {
  // The overlay's toggle's control id; its through-walls toggle's is this with
  // kThroughWallsSuffix.
  std::string_view id;
  std::string_view label;
  bool through_walls_by_default = false;
};

// In PhysicsOverlay's order.
constexpr std::array<OverlayControls, kPhysicsOverlayCount> kOverlays{{
    {.id = "server_bodies", .label = "Server bodies", .through_walls_by_default = false},
    {.id = "hitboxes", .label = "Hitboxes", .through_walls_by_default = false},
}};

constexpr std::string_view kThroughWallsSuffix = ".through_walls";

constexpr std::size_t IndexOf(PhysicsOverlay overlay) { return static_cast<std::size_t>(overlay); }

debug::Capsule WireframeCapsule(const physics::BodyState& body, const math::Vec3& color, bool through_walls) {
  const physics::Capsule capsule = physics::CapsuleOf(body);
  return debug::Capsule{.from = capsule.bottom,
                        .to = capsule.top,
                        .radius = capsule.radius,
                        .color = color,
                        .alpha = 1.0F,
                        .style = debug::Style::kWireframe,
                        .through_walls = through_walls};
}

// Server bodies: the newest update's bodies, and the local player's predicted
// body beside its own.
void AddServerBodies(const FrameInput& input, bool through_walls, debug::Shapes& shapes) {
  if (input.updates.empty()) {
    return;
  }
  bool lists_local = false;
  for (const DynamicBody& body : input.updates.back().bodies) {
    shapes.capsules.push_back(WireframeCapsule(body.state, kServerBodyColor, through_walls));
    lists_local = lists_local || body.entity == input.local_entity;
  }
  if (lists_local) {
    shapes.capsules.push_back(WireframeCapsule(input.ticks.latest.local_body, kPredictedBodyColor, through_walls));
  }
}

// Where body's Hitboxes stand.
hitbox::Pose PoseOf(const RemoteBody& body) {
  return hitbox::Pose{.position = body.position, .yaw = body.yaw, .stance = body.stance};
}

// Each of placed's triangles outlined: its three edges in its Body part's colour.
void AddOutlines(const std::vector<hitbox::PlacedHitbox>& placed, bool through_walls, debug::Shapes& shapes) {
  for (const hitbox::PlacedHitbox& hitbox : placed) {
    const math::Vec3 color = HitboxColorOf(hitbox.part);
    for (const ballistics::Triangle& triangle : hitbox.triangles) {
      for (const auto& [from, to] :
           {std::pair{triangle.a, triangle.b}, std::pair{triangle.b, triangle.c}, std::pair{triangle.c, triangle.a}}) {
        shapes.segments.push_back(
            debug::Segment{.from = from, .to = to, .color = color, .alpha = 1.0F, .through_walls = through_walls});
      }
    }
  }
}

}  // namespace

PhysicsOverlays::PhysicsOverlays(const PhysicsOverlaysStart& start) {
  for (std::size_t i = 0; i < kPhysicsOverlayCount; ++i) {
    switches_.at(i).through_walls = kOverlays.at(i).through_walls_by_default;
  }
  switches_.at(IndexOf(PhysicsOverlay::kServerBodies)).on = start.server_bodies;
  switches_.at(IndexOf(PhysicsOverlay::kHitboxes)).on = start.hitboxes;
}

debug::Panel PhysicsOverlays::BuildPanel() const {
  debug::Panel panel{.id = std::string(kPhysicsPanelId), .title = "Physics", .elements = {}};
  for (std::size_t i = 0; i < kPhysicsOverlayCount; ++i) {
    const OverlayControls& overlay = kOverlays.at(i);
    panel.elements.emplace_back(
        debug::Toggle{.id = std::string(overlay.id), .label = std::string(overlay.label), .on = switches_.at(i).on});
    panel.elements.emplace_back(debug::Toggle{.id = std::format("{}{}", overlay.id, kThroughWallsSuffix),
                                              .label = std::format("{} through walls", overlay.label),
                                              .on = switches_.at(i).through_walls});
  }
  return panel;
}

void PhysicsOverlays::Update(const debug::ControlEvent& event) {
  const bool* on = std::get_if<bool>(&event.value);
  if (event.panel != kPhysicsPanelId || on == nullptr) {
    return;
  }
  const std::string_view control = event.control;
  for (std::size_t i = 0; i < kPhysicsOverlayCount; ++i) {
    const std::string_view id = kOverlays.at(i).id;
    if (control == id) {
      switches_.at(i).on = *on;
    } else if (control.starts_with(id) && control.substr(id.size()) == kThroughWallsSuffix) {
      switches_.at(i).through_walls = *on;
    }
  }
}

bool PhysicsOverlays::IsOn(PhysicsOverlay overlay) const { return switches_.at(IndexOf(overlay)).on; }

bool PhysicsOverlays::ThroughWalls(PhysicsOverlay overlay) const {
  return switches_.at(IndexOf(overlay)).through_walls;
}

void PhysicsOverlays::SetCharacterHitboxes(const std::string& character,
                                           std::vector<hitbox::CharacterHitbox> hitboxes) {
  hitboxes_.insert_or_assign(character, std::move(hitboxes));
}

debug::Shapes PhysicsOverlays::BuildShapes(const FrameInput& input, const State& state) const {
  debug::Shapes shapes;
  if (IsOn(PhysicsOverlay::kServerBodies)) {
    AddServerBodies(input, ThroughWalls(PhysicsOverlay::kServerBodies), shapes);
  }
  if (IsOn(PhysicsOverlay::kHitboxes)) {
    for (const RemotePlayer& player : state.remote_players) {
      const auto hitboxes = hitboxes_.find(player.character);
      if (hitboxes != hitboxes_.end()) {
        AddOutlines(hitbox::Place(hitboxes->second, PoseOf(player.body)), ThroughWalls(PhysicsOverlay::kHitboxes),
                    shapes);
      }
    }
  }
  return shapes;
}

}  // namespace augusta::presentation
