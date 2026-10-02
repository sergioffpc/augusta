#ifndef AUGUSTA_PREDICTION_H_
#define AUGUSTA_PREDICTION_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

#include "augusta/command.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/weapon.h"

// augusta::prediction orchestrates PredictionWorld (ADR-0024): the
// client-side ECS pipeline, run once per fixed tick on the Simulation
// thread (ADR-0005), alongside SimulationWorld's server-side counterpart
// (augusta::simulation). It composes augusta::physics and augusta::weapon -
// the same interfaces SimulationWorld's Movement and WeaponHandling phases
// use (ARCHITECTURE.md §5) - into the five ordered phases ADR-0024 defines
// (see Phase below), and emits an immutable Prediction State each tick for
// augusta::presentation to consume.
//
// Unlike SimulationWorld, PredictionWorld only ever predicts the local
// player - never a bullet's trajectory or outcome (ADR-0024: Ballistics/
// HitDetection/Damage stay exclusively server-side) - so World::Tick
// takes a single command::Command, not a per-player list the way
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
namespace augusta::prediction {

// PredictionWorld's five phases (ADR-0024), executed in this exact
// order every tick. No Ballistics/HitDetection/Damage/Scripts-
// Behaviours phase exists here - those remain exclusively server-side
// (ADR-0024), so this pipeline predicts only immediate local feedback,
// never a bullet's outcome or game policy.
enum class Phase {
  // Mechanism. Applies this tick's local input command
  // (augusta::command::Command, from Input::Sample) to the local player's
  // entity.
  kCommandIngestion,
  // Mechanism. Ingests any authoritative body and rifle newly arrived from
  // the server since the last tick, compares them with what was predicted
  // after the same command (History, see reconciliation.h), then, if either
  // differs, puts both at the server's state and replays the commands sent
  // since (ADR-0004) through Movement and WeaponHandling alike, via
  // physics::World::Restore and Step and weapon::Step. A no-op on ticks
  // where nothing new arrived.
  kReconciliation,
  // Mechanism. Predicted PhysX movement, stamina -
  // augusta::physics::World::Step, same interface SimulationWorld's
  // Movement phase uses on the authoritative body.
  kMovement,
  // Mechanism. Predicts the local player's own fire, reload and recoil
  // (US-07 to US-09) - augusta::weapon::Step, the same function
  // SimulationWorld's WeaponHandling phase runs on the authoritative rifle,
  // with the Parameters the server sent. Only the rifle and whether it
  // fired: no bullet, no trajectory, no hit; a bullet's outcome stays
  // server-authoritative (ADR-0024).
  kWeaponHandling,
  // Mechanism. Packages the tick's predicted state into the immutable
  // Prediction State (State, below).
  kCommit,
};

// PredictionWorld's per-tick output - ADR-0024/ARCHITECTURE.md's
// "Prediction State", consumed by augusta::presentation::World::RunFrame.
// It holds the local player's body and rifle: the one entity a client
// predicts.
struct State {
  // The local player's predicted body state as of this tick, after
  // Movement and any Reconciliation (M1 spike, issue #32: this is the
  // "one entity under prediction" the spike proves out, ahead of real
  // ECS component shapes).
  physics::BodyState local_body;
  /// Every jump Reconciliation has made to local_body since the world began,
  /// summed: how far each replay moved the body from where the previous tick
  /// left it. A reader that sees only some of the ticks (presentation, one
  /// frame at a time) gets the jumps between two states it saw, every one and
  /// none twice, from the difference of their totals.
  math::Vec3 total_correction{};
  /// The local player's predicted rifle as of this tick, after WeaponHandling
  /// and any Reconciliation. Its Recoil offset is how far off the view of the
  /// tick's Command the next round leaves.
  weapon::State rifle{};
  /// How many times Reconciliation has put the rifle at a server's state that
  /// differed from the one predicted, since the world began: a rifle predicted
  /// right never adds to it.
  std::uint32_t rifle_corrections = 0;
  /// Every round the rifle has fired since the world began, summed; a replay
  /// adds none. A reader that sees only some of the ticks (presentation, one
  /// frame at a time) gets the rounds fired between two states it saw, every
  /// one and none twice, from the difference of their totals: what the muzzle
  /// flash is drawn from.
  std::uint32_t total_rounds_fired = 0;
  /// How many rounds the rifle fired on this tick, at most one. Never a
  /// bullet's outcome.
  std::uint8_t rounds_fired = 0;
};

/// What the server has told this client about its own player: its body and
/// its rifle after the command with this sequence, the newest of ours it has
/// processed.
struct Acknowledgement {
  command::Sequence sequence = 0;
  physics::BodyState body{};
  weapon::State rifle{};
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
  // Constructs an empty World: a physics::World holding the one local-player
  // body this spike predicts (M1, issue #32), spawned at the world origin,
  // plus the Flecs world with Phase's five phases and their systems
  // registered (see header comment). It holds no rules of the server's until
  // Start gives it them, so nothing is predicted with rules of its own: until
  // then its rifle holds no round and fires nothing.
  World();
  ~World();

  /// Adds immovable level geometry to this world's physics, the same way SimulationWorld does.
  std::expected<void, physics::CollisionMeshError> AddCollisionMesh(const physics::CollisionMesh& mesh);

  /// Starts the local player over at spawn, standing, at full stamina and with
  /// a rifle ready to fire, under the stamina rules and the rifle of
  /// parameters: what the server told this client when it admitted it, so the
  /// client never predicts with rules of its own. Call before the first command
  /// is sent; nothing predicted earlier is kept but State::total_correction,
  /// State::rifle_corrections and State::total_rounds_fired, which starting
  /// over does not add to.
  void Start(const math::Vec3& spawn, const parameters::Parameters& parameters);

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
  State Tick(const command::Command& command, command::Sequence sequence,
             const std::optional<Acknowledgement>& acknowledgement, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::prediction

#endif  // AUGUSTA_PREDICTION_H_
