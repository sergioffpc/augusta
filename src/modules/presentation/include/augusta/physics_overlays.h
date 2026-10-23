#ifndef AUGUSTA_PHYSICS_OVERLAYS_H_
#define AUGUSTA_PHYSICS_OVERLAYS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
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

/// One overlay the Physics panel switches, each with a toggle and a
/// through-walls toggle of its own.
enum class PhysicsOverlay : std::uint8_t {
  kCollision,
  kServerBodies,
};

inline constexpr std::size_t kPhysicsOverlayCount = 2;

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

  /// What the client World's visualization draws for this state: off while
  /// Collision is, so a closed overlay costs nothing; on, its flags and the
  /// culling box of its radius centred on camera.
  [[nodiscard]] physics::Visualization Visualization(const math::Vec3& camera) const;

  /// The frame's shapes of every overlay on, from what the frame is shown from.
  /// Collision: a segment per line of the newest Prediction State
  /// (input.ticks.latest.collision_lines) in its PhysX colour, opaque.
  /// Server bodies: a wireframe capsule per body of the newest update
  /// (input.updates' last), and the local player's predicted body on the newest
  /// tick (input.ticks.latest) while that update lists its body; nothing
  /// outside a match.
  [[nodiscard]] debug::Shapes BuildShapes(const FrameInput& input) const;

 private:
  struct Switches {
    bool on = false;
    bool through_walls = false;
  };

  std::array<Switches, kPhysicsOverlayCount> switches_{};
  // Collision's flags and culling radius; its on and cull_center are
  // Visualization's to fill.
  physics::Visualization collision_{.cull_half_extent = kCollisionRadiusDefault};
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PHYSICS_OVERLAYS_H_
