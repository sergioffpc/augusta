#ifndef AUGUSTA_BALLISTICS_H_
#define AUGUSTA_BALLISTICS_H_

#include <cstdint>

#include "augusta/physics.h"

// augusta::ballistics simulates bullet trajectories (gravity-induced
// drop, travel time - US-10), hand-rolled instead of PhysX's generic
// projectile handling (ADR-0002: full control over determinism and a
// core learning goal, not a gap - see that ADR before reaching for an
// external solver). A semi-implicit Euler integrator is enough for v1;
// nothing in REQUIREMENTS.md asks for aerodynamic drag/wind modeling.
//
// Exclusively server-side (ADR-0024): the client never simulates a
// bullet's outcome, only predicts local fire feedback (WeaponHandling),
// so this isn't part of Shared Core despite being physics-adjacent.
//
// World::Step follows the same per-handle, called-once-per-tick shape as
// physics::World::Step, since bullets are ECS entities too
// (ARCHITECTURE.md §5's Shared Core ECS list) advanced by a system that
// iterates them the same way player bodies are. Player hit detection
// reuses physics::World::Raycast rather than maintaining independent
// hitbox geometry - see that method's own doc comment for why PhysX's
// cross-platform non-determinism isn't a concern for a server-only
// query. Body part (US-11) is a coarse zone (head/torso/limb) derived
// from where along the hit body's single collision capsule the ray
// landed - there is no per-body-part hitbox geometry to test against
// yet (that would need character/skeleton data this project hasn't
// designed).
namespace augusta::ballistics {

// Where on a hit player's body a bullet struck (US-11). Coarse zones,
// matching what REQUIREMENTS.md's "e.g., head, torso, limb" actually
// asks for - not per-limb detail.
enum class BodyPart {
  kHead,
  kTorso,
  kLimb,
};

// Tunable per-shot values (ARCHITECTURE.md §8 mechanism/policy/data
// split, same category as physics::StaminaConfig): the gravity
// integration and max-range cutoff *logic* is mechanism code in
// World::Step, identical for every bullet, but these *values* are meant
// to vary per weapon/ammo type (US-12) and be loaded from external
// configuration rather than hardcoded.
struct BulletConfig {
  // Downward acceleration applied each tick, in engine units/s^2.
  float gravity = 0.0F;
  // The bullet resolves as kExpired (see Outcome) once it has travelled
  // this far from its origin without hitting anything - bounds an
  // in-flight bullet's lifetime so a miss doesn't stay simulated
  // forever.
  float max_range = 0.0F;
};

// Opaque handle to an in-flight bullet created by World::Fire. Valid
// only for the World instance that created it, and only until Step
// returns a resolved (non-kInFlight) result for it - see Step.
enum class BulletHandle : std::uint32_t {};

// A bullet's position/velocity at a point in time.
struct BulletState {
  math::Vec3 position;
  math::Vec3 velocity;
};

// One World::Step call's outcome for one bullet.
enum class Outcome {
  // Still travelling; Step must be called again next tick.
  kInFlight,
  // Struck a player's body this tick (see StepResult::target/part/point).
  kHitPlayer,
  // Exceeded BulletConfig::max_range without hitting anything - a miss.
  kExpired,
};

// The result of one World::Step call.
struct StepResult {
  Outcome outcome = Outcome::kInFlight;
  // The bullet's position/velocity as of this call, regardless of
  // outcome.
  BulletState state;
  // The body that was hit. Only meaningful if outcome is kHitPlayer.
  physics::BodyHandle target{};
  // Which part of target was hit. Only meaningful if outcome is
  // kHitPlayer.
  BodyPart part = BodyPart::kTorso;
  // World-space point of impact. Only meaningful if outcome is
  // kHitPlayer.
  math::Vec3 impact_point;
};

// Owns every in-flight bullet for one SimulationWorld. The server
// constructs exactly one.
class World {
 public:
  World();

  // Spawns a new bullet at origin, travelling in direction (need not be
  // pre-normalized) at initial_speed (engine units/s), obeying config
  // for the rest of its flight (US-07: "correct origin, direction, and
  // initial velocity"). config is copied per bullet, not shared with
  // the World - different ammo types can fire simultaneously with
  // different gravity/max_range.
  BulletHandle Fire(const math::Vec3& origin, const math::Vec3& direction, float initial_speed,
                    const BulletConfig& config);

  // Advances handle's bullet by one fixed tick of delta_time seconds:
  // integrates gravity, then tests the tick's movement segment against
  // physics_world's bodies (physics::World::Raycast) for a player hit.
  // Once this returns a non-kInFlight outcome for handle, the bullet no
  // longer exists - calling Step again with the same handle is
  // undefined behavior.
  StepResult Step(BulletHandle handle, float delta_time, const physics::World& physics_world);
};

}  // namespace augusta::ballistics

#endif  // AUGUSTA_BALLISTICS_H_
