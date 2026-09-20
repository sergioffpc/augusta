#include "augusta/prediction.h"

#include <array>

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/logging.h"
#include "augusta/math.h"

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
  std::optional<physics::BodyState> tick_authoritative_state;
  State tick_state;

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
    ecs.system("CommandIngestionSystem").kind(phases[kCommandIngestion]).run([](flecs::iter&) {
      const nvtx3::scoped_range range{"CommandIngestion"};
      LT("subsystem=predictionworld event=command_ingestion");
      // tick_command is already staged by Tick() - nothing else to
      // ingest yet without an entity/component to apply it to.
    });
    ecs.system("ReconciliationSystem").kind(phases[kReconciliation]).run([this](flecs::iter&) {
      const nvtx3::scoped_range range{"Reconciliation"};
      if (!tick_authoritative_state.has_value()) {
        return;
      }
      LT("subsystem=predictionworld event=reconciliation");
      physics.Reconcile(local_body, *tick_authoritative_state);
    });
    ecs.system("MovementSystem").kind(phases[kMovement]).run([this](flecs::iter& sys_iter) {
      const nvtx3::scoped_range range{"Movement"};
      LT("subsystem=predictionworld event=movement");
      tick_state.local_body = physics.Step(local_body, tick_command.movement, sys_iter.delta_time());
    });
    ecs.system("WeaponHandlingSystem").kind(phases[kWeaponHandling]).run([](flecs::iter&) {
      const nvtx3::scoped_range range{"WeaponHandling"};
      LT("subsystem=predictionworld event=weapon_handling");
      // TODO(sergioffpc): not yet a module of its own - see prediction.h.
    });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([](flecs::iter&) {
      const nvtx3::scoped_range range{"Commit"};
      LT("subsystem=predictionworld event=commit");
      // tick_state.local_body is already committed by MovementSystem -
      // nothing else in State to package yet.
    });
  }
};

World::World(const physics::StaminaConfig& stamina_config) : impl_(std::make_unique<Impl>(stamina_config)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::Tick(const input::Command& command, const std::optional<physics::BodyState>& authoritative_state,
                  float delta_time) {
  impl_->tick_command = command;
  impl_->tick_authoritative_state = authoritative_state;
  impl_->ecs.progress(delta_time);
  return impl_->tick_state;
}

}  // namespace augusta::prediction
