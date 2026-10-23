#ifndef AUGUSTA_PHYSICS_OVERLAYS_H_
#define AUGUSTA_PHYSICS_OVERLAYS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/body_part.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/judged_rounds.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/shown_hitboxes.h"

/// \file
/// The Physics Debug overlays (ADR-0075): which of them are on, the Physics
/// Debug panel that switches them, and each frame's shapes, built from the
/// plain values physics and gameplay hand over, so neither physics nor the
/// server ever names augusta_debug. Shot judgements' rounds are JudgedRounds'
/// (judged_rounds.h), which this owns and switches.
///
/// Pure: the client Runtime, on the Main/Render thread, builds the panel while
/// the Debug index has it on, routes the panel's control events back here, and
/// hands the Renderer the frame's shapes whether the panel is on or not, so an
/// overlay stays drawn once the panel is closed, and hands the Runner the
/// frame's visualization settings. It decides nothing of which client modes
/// carry the panel. Compiled only in a Development build (ADR-0074).
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
  kShotJudgements,
};

inline constexpr std::size_t kPhysicsOverlayCount = 4;

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
  bool shot_judgements = false;

  bool operator==(const PhysicsOverlaysStart&) const = default;
};

class PhysicsOverlays {
 public:
  using Clock = JudgedRounds::Clock;

  explicit PhysicsOverlays(const PhysicsOverlaysStart& start);

  /// The Physics panel: per overlay, in PhysicsOverlay's order, its toggle
  /// (control id the overlay's, e.g. `server_bodies`) then its through-walls
  /// toggle (`server_bodies.through_walls`); then Collision's flags (toggles
  /// `collision.shapes`, `collision.bounds`, `collision.contacts`,
  /// `collision.joints`, `collision.controller_volumes`) and its culling
  /// radius (slider `collision.radius`, kCollisionRadiusMin to
  /// kCollisionRadiusMax metres); then Shot judgements' status
  /// (JudgedRounds::AddStatus).
  [[nodiscard]] debug::Panel BuildPanel() const;

  /// Switches what a toggle event of the Physics panel names, or sets the
  /// radius its slider's event gives, kept within its range; any other event,
  /// another panel's included, changes nothing. A switch lasts until the client
  /// exits, never written back to the config.
  void Update(const debug::ControlEvent& event);

  [[nodiscard]] bool IsOn(PhysicsOverlay overlay) const;

  /// Whether overlay's shapes are drawn over what is in front of them.
  [[nodiscard]] bool ThroughWalls(PhysicsOverlay overlay) const;

  /// Whether the client's Debug subscribe should ask for Shot judgements (bit
  /// 0): while their overlay is on. The client Runtime sends it.
  [[nodiscard]] bool WantsShotJudgements() const;

  /// What Shot judgements flies a round's flight by, as
  /// JudgedRounds::SetParameters takes it.
  void SetParameters(const parameters::Parameters& parameters, float tick_duration);

  /// Takes the Hitboxes of character, by its path (ADR-0042), as the client
  /// loaded them: what a player of that character is outlined with. A
  /// character never given any, or given none, is outlined with nothing.
  void SetCharacterHitboxes(const std::string& character, std::vector<hitbox::CharacterHitbox> hitboxes);

  /// What the client World's visualization draws for this state: off while
  /// Collision is, so a closed overlay costs nothing; on, its flags and the
  /// culling box of its radius centred on camera.
  [[nodiscard]] physics::Visualization Visualization(const math::Vec3& camera) const;

  /// Moves Shot judgements on to the frame shown from input, which state shows,
  /// at now (JudgedRounds::Advance while the overlay is on, Skip while it is
  /// off, so the rounds fired are counted either way and a round kept never
  /// outlives the overlay). Called once a frame, before Judge and BuildShapes.
  void Advance(const FrameInput& input, const State& state, Clock::time_point now);

  /// Draws judgement, received at now, as JudgedRounds::Judge does, its
  /// flight flown against map, the Map Tracers meet (World::Map). Ignored
  /// while the overlay is off.
  void Judge(const ShotJudgement& judgement, const physics::World& map, Clock::time_point now);

  /// The frame's shapes of every overlay on, from what the frame is shown from
  /// (input) and what PresentationWorld showed of it (state).
  /// Collision: a segment per line of the newest Prediction State
  /// (input.ticks.latest.collision_lines) in its PhysX colour, opaque.
  /// Server bodies: a wireframe capsule per body of the newest update
  /// (input.updates' last), and the local player's predicted body on the newest
  /// tick (input.ticks.latest) while that update lists its body; nothing
  /// outside a match. Hitboxes: every edge of every Hitbox of every other player
  /// at its interpolated pose (PlaceShownPlayers), as a segment in its Body
  /// part's colour (HitboxColorOf); a spectator's alike. Shot judgements: the
  /// judged rounds still kept.
  [[nodiscard]] debug::Shapes BuildShapes(const FrameInput& input, const State& state) const;

 private:
  struct Switches {
    bool on = false;
    bool through_walls = false;
  };

  std::array<Switches, kPhysicsOverlayCount> switches_{};
  CharacterHitboxes hitboxes_;
  // Collision's flags and culling radius; its on and cull_center are
  // Visualization's to fill.
  physics::Visualization collision_{.cull_half_extent = kCollisionRadiusDefault};
  JudgedRounds judged_rounds_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PHYSICS_OVERLAYS_H_
