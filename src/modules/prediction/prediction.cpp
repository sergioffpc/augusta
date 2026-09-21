#include "augusta/prediction.h"

#include <array>
#include <cmath>

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/reconciliation.h"

namespace augusta::prediction {

namespace {

constexpr std::size_t kPhaseCount = 5;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

// How far the server's state may be from the client's own prediction of the
// same command and still count as agreeing with it. Below this nothing is
// restored or replayed: the error is not worth a jump, and it cannot pile up,
// since the next acknowledgement is compared with the server's state again,
// not with this one.
constexpr float kPositionTolerance = 0.001F;  // 1 mm.
constexpr float kStaminaTolerance = 0.001F;

// Position is what the player sees, but a stance or a stamina that differs
// changes what the next commands do, so those count as well.
bool NeedsCorrection(const physics::BodyState& authoritative, const physics::BodyState& predicted) {
  return math::Length(authoritative.position - predicted.position) >= kPositionTolerance ||
         authoritative.stance != predicted.stance ||
         std::abs(authoritative.stamina - predicted.stamina) >= kStaminaTolerance;
}

enum PhaseIndex : std::size_t {
  kCommandIngestion = 0,
  kReconciliation,
  kMovement,
  kWeaponHandling,
  kCommit,
};

}  // namespace

struct World::Impl {
  flecs::world ecs;
  physics::World physics;
  // M1 spike (issue #32): the one entity this pipeline predicts, ahead of
  // real ECS component shapes (see prediction.h's header comment) -
  // spawned once here rather than discovered via a component query.
  physics::BodyHandle local_body;
  PhaseEntities phases;

  // Staged by Tick() immediately before each ecs.progress() call, read by
  // the phase systems below; not meaningful outside of a Tick call. Once
  // real ECS component shapes exist, CommandIngestion applying this to an
  // entity (rather than the systems closing over it directly) is what
  // replaces this.
  input::Command tick_command;
  std::uint32_t tick_sequence = 0;
  std::optional<Acknowledgement> tick_acknowledgement;
  State tick_state;

  // The commands sent and what was predicted after each, for Reconciliation to
  // compare the server's answer with and to replay from it.
  History history;

  explicit Impl(const physics::StaminaConfig& stamina_config)
      // enable_gpu=true: PredictionWorld is exclusively client-side (see
      // this class's own header comment) - the server's SimulationWorld
      // never passes this, so GPU is requested here only, not threaded
      // through as a Config field. See physics::World's own header
      // comment for why this currently has no observable effect.
      : physics(stamina_config, /*enable_gpu=*/true), local_body(physics.CreateBody(math::Vec3(0.0F, 0.0F, 0.0F))) {
    // Chain the five phases in Phase's declared order (ADR-0024): each
    // depends_on the previous one, and the first depends on Flecs's
    // built-in OnUpdate phase, so a single ecs.progress() call runs them
    // in exactly this sequence.
    phases[kCommandIngestion] = ecs.entity("CommandIngestion").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kReconciliation] = ecs.entity("Reconciliation").add(flecs::Phase).depends_on(phases[kCommandIngestion]);
    phases[kMovement] = ecs.entity("Movement").add(flecs::Phase).depends_on(phases[kReconciliation]);
    phases[kWeaponHandling] = ecs.entity("WeaponHandling").add(flecs::Phase).depends_on(phases[kMovement]);
    phases[kCommit] = ecs.entity("Commit").add(flecs::Phase).depends_on(phases[kWeaponHandling]);

    // One system per phase, matching the responsibility documented on
    // Phase's matching enumerator in prediction.h. Each uses run()
    // rather than each(): it fires exactly once per Tick regardless of
    // matched entities, since ECS component shapes aren't designed yet
    // (see prediction.h's header comment) - local_body is a single
    // hardcoded handle rather than something discovered by a query.
    ecs.system("CommandIngestionSystem").kind(phases[kCommandIngestion]).run([this](flecs::iter&) {
      OnCommandIngestion();
    });
    ecs.system("ReconciliationSystem").kind(phases[kReconciliation]).run([this](flecs::iter& sys_iter) {
      OnReconciliation(sys_iter.delta_time());
    });
    ecs.system("MovementSystem").kind(phases[kMovement]).run([this](flecs::iter& sys_iter) {
      OnMovement(sys_iter.delta_time());
    });
    ecs.system("WeaponHandlingSystem").kind(phases[kWeaponHandling]).run([this](flecs::iter&) { OnWeaponHandling(); });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([this](flecs::iter&) { OnCommit(); });
  }

  void OnCommandIngestion() {
    const nvtx3::scoped_range range{"CommandIngestion"};
    LT("subsystem=predictionworld event=command_ingestion");
    // tick_command is already staged by Tick() - nothing else to
    // ingest yet without an entity/component to apply it to.
  }

  // Puts the body at the server's state and steps it through the commands sent
  // since, so the present is the server's past with the client's own commands
  // carried forward. Every step, here and in Movement, is a fixed tick long.
  void OnReconciliation(float delta_time) {
    const nvtx3::scoped_range range{"Reconciliation"};
    if (!tick_acknowledgement.has_value()) {
      return;
    }
    const std::optional<Predicted> predicted = history.Acknowledge(tick_acknowledgement->sequence);
    if (!predicted.has_value()) {
      return;
    }
    const physics::BodyState& authoritative = tick_acknowledgement->body;
    if (!NeedsCorrection(authoritative, predicted->body)) {
      LT("subsystem=predictionworld event=reconcile_skipped sequence={}", tick_acknowledgement->sequence);
      return;
    }
    physics::BodyState replayed = physics.Restore(local_body, authoritative, predicted->fall);
    history.Replay([&](const physics::MovementInput& command) {
      replayed = physics.Step(local_body, command, delta_time);
      return Predicted{.body = replayed, .fall = physics.Fall(local_body)};
    });

    const math::Vec3 jump = replayed.position - tick_state.local_body.position;
    LD("subsystem=predictionworld event=reconcile sequence={} error={:.3f} jump={:.3f}", tick_acknowledgement->sequence,
       math::Length(authoritative.position - predicted->body.position), math::Length(jump));
    tick_state.total_correction += jump;
    tick_state.local_body = replayed;
  }

  void OnMovement(float delta_time) {
    const nvtx3::scoped_range range{"Movement"};
    LT("subsystem=predictionworld event=movement");
    tick_state.local_body = physics.Step(local_body, tick_command.movement, delta_time);
  }

  void OnWeaponHandling() {
    const nvtx3::scoped_range range{"WeaponHandling"};
    LT("subsystem=predictionworld event=weapon_handling");
    // TODO(sergioffpc): not yet a module of its own - see prediction.h.
  }

  void OnCommit() {
    const nvtx3::scoped_range range{"Commit"};
    LT("subsystem=predictionworld event=commit");
    // tick_state.local_body is already set by OnMovement; what is left
    // is remembering it for the server's answer to this command.
    if (tick_sequence != 0) {
      history.Record(tick_sequence, tick_command.movement,
                     Predicted{.body = tick_state.local_body, .fall = physics.Fall(local_body)});
    }
  }
};

World::World(const physics::StaminaConfig& stamina_config) : impl_(std::make_unique<Impl>(stamina_config)) {}

World::~World() = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->physics.AddCollisionMesh(mesh);
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::Tick(const input::Command& command, std::uint32_t sequence,
                  const std::optional<Acknowledgement>& acknowledgement, float delta_time) {
  impl_->tick_command = command;
  impl_->tick_sequence = sequence;
  impl_->tick_acknowledgement = acknowledgement;
  impl_->ecs.progress(delta_time);
  return impl_->tick_state;
}

}  // namespace augusta::prediction
