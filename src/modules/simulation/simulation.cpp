#include "augusta/simulation.h"

#include <array>

#include <flecs.h>

#include "augusta/logging.h"

namespace augusta::simulation {

namespace {

// One flecs::entity per Phase value, indexed by Phase's declared order -
// not by its enum value (Phase isn't contiguous-from-zero by design, see
// simulation.h), so PhaseIndex below is the single place that mapping is
// spelled out.
constexpr std::size_t kPhaseCount = 8;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

enum PhaseIndex : std::size_t {
  kCommandIngestion = 0,
  kMovement,
  kWeaponHandling,
  kBallistics,
  kHitDetection,
  kDamage,
  kScriptsBehaviours,
  kCommit,
};

}  // namespace

struct World::Impl {
  flecs::world ecs;
  physics::World physics;
  ballistics::World ballistics;
  scripting::Engine scripting;
  PhaseEntities phases;

  Impl(const physics::StaminaConfig& stamina_config, const std::string& script_path)
      : physics(stamina_config), scripting(script_path) {
    // Chain the eight phases in Phase's declared order (ADR-0023): each
    // depends_on the previous one, and the first depends on Flecs's
    // built-in OnUpdate phase, so a single ecs.progress() call runs them
    // in exactly this sequence - the built-in phases around OnUpdate
    // (OnLoad, PostLoad, ...) stay empty and cost nothing.
    phases[kCommandIngestion] = ecs.entity("CommandIngestion").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kMovement] = ecs.entity("Movement").add(flecs::Phase).depends_on(phases[kCommandIngestion]);
    phases[kWeaponHandling] = ecs.entity("WeaponHandling").add(flecs::Phase).depends_on(phases[kMovement]);
    phases[kBallistics] = ecs.entity("Ballistics").add(flecs::Phase).depends_on(phases[kWeaponHandling]);
    phases[kHitDetection] = ecs.entity("HitDetection").add(flecs::Phase).depends_on(phases[kBallistics]);
    phases[kDamage] = ecs.entity("Damage").add(flecs::Phase).depends_on(phases[kHitDetection]);
    phases[kScriptsBehaviours] = ecs.entity("ScriptsBehaviours").add(flecs::Phase).depends_on(phases[kDamage]);
    phases[kCommit] = ecs.entity("Commit").add(flecs::Phase).depends_on(phases[kScriptsBehaviours]);

    // One system per phase, matching the responsibility documented on
    // Phase's matching enumerator in simulation.h. Each uses run()
    // rather than each(): it fires exactly once per Tick regardless of
    // matched entities, since ECS component shapes aren't designed yet
    // (see simulation.h's header comment) - there is nothing to iterate.
    // Bodies are stubs until those shapes exist; this only establishes
    // each system's place in the pipeline.
    ecs.system("CommandIngestionSystem").kind(phases[kCommandIngestion]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=command_ingestion");
      // TODO(sergioffpc): apply each connected player's this-tick
      // input::Command to their entity.
    });
    ecs.system("MovementSystem").kind(phases[kMovement]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=movement");
      // TODO(sergioffpc): physics::World::Step per player body.
    });
    ecs.system("WeaponHandlingSystem").kind(phases[kWeaponHandling]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=weapon_handling");
      // TODO(sergioffpc): not yet a module of its own - see simulation.h.
    });
    ecs.system("BallisticsSystem").kind(phases[kBallistics]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=ballistics");
      // TODO(sergioffpc): ballistics::World::Step per in-flight bullet.
    });
    ecs.system("HitDetectionSystem").kind(phases[kHitDetection]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=hit_detection");
      // Already folded into BallisticsSystem's ballistics::World::Step
      // call - see simulation.h's Phase::kHitDetection doc comment.
      // Kept as its own phase/system for pipeline ordering.
    });
    ecs.system("DamageSystem").kind(phases[kDamage]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=damage");
      // TODO(sergioffpc): apply damage from each bullet's resolved
      // ballistics::BodyPart.
    });
    ecs.system("ScriptsBehavioursSystem").kind(phases[kScriptsBehaviours]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=scripts_behaviours");
      // TODO(sergioffpc): scripting::Engine::RunHook per relevant hook.
    });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([](flecs::iter&) {
      LT("subsystem=simulationworld event=commit");
      // TODO(sergioffpc): package the tick's resolved state into State.
    });
  }
};

World::World(const physics::StaminaConfig& stamina_config, const std::string& script_path)
    : impl_(std::make_unique<Impl>(stamina_config, script_path)) {}

World::~World() = default;

std::expected<void, physics::StaticMeshError> World::AddStaticMesh(const physics::StaticMesh& mesh) {
  return impl_->physics.AddStaticMesh(mesh);
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::Tick(const std::vector<input::Command>& commands, float delta_time) {
  // TODO(sergioffpc): not yet consumed - see Tick's own doc comment in
  // simulation.h: per-entity command association isn't designed until
  // ECS component shapes are.
  (void)commands;
  impl_->ecs.progress(delta_time);
  return State{};
}

}  // namespace augusta::simulation
