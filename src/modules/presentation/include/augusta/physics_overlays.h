#ifndef AUGUSTA_PHYSICS_OVERLAYS_H_
#define AUGUSTA_PHYSICS_OVERLAYS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "augusta/body_part.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"

/// \file
/// The Physics Debug overlays (ADR-0075): which of them are on, the Physics
/// Debug panel that switches them, and each frame's shapes. Physics hands plain
/// values (a body's capsule, physics::CapsuleOf; the client World's collision
/// lines in the Prediction State) and this module builds their shapes, so
/// neither physics nor the server ever names augusta_debug. Collision draws
/// PhysX's own visualization of the client's World in PhysX's colours, inside a
/// culling box centred on the camera, and gives the visualization settings
/// that make the Prediction thread produce it (Visualization). Server bodies
/// draws every body as the newest Authoritative State update reported
/// it, not interpolated, the local player's beside its predicted body.
/// Hitboxes outlines every other player's Hitboxes where the frame shows that
/// player, placed by gameplay's hitbox::Place, the function the server's Lag
/// compensation tests a round against, so what is drawn is the server's Hitbox
/// by construction (ADR-0044).
///
/// Pure: the client Runtime, on the Main/Render thread, builds the panel while
/// the Debug index has it on, routes the panel's control events back here, and
/// hands the Renderer the frame's shapes whether the panel is on or not, so an
/// overlay stays drawn once the panel is closed, and hands the Runner the
/// frame's visualization settings. It decides nothing of which
/// client modes carry the panel. Compiled only in a Development build
/// (ADR-0074).
namespace augusta::presentation {

/// The Physics panel's Panel id, which its ControlEvents name and the Debug
/// index's entry for it is named by.
inline constexpr std::string_view kPhysicsPanelId = "physics";

/// A body as the server last reported it.
inline constexpr math::Vec3 kServerBodyColor{1.0F, 0.55F, 0.1F};
/// The local player's body as it is predicted on the newest tick.
inline constexpr math::Vec3 kPredictedBodyColor{0.2F, 0.85F, 1.0F};

/// The Hitboxes overlay's outline of a head Hitbox.
inline constexpr math::Vec3 kHeadHitboxColor{1.0F, 0.2F, 0.2F};
/// Of a torso Hitbox.
inline constexpr math::Vec3 kTorsoHitboxColor{1.0F, 0.9F, 0.2F};
/// Of a limb Hitbox.
inline constexpr math::Vec3 kLimbHitboxColor{0.3F, 1.0F, 0.4F};

/// What colour the Hitboxes overlay outlines a Hitbox of part in: one per Body
/// part, so a head reads apart from a limb.
[[nodiscard]] constexpr math::Vec3 HitboxColorOf(body_part::BodyPart part) {
  switch (part) {
    case body_part::BodyPart::kHead:
      return kHeadHitboxColor;
    case body_part::BodyPart::kTorso:
      return kTorsoHitboxColor;
    case body_part::BodyPart::kLimb:
      return kLimbHitboxColor;
  }
  return kTorsoHitboxColor;
}

/// One overlay the Physics panel switches, each with a toggle and a
/// through-walls toggle of its own.
enum class PhysicsOverlay : std::uint8_t {
  kCollision,
  kServerBodies,
  kHitboxes,
};

inline constexpr std::size_t kPhysicsOverlayCount = 3;

/// The Collision overlay's culling radius, the half-extent of the box around
/// the camera it draws in, in metres: where it starts, and the slider's range.
inline constexpr float kCollisionRadiusDefault = 20.0F;
inline constexpr float kCollisionRadiusMin = 1.0F;
inline constexpr float kCollisionRadiusMax = 100.0F;

/// Which overlays start on, as the `debug.physics` config keys have them; none
/// of their through-walls switches has a key.
struct PhysicsOverlaysStart {
  bool collision = false;
  bool server_bodies = false;
  bool hitboxes = false;
};

class PhysicsOverlays {
 public:
  explicit PhysicsOverlays(const PhysicsOverlaysStart& start);

  /// The Physics panel: per overlay, in PhysicsOverlay's order, its toggle
  /// (control id the overlay's, e.g. `server_bodies`) then its through-walls
  /// toggle (`server_bodies.through_walls`); then Collision's flags (toggles
  /// `collision.shapes`, `collision.bounds`, `collision.contacts`,
  /// `collision.joints`, `collision.controller_volumes`) and its culling
  /// radius (slider `collision.radius`, kCollisionRadiusMin to
  /// kCollisionRadiusMax metres).
  [[nodiscard]] debug::Panel BuildPanel() const;

  /// Switches what a toggle event of the Physics panel names, or sets the
  /// radius its slider's event gives, kept within its range; any other event,
  /// another panel's included, changes nothing. A switch lasts until the client
  /// exits, never written back to the config.
  void Update(const debug::ControlEvent& event);

  [[nodiscard]] bool IsOn(PhysicsOverlay overlay) const;

  /// Whether overlay's shapes are drawn over what is in front of them.
  [[nodiscard]] bool ThroughWalls(PhysicsOverlay overlay) const;

  /// Takes the Hitboxes of character, by its path (ADR-0042), as the client
  /// loaded them: what a player of that character is outlined with. A
  /// character never given any, or given none, is outlined with nothing.
  void SetCharacterHitboxes(const std::string& character, std::vector<hitbox::CharacterHitbox> hitboxes);

  /// What the client World's visualization draws for this state: off while
  /// Collision is, so a closed overlay costs nothing; on, its flags and the
  /// culling box of its radius centred on camera.
  [[nodiscard]] physics::Visualization Visualization(const math::Vec3& camera) const;

  /// The frame's shapes of every overlay on, from what the frame is shown from
  /// (input) and what PresentationWorld showed of it (state).
  /// Collision: a segment per line of the newest Prediction State
  /// (input.ticks.latest.collision_lines) in its PhysX colour, opaque.
  /// Server bodies: a wireframe capsule per body of the newest update
  /// (input.updates' last), and the local player's predicted body on the newest
  /// tick (input.ticks.latest) while that update lists its body; nothing
  /// outside a match. Hitboxes: every edge of every Hitbox of every other player
  /// at its interpolated pose (state.remote_players), as a segment in its Body
  /// part's colour (HitboxColorOf); a spectator's alike.
  [[nodiscard]] debug::Shapes BuildShapes(const FrameInput& input, const State& state) const;

 private:
  struct Switches {
    bool on = false;
    bool through_walls = false;
  };

  std::array<Switches, kPhysicsOverlayCount> switches_{};
  /// By character path.
  std::unordered_map<std::string, std::vector<hitbox::CharacterHitbox>> hitboxes_;
  // Collision's flags and culling radius; its on and cull_center are
  // Visualization's to fill.
  physics::Visualization collision_{.cull_half_extent = kCollisionRadiusDefault};
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PHYSICS_OVERLAYS_H_
