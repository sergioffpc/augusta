#ifndef AUGUSTA_PHYSICS_H_
#define AUGUSTA_PHYSICS_H_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/math.h"

/// \file
/// augusta::physics wraps PhysX for collision and movement (ADR-0002):
/// general body movement, stance transitions, and stamina depletion/
/// recovery (US-04, US-05). Ballistics (bullet trajectories) is a separate
/// module (see augusta::ballistics) - PhysX's own generic projectile
/// handling is deliberately not used for that. The module also builds a
/// map's static collision from a verified Pack (see augusta/map.h), so it
/// depends on augusta_assets.
///
/// This module's World is used identically by both PredictionWorld (client,
/// predicted/approximate) and SimulationWorld (server, authoritative) - the
/// same interface, called from two different orchestrators. PhysX does not
/// guarantee cross-platform bit-exact determinism, so callers must not
/// assume the client and server ever produce identical results from the
/// same inputs; client-side divergence is corrected by restoring the server's
/// state and stepping again (Restore), not avoided.
namespace augusta::physics {

/// A player's movement stance. Affects collision shape (capsule height/
/// radius), movement speed, and stealth (not yet modeled) per US-04.
enum class Stance {
  /// Standing upright: default stance, full movement speed.
  kStanding,
  /// Crouching: reduced collision height, reduced movement speed.
  kCrouching,
  /// Prone: minimal collision height, slowest movement speed.
  kProne,
};

/// How tall a body is in stance, feet to the top of its collision capsule, in
/// meters - lower crouching than standing, and lower prone than crouching.
[[nodiscard]] float StanceHeight(Stance stance);

/// Where a point of a standing body is once the body is in stance:
/// standing_point is relative to the feet, and is lowered in proportion to the
/// body's height in that stance (StanceHeight). What keeps a character's eye
/// (ADR-0040) with its body as it crouches or goes prone, for the client's
/// camera and the server's Shots alike.
[[nodiscard]] math::Vec3 LowerToStance(const math::Vec3& standing_point, Stance stance);

/// A body's collision capsule: the points within radius of the segment between
/// the centres of its two hemispherical caps.
struct Capsule {
  /// World space, as BodyState::position: the lower cap's centre, radius above
  /// the feet.
  math::Vec3 bottom{};
  /// The upper cap's centre, radius below the top of the body in its stance
  /// (StanceHeight).
  math::Vec3 top{};
  /// In metres.
  float radius = 0.0F;
};

/// Opaque handle to a body created by World::CreateBody. Valid only for the
/// World instance that created it; passing a handle from one World to
/// another is undefined behavior.
enum class BodyHandle : std::uint32_t {};

/// Opaque id of a Surface (GLOSSARY.md): something a round can strike, here a
/// collision mesh of the Map. Its caller chooses it when adding the mesh
/// (World::AddCollisionMesh), and World::RaycastMap hands it back for the mesh
/// struck; physics stores and returns it but never interprets it.
enum class SurfaceId : std::uint32_t {};

/// This tick's desired movement for one body, supplied by the caller (the
/// orchestrating World) from that tick's input commands.
struct MovementInput {
  /// Desired movement direction in world space. Does not need to be
  /// pre-normalized; World::Step normalizes it internally. The zero vector
  /// means "no movement input this tick", and is the default: glm leaves a
  /// default-initialized vector's components unset.
  math::Vec3 direction{};
  /// True if the player is holding the sprint control this tick. Sprint is
  /// honored only while the body is standing, moving and not exhausted (see
  /// BodyState::exhausted), and only then depletes stamina (see StaminaConfig);
  /// otherwise it is ignored (treated as false).
  bool sprint = false;
  /// The stance the player is attempting to be in this tick (e.g. the
  /// player pressed the crouch key). World::Step resolves whether the
  /// transition is actually possible (e.g. standing up under a low
  /// ceiling) and reflects the result in the returned BodyState::stance.
  Stance desired_stance = Stance::kStanding;

  bool operator==(const MovementInput&) const = default;
};

/// What World::Step carries from one tick to the next besides BodyState: the fall
/// and ground tracking. Putting a body back at an earlier state (World::Restore)
/// puts this back too, or the fall would carry on from the wrong speed.
struct FallState {
  float vertical_speed = 0.0F;
  bool grounded = false;
};

/// A body's full movement-relevant state at a point in time: what
/// World::Step returns, what World::SetState and World::Restore consume,
/// and what the caller packages into its own Prediction/Authoritative State
/// each tick.
struct BodyState {
  /// World-space position, in engine units (1 unit = 1 meter).
  math::Vec3 position;
  /// World-space linear velocity, in engine units per second.
  math::Vec3 velocity;
  /// The body's current stance, after any transition resolved this tick.
  Stance stance = Stance::kStanding;
  /// Remaining stamina, normalized to [0, 1]. 0 means fully depleted; 1 means
  /// fully recovered.
  float stamina = 1.0F;
  /// Whether the body ran its stamina out and has not yet recovered above
  /// StaminaConfig::forced_walk_below: while set, sprint is ignored. It changes
  /// what the next commands do, so it travels and is restored with the rest.
  bool exhausted = false;

  bool operator==(const BodyState&) const = default;
};

/// The collision capsule of a body in state: standing on its position in its
/// stance, as World holds the body's controller. What a client draws a body
/// the server reported as.
[[nodiscard]] Capsule CapsuleOf(const BodyState& state);

/// Tunable balance values governing stamina depletion/recovery (US-05).
/// These are data, not mechanism or policy (ARCHITECTURE.md §8's third
/// category, alongside e.g. per-hit-location damage values): the
/// deplete/recover/threshold *logic* is mechanism code inside World::Step,
/// identical on both client and server, but these *rates* are meant to be
/// loaded from external configuration rather than hardcoded, so gameplay
/// tuning doesn't require touching this module's implementation.
struct StaminaConfig {
  /// Stamina fraction lost per second while sprint is honored, e.g. 0.2 means
  /// a full stamina bar depletes after 5 seconds of continuous sprinting.
  float deplete_per_second = 0.0F;
  /// Stamina fraction regained per second while sprint is not honored.
  float regen_per_second = 0.0F;
  /// Once a body runs its stamina out it is exhausted (BodyState::exhausted):
  /// World::Step ignores MovementInput::sprint (forcing walk speed) until
  /// stamina recovers above this fraction again.
  float forced_walk_below = 0.0F;
};

/// A triangle mesh of immovable map geometry, already in world space.
struct CollisionMesh {
  std::vector<math::Vec3> points;
  /// Three indices into points per triangle.
  std::vector<std::uint32_t> indices;
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

/// The result of one World::Raycast query.
struct RaycastHit {
  /// True if the ray intersected any body within max_distance. If false,
  /// every other field here is unspecified.
  bool has_hit = false;
  /// Which body was hit. Only meaningful if has_hit is true and the hit was a
  /// body: it is left at its default when the ray hit static map geometry.
  BodyHandle body{};
  /// World-space point where the ray intersected body. Only meaningful
  /// if has_hit is true.
  math::Vec3 point;
  /// Distance along the ray from origin to point. Only meaningful if
  /// has_hit is true.
  float distance = 0.0F;
};

/// The result of one World::RaycastMap query.
struct MapRaycastHit {
  /// True if the ray intersected a collision mesh within max_distance. If
  /// false, every other field here is unspecified.
  bool has_hit = false;
  /// The Surface id the struck mesh was added with. Only meaningful if has_hit
  /// is true.
  SurfaceId surface{};
  /// World-space point where the ray intersected the mesh. Only meaningful if
  /// has_hit is true.
  math::Vec3 point;
  /// Distance along the ray from origin to point. Only meaningful if has_hit
  /// is true.
  float distance = 0.0F;
};

#if AUGUSTA_DEBUG_TOOLS
/// What a World's visualization draws, and where (World::SetVisualization), in
/// a Development build alone (ADR-0074): PhysX's own view of its collision, for
/// the Collision Debug overlay.
struct Visualization {
  /// Off, a World draws nothing and its visualization costs nothing.
  bool on = false;
  /// Every collision shape: the Map's meshes and each body's capsule.
  bool collision_shapes = true;
  /// Each collision shape's axis-aligned bounds.
  bool bounds = false;
  /// Contact points and their normals.
  bool contacts = false;
  /// Joints' frames and limits.
  bool joints = false;
  /// The character controllers' own volumes, such as the space a move sweeps.
  bool controller_volumes = true;
  /// The culling box: only the scene's shapes in the cube of half-extent
  /// cull_half_extent metres around cull_center are drawn, so a whole Map of
  /// triangle edges does not bury the frame or the tick. Controller volumes
  /// are drawn wherever they are.
  math::Vec3 cull_center{};
  float cull_half_extent = 0.0F;

  bool operator==(const Visualization&) const = default;
};

/// One line of a World's visualization, in world space.
struct VisualizationLine {
  math::Vec3 from{};
  math::Vec3 to{};
  /// PhysX's colour for it, as PhysX gives it (0xAARRGGBB, PxDebugColor), so
  /// it reads as PhysX's documentation describes it.
  std::uint32_t argb = 0;

  bool operator==(const VisualizationLine&) const = default;
};
#endif

/// Owns every body's PhysX state for one side (client or server) of the
/// engine. One World instance is created per process; see
/// ARCHITECTURE.md §8 (Threading) for which thread owns it on each side.
///
/// Every body lives on the grids the Networking Protocol sends it on
/// (math::SnapPosition and the rest, ADR-0038): CreateBody, Step, SetState
/// and Restore round what they hold and return to them. So a server's body is
/// exactly what its clients are told, and a client that predicts correctly
/// rounds as the server did and never corrects for rounding.
class World {
 public:
  /// Constructs an empty World (no bodies yet), using config for every
  /// body's stamina rules. config is copied; there is no way to change it
  /// for a World already constructed. Creates its own PhysX foundation/
  /// physics/scene instance (ADR-0002) - not shared with any other World,
  /// matching the "one World instance per process" contract above.
  ///
  /// CPU only, and with no worker threads: this World moves its bodies with
  /// PxController::move() alone (see physics.cpp's header comment), and its
  /// only PxScene::simulate()/fetchResults() is StepVisualization's, in a
  /// Development build, so neither GPU dynamics nor a
  /// simulation thread pool would do anything but cost startup time,
  /// threads and (next to the client's renderer) VRAM. Revisit once a real
  /// rigid-body simulate step (props, ragdolls, projectiles) exists.
  explicit World(const StaminaConfig& config);
  ~World();

  /// Move-only: copying would either duplicate or alias the owned PhysX
  /// scene, neither of which is meaningful.
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  /// Replaces the stamina rules every body's next Step follows, e.g. with the
  /// server's once a client has joined; stamina already in bodies is kept.
  void SetStaminaConfig(const StaminaConfig& config);

  /// Creates a new body with its feet at initial_position (the same position
  /// BodyState carries), with default BodyState
  /// otherwise (standing, zero velocity, full stamina). Returns a handle
  /// valid for the lifetime of this World or until DestroyBody is called
  /// with it.
  BodyHandle CreateBody(const math::Vec3& initial_position);

  /// Adds mesh as immovable geometry that bodies collide with and stand on,
  /// known to RaycastMap as surface. Meant to be called while loading a map,
  /// before bodies are stepped. surface has no default: one could silently
  /// equal an id the caller gives something else a round can strike.
  std::expected<void, CollisionMeshError> AddCollisionMesh(const CollisionMesh& mesh, SurfaceId surface);

  /// Removes a body from this World and invalidates its handle. Calling
  /// any other method with a handle after it has been destroyed is
  /// undefined behavior.
  void DestroyBody(BodyHandle handle);

  /// Advances handle's body by one fixed tick of duration delta_time
  /// seconds: resolves collision and movement via PhysX according to
  /// input, applies stance transitions, and applies stamina depletion/
  /// recovery (StaminaConfig). Returns the resulting state, which is also
  /// the new internally-held state for handle (visible to a subsequent
  /// Step, SetState, or Restore call).
  BodyState Step(BodyHandle handle, const MovementInput& input, float delta_time);

  /// Overwrites handle's state immediately - e.g. for spawning or
  /// respawning a player at a fixed point (US-03). Unlike Restore, the
  /// fall starts over (a teleport is not a fall); do not use this to put a
  /// body back where it was.
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

  /// Casts a ray from origin in direction (need not be pre-normalized) up
  /// to max_distance, against every body and collision mesh currently in this
  /// World, and returns the closest intersection.
  ///
  /// A body is not hit where it is: the ray sees it where its PhysX
  /// controller was made (centered on the world origin), however far
  /// CreateBody, Step, SetState or Restore have put it since (ADR-0002), until
  /// a StepVisualization moves its actor to where its body is.
  /// Player hit detection does not use this - see RaycastMap.
  [[nodiscard]] RaycastHit Raycast(const math::Vec3& origin, const math::Vec3& direction, float max_distance) const;

  /// Like Raycast, but against the collision meshes (the Map) only: no body is
  /// ever reported, wherever its controller is. What a bullet's flight is tested
  /// against, since player hits are judged against hitboxes instead (ADR-0044).
  /// Reports which mesh it struck by the Surface id the mesh was added with.
  [[nodiscard]] MapRaycastHit RaycastMap(const math::Vec3& origin, const math::Vec3& direction,
                                         float max_distance) const;

#if AUGUSTA_DEBUG_TOOLS
  /// Switches this World's visualization to visualization: off, or on with
  /// what it draws and its culling box. A World starts with it off, and one
  /// that never turns it on pays nothing for it. Development build only.
  void SetVisualization(const Visualization& visualization);

  /// Steps this World's scene once by delta_time seconds, a fixed tick, and
  /// returns the lines PhysX then draws of it, its character controllers'
  /// included; nullopt, stepping nothing, while the visualization is off.
  /// Development build only.
  ///
  /// The step is harmless: a World holds the Map's static meshes and each
  /// body's kinematic actor, nothing dynamic, so it moves only those actors,
  /// to where their bodies last moved. No body's Step, SetState, Restore or
  /// Fall sees them, nor RaycastMap; only Raycast sees a body where it is
  /// rather than where its controller was made, from this call on.
  [[nodiscard]] std::optional<std::vector<VisualizationLine>> StepVisualization(float delta_time);
#endif

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#if AUGUSTA_DEBUG_TOOLS
/// A recording of every PhysX scene in the process to one OmniPVD file (`.ovd`),
/// which OmniPVD opens, made by StartRecording. A debug tool's (ADR-0074): only
/// a Development build has it. It holds a use of the PhysX state every World
/// shares for as long as it lives, so the file covers each World the process
/// makes until the recording goes, and is complete once it has (and every
/// World with it). OmniPVD has no stop, so a process records one file.
class Recording {
 public:
  ~Recording();
  Recording(const Recording&) = delete;
  Recording& operator=(const Recording&) = delete;
  Recording(Recording&&) noexcept;
  Recording& operator=(Recording&&) noexcept;

  /// The file the recording writes.
  [[nodiscard]] const std::filesystem::path& File() const;

 private:
  struct Impl;
  explicit Recording(std::unique_ptr<Impl> impl);
  friend std::expected<Recording, std::string> StartRecording(std::string_view executable);

  std::unique_ptr<Impl> impl_;
};

/// Starts recording the process's PhysX scenes to
/// `<executable>-<startup time, UTC>.ovd` in the system's temporary directory,
/// and logs its path at INFO (`subsystem=physics`). It must come before any
/// World exists, since PhysX is made with the recording or without it. Fails,
/// saying why, when it cannot record: a World already exists, this build's
/// PhysX has no OmniPVD (as every non-Debug build's), or OmniPVD's runtime
/// (PVDRuntime_64, beside the executable) does not load. The process then runs
/// on unrecorded.
[[nodiscard]] std::expected<Recording, std::string> StartRecording(std::string_view executable);
#endif

}  // namespace augusta::physics

#endif  // AUGUSTA_PHYSICS_H_
