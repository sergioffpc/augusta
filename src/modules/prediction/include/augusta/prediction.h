#ifndef AUGUSTA_PREDICTION_H_
#define AUGUSTA_PREDICTION_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

#include "augusta/input.h"
#include "augusta/physics.h"

// augusta::prediction orchestrates PredictionWorld (ADR-0024): the
// client-side ECS pipeline, run once per fixed tick on the Simulation
// thread (ADR-0005), alongside SimulationWorld's server-side counterpart
// (augusta::simulation). It composes augusta::physics - the same
// interface SimulationWorld's Movement phase uses (ARCHITECTURE.md §5) -
// into the five ordered phases ADR-0024 defines (see Phase below), and
// emits an immutable Prediction State each tick for
// augusta::presentation to consume.
//
// Unlike SimulationWorld, PredictionWorld only ever predicts the local
// player - never a bullet's trajectory or outcome (ADR-0024: Ballistics/
// HitDetection/Damage stay exclusively server-side) - so World::Tick
// takes a single input::Command, not a per-player list the way
// augusta::simulation::World::Tick does.
//
// Like augusta::simulation, World owns one Flecs world (ADR-0001)
// internally, entirely encapsulated behind Impl (prediction.cpp) - no
// flecs header leaks in here. Phase's five values become five
// dependency-chained flecs::Phase entities, each with one registered
// flecs::system that runs once per Tick regardless of matched entities
// - see prediction.cpp. Entity/component shapes still aren't designed,
// so a system's body is presently a stub; what each one will eventually
// do is documented on its Phase enumerator below.
//
// WeaponHandling has no C++ home yet, same forward reference as in
// augusta::simulation - see that header's comment.
namespace augusta::prediction {

// PredictionWorld's five phases (ADR-0024), executed in this exact
// order every tick. No Ballistics/HitDetection/Damage/Scripts-
// Behaviours phase exists here - those remain exclusively server-side
// (ADR-0024), so this pipeline predicts only immediate local feedback,
// never a bullet's outcome or game policy.
enum class Phase {
  // Mechanism. Applies this tick's local input command
  // (augusta::input::Command, from Input::Sample) to the local player's
  // entity.
  kCommandIngestion,
  // Mechanism. Ingests any authoritative physics::BodyState newly
  // arrived from the server since the last tick, compares it with what was
  // predicted after the same command (History, see reconciliation.h) and
  // applies the error as smooth snap/blend correction (ADR-0004) via
  // physics::World::Correct - no rollback/resimulate. A no-op on ticks where
  // nothing new arrived.
  kReconciliation,
  // Mechanism. Predicted PhysX movement, stamina -
  // augusta::physics::World::Step, same interface SimulationWorld's
  // Movement phase uses on the authoritative body.
  kMovement,
  // Mechanism. Predicts local fire feedback only (muzzle flash, sound
  // cue, recoil, ammo count) - no bullet trajectory; hit/damage stays
  // server-authoritative (ADR-0024). Not yet a module of its own - see
  // the header comment above.
  kWeaponHandling,
  // Mechanism. Packages the tick's predicted state into the immutable
  // Prediction State (State, below).
  kCommit,
};

// PredictionWorld's per-tick output - ADR-0024/ARCHITECTURE.md's
// "Prediction State", consumed by augusta::presentation::World::RunFrame.
// Beyond local_body, deliberately empty for now - same deferred-design
// posture as augusta::simulation::State; its real shape depends on ECS
// component shapes not yet designed.
struct State {
  // The local player's predicted body state as of this tick, after
  // Movement and any Reconciliation (M1 spike, issue #32: this is the
  // "one entity under prediction" the spike proves out, ahead of real
  // ECS component shapes).
  physics::BodyState local_body;
};

/// What the server has told this client about its own player: its body after
/// the command with this sequence, the newest of ours it has processed.
struct Acknowledgement {
  std::uint32_t sequence = 0;
  physics::BodyState body{};
};

// The client's single PredictionWorld. The client constructs exactly
// one, on the Simulation thread (ADR-0005), predicting only the local
// player. Owns the physics sub-world plus the Flecs world it runs inside
// of (see header comment); no I/O happens inside Tick (ARCHITECTURE.md
// §8) - augusta::networking, not this class, is responsible for sending
// commands and receiving authoritative state.
//
// Move-only: copying would either duplicate or alias the owned Flecs
// world, neither of which is meaningful.
class World {
 public:
  // Constructs an empty World: a physics::World (using stamina_config)
  // holding the one local-player body this spike predicts (M1, issue
  // #32), spawned at the world origin, plus the Flecs world with Phase's
  // five phases and their systems registered (see header comment).
  explicit World(const physics::StaminaConfig& stamina_config);
  ~World();

  /// Adds immovable level geometry to this world's physics, the same way SimulationWorld does.
  std::expected<void, physics::StaticMeshError> AddStaticMesh(const physics::StaticMesh& mesh);

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  // Runs all five Phase values above, in their declared order, for one
  // fixed tick of duration delta_time seconds (internally, one
  // flecs::world::progress(delta_time) call), for the local player only.
  // command is this tick's local input, and sequence the number it is sent
  // to the server under (0 if it is not sent, e.g. before joining; such a
  // tick cannot be reconciled against). acknowledgement is the newest state
  // received from the server for this player, if any: the server repeats it
  // while it waits for input, so passing the same one again is harmless -
  // Reconciliation acts on each acknowledged sequence once. Returns the
  // tick's Prediction State.
  State Tick(const input::Command& command, std::uint32_t sequence,
             const std::optional<Acknowledgement>& acknowledgement, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::prediction

#endif  // AUGUSTA_PREDICTION_H_
