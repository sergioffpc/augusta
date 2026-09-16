#include "augusta/prediction.h"

#include <flecs.h>

#include <array>

#include "augusta/logging.h"

namespace augusta::prediction {

namespace {

constexpr std::size_t kPhaseCount = 5;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

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
  PhaseEntities phases;

  explicit Impl(const physics::StaminaConfig& stamina_config) : physics(stamina_config) {
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
    // (see prediction.h's header comment). Bodies are stubs until those
    // shapes exist, and until Tick's per-call command/authoritative_state
    // arguments have somewhere to flow into the ECS (a singleton,
    // presumably, once one is designed).
    ecs.system("CommandIngestionSystem").kind(phases[kCommandIngestion]).run([](flecs::iter&) {
      TRACE("subsystem=predictionworld event=command_ingestion");
      // TODO(sergioffpc): apply this tick's input::Command to the local
      // player's entity.
    });
    ecs.system("ReconciliationSystem").kind(phases[kReconciliation]).run([](flecs::iter&) {
      TRACE("subsystem=predictionworld event=reconciliation");
      // TODO(sergioffpc): physics::World::Reconcile against
      // authoritative_state, if any arrived.
    });
    ecs.system("MovementSystem").kind(phases[kMovement]).run([](flecs::iter&) {
      TRACE("subsystem=predictionworld event=movement");
      // TODO(sergioffpc): physics::World::Step for the local player body.
    });
    ecs.system("WeaponHandlingSystem").kind(phases[kWeaponHandling]).run([](flecs::iter&) {
      TRACE("subsystem=predictionworld event=weapon_handling");
      // TODO(sergioffpc): not yet a module of its own - see prediction.h.
    });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([](flecs::iter&) {
      TRACE("subsystem=predictionworld event=commit");
      // TODO(sergioffpc): package the tick's predicted state into State.
    });
  }
};

World::World(const physics::StaminaConfig& stamina_config) : impl_(std::make_unique<Impl>(stamina_config)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::Tick(const input::Command& command, const std::optional<physics::BodyState>& authoritative_state,
                  float delta_time) {
  // TODO(sergioffpc): not yet consumed - see Tick's own doc comment in
  // prediction.h: there is no ECS entity/component shape for these to
  // flow into yet.
  (void)command;
  (void)authoritative_state;
  impl_->ecs.progress(delta_time);
  return State{};
}

}  // namespace augusta::prediction
