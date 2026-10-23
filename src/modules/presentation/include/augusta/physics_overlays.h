#ifndef AUGUSTA_PHYSICS_OVERLAYS_H_
#define AUGUSTA_PHYSICS_OVERLAYS_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/debug_kept_shapes.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/effects.h"
#include "augusta/hitbox.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/tick.h"
#include "augusta/tracers.h"

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
/// by construction (ADR-0044). Shot judgements sets, for each of the local
/// player's own rounds, the Hitboxes the client showed when it fired (seen)
/// beside those the server judged it against (judged), with the round's flight
/// and the point struck: the gap between them is the Lag compensation error.
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

/// The Shot judgements overlay's outline of the Hitboxes the client showed
/// when the local player fired a round.
inline constexpr math::Vec3 kSeenHitboxColor{0.1F, 0.9F, 0.1F};
/// Of the Hitboxes the server judged it against, and the struck one's fill.
inline constexpr math::Vec3 kJudgedHitboxColor{1.0F, 0.1F, 0.1F};
/// The struck Hitbox's fill is this opaque.
inline constexpr float kStruckHitboxAlpha = 0.5F;
/// A round's flight.
inline constexpr math::Vec3 kFlightColor{1.0F, 1.0F, 1.0F};
/// The point a round struck, a filled sphere of kStruckPointRadius metres.
inline constexpr math::Vec3 kStruckPointColor{1.0F, 0.85F, 0.1F};
inline constexpr float kStruckPointRadius = 0.05F;

/// How many judged rounds Shot judgements keeps drawn at most, and how long
/// each.
inline constexpr std::size_t kShotJudgementsKept = 8;
inline constexpr std::chrono::seconds kShotJudgementSeconds{5};
/// How long a round's seen Hitboxes wait for its judgement: longer than any
/// round flies (the server judges it once its flight ends) by a second for the
/// judgement's way back.
inline constexpr std::chrono::seconds kSeenHitboxesWait = ballistics::kMaxFlightTime + std::chrono::seconds{1};
/// How long after a round fired with Shot judgements on, unanswered, the panel
/// says none has come.
inline constexpr std::chrono::seconds kNoShotJudgementsAfter{2};
inline constexpr std::string_view kNoShotJudgementsLine =
    "no Shot judgements received: the server sends them only with `debug.shot_judgements` on";

/// What ended a round's flight, as its Shot judgement tells it.
enum class Struck : std::uint8_t {
  /// Nothing: it flew past its range or its flight time.
  kNothing,
  kMap,
  kProp,
  kHitbox,
};

/// One player a round was judged against on one tick of its flight: where the
/// server's Hitbox history posed it.
struct JudgedPlayer {
  EntityId entity{};
  hitbox::Pose pose{};
};

/// One tick of a round's flight whose segment crossed some player's bounds.
struct ShotJudgementEntry {
  std::vector<JudgedPlayer> players;
};

/// How the server judged one round the local player fired (GLOSSARY.md's Shot
/// judgement), in presentation's own terms: the client Runtime converts the
/// Session's at its edge.
struct ShotJudgement {
  /// The server tick its Shot was fired on.
  tick::Tick shot_tick = 0;
  /// Its Shooter's delay (ADR-0044), in ticks: the Seen time it was judged at
  /// is shot_tick less this.
  float shooters_delay = 0.0F;
  Struck struck = Struck::kNothing;
  /// The Body part struck; meaningful only when struck is kHitbox.
  body_part::BodyPart part = body_part::BodyPart::kTorso;
  /// Where it struck; meaningful unless struck is kNothing.
  math::Vec3 point{};
  /// The body or Prop it struck, if any.
  std::optional<EntityId> target;
  /// The ticks of its flight whose segment crossed some player's bounds,
  /// oldest first.
  std::vector<ShotJudgementEntry> entries;
  /// Whether the server left some of those ticks out.
  bool entries_left_out = false;
};

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
};

class PhysicsOverlays {
 public:
  using Clock = debug::KeptShapes::Clock;

  explicit PhysicsOverlays(const PhysicsOverlaysStart& start);

  /// The Physics panel: per overlay, in PhysicsOverlay's order, its toggle
  /// (control id the overlay's, e.g. `server_bodies`) then its through-walls
  /// toggle (`server_bodies.through_walls`); then Collision's flags (toggles
  /// `collision.shapes`, `collision.bounds`, `collision.contacts`,
  /// `collision.joints`, `collision.controller_volumes`) and its culling
  /// radius (slider `collision.radius`, kCollisionRadiusMin to
  /// kCollisionRadiusMax metres); then Shot judgements' status: the rounds it
  /// keeps, a line once some judgement left Hitbox poses out, and
  /// kNoShotJudgementsLine while a round has waited kNoShotJudgementsAfter
  /// unanswered.
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

  /// Takes the parameters the server admitted the client with and its tick
  /// duration, in seconds: what Shot judgements flies a round's flight by, as
  /// Tracers fly it. Until then no flight is drawn.
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
  /// at now: for each round the local player's rifle fired since the last call
  /// (FiredRounds) with the overlay on, keeps every other player's Hitboxes
  /// where state shows them (the seen Hitboxes), by state's Seen time, until
  /// a judgement takes them or kSeenHitboxesWait passes; keeps the local
  /// player's own Shots for their judgements' flights; and drops the judged
  /// rounds whose time has come. Called once a frame, before Judge and
  /// BuildShapes; whatever the overlay, so the rounds fired are counted.
  void Advance(const FrameInput& input, const State& state, Clock::time_point now);

  /// Draws judgement, received at now, for kShotJudgementSeconds, among the
  /// newest kShotJudgementsKept: the seen Hitboxes whose Seen time is nearest
  /// its own, within a tick (none if there are none); the judged ones, placed
  /// from its Hitbox poses with gameplay's placing, the struck one filled; its
  /// flight from its Shot to the point struck, flown as Tracers fly it; and the
  /// point struck. Ignored while the overlay is off.
  void Judge(const ShotJudgement& judgement, Clock::time_point now);

  /// The frame's shapes of every overlay on, from what the frame is shown from
  /// (input) and what PresentationWorld showed of it (state).
  /// Collision: a segment per line of the newest Prediction State
  /// (input.ticks.latest.collision_lines) in its PhysX colour, opaque.
  /// Server bodies: a wireframe capsule per body of the newest update
  /// (input.updates' last), and the local player's predicted body on the newest
  /// tick (input.ticks.latest) while that update lists its body; nothing
  /// outside a match. Hitboxes: every edge of every Hitbox of every other player
  /// at its interpolated pose (state.remote_players), as a segment in its Body
  /// part's colour (HitboxColorOf); a spectator's alike. Shot judgements: the
  /// judged rounds still kept.
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

  // Every Hitbox of every other player where state shows them, placed as the
  // server places them.
  [[nodiscard]] std::vector<hitbox::PlacedHitbox> PlaceOtherPlayers(const State& state) const;

  // Shot judgements.
  // One round's seen Hitboxes, outlined.
  struct SeenRecord {
    // Its frame's Seen time, in ticks.
    double seen_time = 0.0;
    Clock::time_point fired;
    debug::Shapes outlines;
  };
  // One of the local player's own Shots.
  struct OwnShot {
    tick::Tick tick = 0;
    math::Vec3 origin{};
    math::Vec3 direction{};
    Clock::time_point received;
  };
  // judgement's judged Hitboxes, outlined, the struck one filled, into round.
  void AddJudgedHitboxes(const ShotJudgement& judgement, debug::Shapes& round) const;
  // The seen Hitboxes whose Seen time is nearest seen_time, within a tick,
  // taken from those kept; nullopt if none is.
  std::optional<debug::Shapes> TakeSeen(double seen_time);
  // The flight of the Shot fired on shot_tick, from its origin: ending at
  // point once it passes it, if given. Empty without that Shot or the
  // parameters.
  std::vector<math::Vec3> FlightOf(tick::Tick shot_tick, const std::optional<math::Vec3>& point);

  FiredRounds fired_rounds_;
  std::deque<SeenRecord> seen_;
  std::deque<OwnShot> own_shots_;
  // By entity, as the match named them.
  std::unordered_map<EntityId, std::string> characters_;
  std::optional<TracerRules> tracer_rules_;
  // The Map a flight is flown against: none, so it flies on to the point
  // struck. Made for the first flight, so never before a PhysX recording
  // starts.
  std::optional<physics::World> no_map_;
  debug::KeptShapes judged_{kShotJudgementsKept};
  debug::Shapes judged_alive_;
  std::optional<Clock::time_point> unanswered_since_;
  bool unanswered_too_long_ = false;
  bool poses_left_out_ = false;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PHYSICS_OVERLAYS_H_
