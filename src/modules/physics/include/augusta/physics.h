#ifndef AUGUSTA_PHYSICS_H_
#define AUGUSTA_PHYSICS_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include "augusta/math.h"

// augusta::physics wraps PhysX for collision and movement (ADR-0002):
// general body movement, stance transitions, and stamina depletion/
// recovery (US-04, US-05). Ballistics (bullet trajectories) is a separate,
// server-only module (see augusta::ballistics) - PhysX's own generic
// projectile handling is deliberately not used for that.
//
// This module's World is used identically by both PredictionWorld (client,
// predicted/approximate) and SimulationWorld (server, authoritative) - the
// same interface, called from two different orchestrators. PhysX does not
// guarantee cross-platform bit-exact determinism, so callers must not
// assume the client and server ever produce identical results from the
// same inputs; client-side divergence is corrected by restoring the server's
// state and stepping again (Restore), not avoided.
namespace augusta::physics {

// A player's movement stance. Affects collision shape (capsule height/
// radius), movement speed, and stealth (not yet modeled) per US-04.
enum class Stance {
  // Standing upright: default stance, full movement speed.
  kStanding,
  // Crouching: reduced collision height, reduced movement speed.
  kCrouching,
  // Prone: minimal collision height, slowest movement speed.
  kProne,
};

// Opaque handle to a body created by World::CreateBody. Valid only for the
// World instance that created it; passing a handle from one World to
// another is undefined behavior.
enum class BodyHandle : std::uint32_t {};

// This tick's desired movement for one body, supplied by the caller (the
// orchestrating World) from that tick's input commands.
struct MovementInput {
  // Desired movement direction in world space. Does not need to be
  // pre-normalized; World::Step normalizes it internally. The zero vector
  // means "no movement input this tick".
  math::Vec3 direction;
  // True if the player is holding the sprint control this tick. Sprinting
  // depletes stamina (see StaminaConfig) and is ignored (treated as false)
  // once stamina has depleted below the forced-walk threshold.
  bool sprint = false;
  // The stance the player is attempting to be in this tick (e.g. the
  // player pressed the crouch key). World::Step resolves whether the
  // transition is actually possible (e.g. standing up under a low
  // ceiling) and reflects the result in the returned BodyState::stance.
  Stance desired_stance = Stance::kStanding;
};

/// What World::Step carries from one tick to the next besides BodyState: the fall
/// and ground tracking. Putting a body back at an earlier state (World::Restore)
/// puts this back too, or the fall would carry on from the wrong speed.
struct FallState {
  float vertical_speed = 0.0F;
  bool grounded = false;
};

// A body's full movement-relevant state at a point in time: what
// World::Step returns, what World::SetState and World::Restore consume,
// and what the caller packages into its own Prediction/Authoritative State
// each tick.
struct BodyState {
  // World-space position, in engine units (1 unit = 1 meter).
  math::Vec3 position;
  // World-space linear velocity, in engine units per second.
  math::Vec3 velocity;
  // The body's current stance, after any transition resolved this tick.
  Stance stance = Stance::kStanding;
  // Remaining stamina, normalized to [0, 1]. 0 means fully depleted (see
  // StaminaConfig::forced_walk_below); 1 means fully recovered.
  float stamina = 1.0F;
};

// Tunable balance values governing stamina depletion/recovery (US-05).
// These are data, not mechanism or policy (ARCHITECTURE.md §8's third
// category, alongside e.g. per-hit-location damage values): the
// deplete/recover/threshold *logic* is mechanism code inside World::Step,
// identical on both client and server, but these *rates* are meant to be
// loaded from external configuration rather than hardcoded, so gameplay
// tuning doesn't require touching this module's implementation.
struct StaminaConfig {
  // Stamina fraction lost per second while sprinting, e.g. 0.2 means a
  // full stamina bar depletes after 5 seconds of continuous sprinting.
  float deplete_per_second = 0.0F;
  // Stamina fraction regained per second while not sprinting.
  float regen_per_second = 0.0F;
  // When a body's stamina drops at or below this fraction, World::Step
  // ignores MovementInput::sprint (forcing walk speed) until stamina
  // recovers above this threshold again.
  float forced_walk_below = 0.0F;
};

/// A triangle mesh of immovable map geometry, already in world space.
struct CollisionMesh {
  std::vector<math::Vec3> points{};
  /// Three indices into points per triangle.
  std::vector<std::uint32_t> indices{};
};

/// Why a CollisionMesh could not be added to a World.
enum class CollisionMeshError {
  /// No points or no triangles.
  kEmpty,
  /// The indices are not whole triangles, or one points outside points.
  kInvalidIndex,
  /// PhysX could not build a collision mesh from it.
  kCookingFailed,
};

/// Whether mesh is a whole, in-range triangle list World::AddCollisionMesh can take.
std::expected<void, CollisionMeshError> ValidateCollisionMesh(const CollisionMesh& mesh);

/// A phrase for error, for a startup failure to report.
std::string_view DescribeCollisionMeshError(CollisionMeshError error);

// The result of one World::Raycast query.
struct RaycastHit {
  // True if the ray intersected any body within max_distance. If false,
  // every other field here is unspecified.
  bool has_hit = false;
  // Which body was hit. Only meaningful if has_hit is true and the hit was a
  // body: it is left at its default when the ray hit static map geometry.
  BodyHandle body{};
  // World-space point where the ray intersected body. Only meaningful
  // if has_hit is true.
  math::Vec3 point;
  // Distance along the ray from origin to point. Only meaningful if
  // has_hit is true.
  float distance = 0.0F;
};

// Owns every body's PhysX state for one side (client or server) of the
// engine. One World instance is created per process; see
// ARCHITECTURE.md §8 (Threading) for which thread owns it on each side.
class World {
 public:
  // Constructs an empty World (no bodies yet), using config for every
  // body's stamina rules. config is copied; there is no way to change it
  // for a World already constructed. Creates its own PhysX foundation/
  // physics/scene instance (ADR-0002) - not shared with any other World,
  // matching the "one World instance per process" contract above.
  //
  // enable_gpu requests a PxCudaContextManager and GPU-accelerated scene
  // dynamics (client only, see augusta::prediction::World's own
  // constructor - the server's SimulationWorld always leaves this false).
  // Falls back to CPU silently (logged, not thrown) if no CUDA-capable
  // GPU/driver is available. Currently has no observable effect: this
  // World only ever drives PxController::move() (see physics.cpp's
  // header comment), never PxScene::simulate()/fetchResults(), and the
  // GPU dynamics pipeline only accelerates the latter. It's wired in now
  // so a future switch to real rigid-body dynamics (props, ragdolls,
  // projectiles) doesn't also need to plumb this through every caller.
  explicit World(const StaminaConfig& config, bool enable_gpu = false);
  ~World();

  // Move-only: copying would either duplicate or alias the owned PhysX
  // scene, neither of which is meaningful.
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  /// Replaces the stamina rules every body's next Step follows, e.g. with the
  /// server's once a client has joined; stamina already in bodies is kept.
  void SetStaminaConfig(const StaminaConfig& config);

  // Creates a new body with its feet at initial_position (the same position
  // BodyState carries), with default BodyState
  // otherwise (standing, zero velocity, full stamina). Returns a handle
  // valid for the lifetime of this World or until DestroyBody is called
  // with it.
  BodyHandle CreateBody(const math::Vec3& initial_position);

  /// Adds mesh as immovable geometry that bodies collide with and stand on.
  /// Meant to be called while loading a map, before bodies are stepped.
  std::expected<void, CollisionMeshError> AddCollisionMesh(const CollisionMesh& mesh);

  // Removes a body from this World and invalidates its handle. Calling
  // any other method with a handle after it has been destroyed is
  // undefined behavior.
  void DestroyBody(BodyHandle handle);

  // Advances handle's body by one fixed tick of duration delta_time
  // seconds: resolves collision and movement via PhysX according to
  // input, applies stance transitions, and applies stamina depletion/
  // recovery (StaminaConfig). Returns the resulting state, which is also
  // the new internally-held state for handle (visible to a subsequent
  // Step, SetState, or Restore call).
  BodyState Step(BodyHandle handle, const MovementInput& input, float delta_time);

  // Overwrites handle's state immediately - e.g. for spawning or
  // respawning a player at a fixed point (US-03). Unlike Restore, the
  // fall starts over (a teleport is not a fall); do not use this to put a
  // body back where it was.
  void SetState(BodyHandle handle, const BodyState& state);

  /// handle's fall and ground tracking as of now: what Restore takes to put the
  /// body back at the state it has now.
  [[nodiscard]] FallState Fall(BodyHandle handle) const;

  /// Puts handle's body back at state (position, stance and the rest) with fall
  /// as its fall tracking, and returns state. This is the mechanism of a
  /// client-side replay (ADR-0004): augusta::prediction restores the server's
  /// state, with the fall it had itself predicted at that command, and steps the
  /// commands sent since. Unlike SetState, the fall carries on instead of
  /// starting over.
  BodyState Restore(BodyHandle handle, const BodyState& state, const FallState& fall);

  // Casts a ray from origin in direction (need not be pre-normalized) up
  // to max_distance, against every body and collision mesh currently in this
  // World, and returns the closest intersection. Used by augusta::ballistics
  // for player hit detection (US-11): PhysX's cross-platform
  // non-determinism (see the header comment above) isn't a correctness
  // concern there, since ballistics runs exclusively server-side - there
  // is no second, client-side computation to diverge from.
  [[nodiscard]] RaycastHit Raycast(const math::Vec3& origin, const math::Vec3& direction, float max_distance) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::physics

#endif  // AUGUSTA_PHYSICS_H_
