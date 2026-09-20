#ifndef AUGUSTA_SIMULATION_H_
#define AUGUSTA_SIMULATION_H_

#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/input.h"
#include "augusta/physics.h"
#include "augusta/scripting.h"

// augusta::simulation orchestrates SimulationWorld (ADR-0023): the single
// authoritative ECS pipeline, run once per tick on the server's
// Simulation thread (ADR-0005). It composes the mechanism modules that
// already exist (physics, ballistics, scripting) into the eight ordered
// phases ADR-0023 defines (see Phase below), and will hand its per-tick
// output to augusta::replication - not yet implemented - to reach
// clients (ARCHITECTURE.md §5's "SimulationWorld... emits authoritative
// state each tick").
//
// World owns one Flecs world (ADR-0001) internally, entirely
// encapsulated behind Impl (simulation.cpp) - Flecs is this module's
// implementation detail, not part of its public interface, so no flecs
// header leaks in here. Phase's eight values become, in the same order,
// eight dependency-chained flecs::Phase entities, each with one
// registered flecs::system (named "<Phase>System") that runs once per
// Tick regardless of matched entities - see simulation.cpp. Entity/
// component shapes still aren't designed, so a system's body is
// presently a stub; what each one will eventually do is documented on
// its Phase enumerator below.
//
// WeaponHandling has no C++ home yet either: ARCHITECTURE.md's Shared
// Core lists it as its own module (one interface used identically by
// SimulationWorld and PredictionWorld, mirroring how both Worlds share
// augusta::physics), but it hasn't been created. Phase::kWeaponHandling
// is a forward reference to it, not a system implemented here.
namespace augusta::simulation {

// SimulationWorld's eight phases (ADR-0023), executed in this exact
// order every tick. Scripts/Behaviours runs last, after Damage has
// resolved the tick's deaths, so a hook can react to what just happened
// (e.g. evaluate a win condition) and schedule what follows (spawns,
// round transitions) for the next tick.
enum class Phase {
  // Mechanism. Applies this tick's already-validated client commands
  // (augusta::input::Command; Input Validation - US-15 - is a boundary
  // component outside this World, per ARCHITECTURE.md §8, and has
  // already run by the time World::Tick sees them) to their entities.
  kCommandIngestion,
  // Mechanism. PhysX integration, stamina, collision resolution
  // (US-04, US-05) - augusta::physics::World::Step, one call per player
  // body. The same physics::World interface PredictionWorld's Movement
  // phase uses (ARCHITECTURE.md §5).
  kMovement,
  // Mechanism. Aim/ADS, fire, reload, recoil, ammo rules (US-06-US-09).
  // Not yet a module of its own - see the header comment above; this
  // enumerator is a forward reference to it, not a system implemented
  // here.
  kWeaponHandling,
  // Mechanism. Advances in-flight bullet trajectories (US-10) -
  // augusta::ballistics::World::Step, one call per in-flight bullet.
  kBallistics,
  // Mechanism. Resolves impact point + body part (US-11). Already
  // folded into the same ballistics::World::Step call as kBallistics -
  // see ballistics.h: one Step call returns Outcome::kHitPlayer
  // together with BodyPart and impact_point, using
  // physics::World::Raycast internally. Kept as its own named phase per
  // ADR-0023 for pipeline ordering/extensibility, not a second system
  // call today.
  kHitDetection,
  // Mechanism, reads Data/Config. Applies damage and marks
  // death/spectator (US-12, US-13), from each bullet's resolved
  // ballistics::BodyPart. Per-hit-location damage values are data
  // (ARCHITECTURE.md §8's mechanism/policy/data split, same category as
  // physics::StaminaConfig) - not yet a module or config type of its
  // own.
  kDamage,
  // Policy, sandboxed Lua (ADR-0022). Win condition, round transitions,
  // spawn logic (US-14, US-03) - augusta::scripting::Engine::RunHook,
  // once per relevant hook. The only phase not implemented in C++.
  kScriptsBehaviours,
  // Mechanism. Packages the tick's resolved state into Authoritative
  // State (State, below), for augusta::replication (not yet
  // implemented) to send to clients.
  kCommit,
};

// SimulationWorld's per-tick output - ADR-0023/ARCHITECTURE.md's
// "Authoritative State". Deliberately empty for now: its real shape
// depends on ECS component shapes (see header comment) and on what
// augusta::replication ends up needing to send, neither of which exist
// yet - same deferred-design posture as augusta::renderer's "what gets
// drawn".
struct State {};

// The single authoritative SimulationWorld. The server constructs
// exactly one, on the Simulation thread (ADR-0005). Owns the mechanism
// sub-worlds each tick drives through in Phase order, plus the Flecs
// world they run inside of (see header comment); no I/O happens inside
// Tick (ARCHITECTURE.md §8) - augusta::replication, not this class, is
// responsible for getting State to the network.
//
// Move-only: copying would either duplicate or alias the owned Flecs
// world, neither of which is meaningful.
class World {
 public:
  // Constructs an empty World: an empty physics::World (using
  // stamina_config for every player body) and an empty ballistics::World
  // (no bullets in flight yet), a scripting::Engine loaded from
  // script_path, and the Flecs world with Phase's eight phases and their
  // systems registered (see header comment). Throws whatever
  // scripting::Engine's constructor throws if script_path fails to load.
  World(const physics::StaminaConfig& stamina_config, const std::string& script_path);
  ~World();

  /// Adds immovable level geometry to this world's physics, the same way PredictionWorld does.
  std::expected<void, physics::StaticMeshError> AddStaticMesh(const physics::StaticMesh& mesh);

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  // Runs all eight Phase values above, in their declared order, for one
  // fixed tick of duration delta_time seconds (internally, one
  // flecs::world::progress(delta_time) call). commands holds this tick's
  // validated input from every connected player (US-02, 2-8 players) -
  // unlike PredictionWorld, which only ever ticks the local player (see
  // augusta::prediction::World::Tick). Returns the tick's Authoritative
  // State.
  State Tick(const std::vector<input::Command>& commands, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_H_
