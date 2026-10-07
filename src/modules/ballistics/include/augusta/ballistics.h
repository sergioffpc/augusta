#ifndef AUGUSTA_BALLISTICS_H_
#define AUGUSTA_BALLISTICS_H_

#include <chrono>
#include <cstdint>
#include <span>
#include <unordered_map>

#include "augusta/math.h"
#include "augusta/physics.h"

/// \file
/// augusta::ballistics simulates bullet trajectories (gravity-induced
/// drop, travel time - US-10), hand-rolled instead of PhysX's generic
/// projectile handling (ADR-0002: full control over determinism and a
/// core learning goal, not a gap - see that ADR before reaching for an
/// external solver). A semi-implicit Euler integrator is enough for v1;
/// nothing in REQUIREMENTS.md asks for aerodynamic drag/wind modeling.
///
/// The module is shared (ADR-0024, ADR-0044): the server advances every
/// bullet with it and decides what each one hits, and each client's
/// presentation draws every announced Shot's tracer and Map impact with
/// the same World, handing it no hitboxes - a visual only. Deciding
/// which player a bullet hits, where and for what damage stays the
/// server's: only the server has the hitboxes to hand in.
///
/// World::Step follows the same per-handle, called-once-per-tick shape as
/// physics::World::Step, since bullets are ECS entities too
/// (ARCHITECTURE.md §5's Shared Core ECS list) advanced by a system that
/// iterates them the same way player bodies are. Each tick's segment is
/// tested against the Map through physics::World::RaycastMap, which never
/// reports a player's controller (ADR-0002), and against the Hitboxes the
/// caller hands in, already posed where the caller judges the players to
/// be: for the server, as they were the Shooter's delay ago (ADR-0044).
/// Hitboxes are tested here, triangle by triangle, not through PhysX,
/// since they are posed anew for every tick; NextSegment tells the caller
/// which players a bullet passes near, so it poses only those.
namespace augusta::ballistics {

/// Where on a hit player's body a bullet struck (US-11): the coarse zones
/// REQUIREMENTS.md's "e.g., head, torso, limb" asks for.
enum class BodyPart : std::uint8_t {
  kHead,
  kTorso,
  kLimb,
};

/// Tunable per-shot values (ARCHITECTURE.md §8 mechanism/policy/data
/// split, same category as physics::StaminaConfig): the gravity
/// integration and max-range cutoff *logic* is mechanism code in
/// World::Step, identical for every bullet, but these *values* are meant
/// to vary per weapon/ammo type (US-12) and be loaded from external
/// configuration rather than hardcoded.
struct BulletConfig {
  /// Downward acceleration applied each tick, in engine units/s^2.
  float gravity = 0.0F;
  /// The bullet resolves as kExpired (see Outcome) once it has travelled
  /// this far from its origin without hitting anything, or once it has
  /// flown kMaxFlightTime, whichever comes first.
  float max_range = 0.0F;
};

/// The longest any bullet flies (ADR-0002): one still in flight after this
/// long expires, whatever its speed and max_range, so a round that crawls
/// toward a range it never reaches is not simulated forever. Far above a rifle
/// round's flight to any range a Map has room for.
inline constexpr std::chrono::seconds kMaxFlightTime{5};

/// How many World::Step calls of step_seconds each a bullet flies at most:
/// kMaxFlightTime in them, rounded up, so 5 times the rate for a step of 1/rate
/// seconds however that rounds to a float. step_seconds must be above 0.
[[nodiscard]] std::uint32_t MaxFlightSteps(float step_seconds);

/// One triangle of a posed Hitbox, in world space.
struct Triangle {
  math::Vec3 a;
  math::Vec3 b;
  math::Vec3 c;
};

/// The caller's name for the player a Hitbox belongs to: the server hands in
/// its Entity ID, so a hit names the body it struck.
enum class TargetId : std::uint32_t {};

/// One body part of one player, posed in world space for one Step. Views its
/// triangles, which must outlive the Step it is handed to.
struct Hitbox {
  TargetId target{};
  BodyPart part = BodyPart::kTorso;
  std::span<const Triangle> triangles;
};

/// Opaque handle to an in-flight bullet created by World::Fire. Valid
/// only for the World instance that created it, and only until Step
/// returns a resolved (non-kInFlight) result for it - see Step.
enum class BulletHandle : std::uint32_t {};

/// A bullet's position/velocity at a point in time.
struct BulletState {
  math::Vec3 position;
  math::Vec3 velocity;
};

/// The straight line a bullet moves along in one tick.
struct Segment {
  math::Vec3 from;
  math::Vec3 to;
};

/// One World::Step call's outcome for one bullet.
enum class Outcome {
  /// Still travelling; Step must be called again next tick.
  kInFlight,
  /// Struck the Map this tick (see StepResult::impact_point).
  kHitMap,
  /// Struck a player's Hitbox this tick (see StepResult::target/part/impact_point).
  kHitPlayer,
  /// Exceeded BulletConfig::max_range, or flew kMaxFlightTime, without
  /// hitting anything - a miss.
  kExpired,
};

/// The result of one World::Step call.
struct StepResult {
  Outcome outcome = Outcome::kInFlight;
  /// The bullet's position/velocity at the end of the tick's movement,
  /// regardless of outcome: past the impact point on a hit.
  BulletState state;
  /// The player that was hit. Only meaningful if outcome is kHitPlayer.
  TargetId target{};
  /// Which part of target was hit. Only meaningful if outcome is
  /// kHitPlayer.
  BodyPart part = BodyPart::kTorso;
  /// World-space point of impact. Only meaningful if outcome is kHitMap
  /// or kHitPlayer.
  math::Vec3 impact_point;
};

/// Owns every in-flight bullet of one world: the server's SimulationWorld,
/// or a client's presentation drawing the Shots it is told of.
class World {
 public:
  World();

  /// Spawns a new bullet at origin, travelling in direction (need not be
  /// pre-normalized) at initial_speed (engine units/s), obeying config
  /// for the rest of its flight (US-07: "correct origin, direction, and
  /// initial velocity"). config is copied per bullet, not shared with
  /// the World - different ammo types can fire simultaneously with
  /// different gravity/max_range.
  BulletHandle Fire(const math::Vec3& origin, const math::Vec3& direction, float initial_speed,
                    const BulletConfig& config);

  /// Advances handle's bullet by one fixed tick of delta_time seconds:
  /// integrates gravity, then tests the tick's movement segment against
  /// map's collision meshes and against hitboxes. The nearest intersection
  /// along the segment is the outcome, so nothing is hit through a wall or
  /// through another player; with none, a bullet past its max range, or on
  /// its MaxFlightSteps(delta_time)-th Step, expires. Once this returns a
  /// non-kInFlight outcome for handle, the bullet no longer exists - calling
  /// Step again with the same handle is undefined behavior.
  StepResult Step(BulletHandle handle, float delta_time, const physics::World& map, std::span<const Hitbox> hitboxes);

  /// The segment the next Step of handle's bullet by delta_time tests, without
  /// moving it: so a caller can pose and hand in only the hitboxes near it.
  /// handle must be in flight, as for Step.
  [[nodiscard]] Segment NextSegment(BulletHandle handle, float delta_time) const;

 private:
  struct Bullet {
    math::Vec3 origin;
    BulletState state;
    BulletConfig config;
    std::uint32_t steps = 0;
  };

  // Where bullet is after one tick of delta_time.
  static BulletState Advanced(const Bullet& bullet, float delta_time);

  std::unordered_map<BulletHandle, Bullet> bullets_;
  std::uint32_t next_handle_ = 0;
};

}  // namespace augusta::ballistics

#endif  // AUGUSTA_BALLISTICS_H_
