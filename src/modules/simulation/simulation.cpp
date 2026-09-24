#include "augusta/simulation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include "augusta/ballistics.h"
#include "augusta/input.h"
#include "augusta/physics.h"
#include "augusta/scripting.h"

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

// A player entity's components.
struct Player {
  PlayerId id{};
};

struct Body {
  physics::BodyHandle handle{};
  physics::BodyState state{};
};

// What CommandIngestion last decided the player is trying to do; Movement acts on it.
struct Intent {
  physics::MovementInput input{};
};

}  // namespace

struct World::Impl {
  // Where a player lives, for RemovePlayer.
  struct Slot {
    flecs::entity entity;
    physics::BodyHandle body{};
  };

  flecs::world ecs;
  physics::World physics;
  ballistics::World ballistics;
  scripting::Engine scripting;
  PhaseEntities phases;
  std::unordered_map<PlayerId, Slot> players;
  // Set by Tick for CommandIngestion to read, and filled by Commit for Tick to return.
  std::unordered_map<PlayerId, input::Command> tick_commands;
  State committed;

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
    ecs.system<const Player, Intent>("CommandIngestionSystem")
        .kind(phases[kCommandIngestion])
        .each([this](const Player& player, Intent& intent) { OnCommandIngestion(player, intent); });
    ecs.system<Body, const Intent>("MovementSystem")
        .kind(phases[kMovement])
        .each([this](flecs::iter& it, std::size_t /*row*/, Body& body, const Intent& intent) {
          OnMovement(it.delta_time(), body, intent);
        });
    ecs.system("WeaponHandlingSystem").kind(phases[kWeaponHandling]).run([this](flecs::iter&) { OnWeaponHandling(); });
    ecs.system("BallisticsSystem").kind(phases[kBallistics]).run([this](flecs::iter&) { OnBallistics(); });
    ecs.system("HitDetectionSystem").kind(phases[kHitDetection]).run([this](flecs::iter&) { OnHitDetection(); });
    ecs.system("DamageSystem").kind(phases[kDamage]).run([this](flecs::iter&) { OnDamage(); });
    ecs.system("ScriptsBehavioursSystem").kind(phases[kScriptsBehaviours]).run([this](flecs::iter&) {
      OnScriptsBehaviours();
    });
    ecs.system<const Player, const Body>("CommitSystem")
        .kind(phases[kCommit])
        .each([this](const Player& player, const Body& body) { OnCommit(player, body); });
  }

  // A player with no command this tick stops and keeps the stance it asked for.
  void OnCommandIngestion(const Player& player, Intent& intent) {
    const auto command = tick_commands.find(player.id);
    if (command == tick_commands.end()) {
      intent.input.direction = math::Vec3{};
      intent.input.sprint = false;
      return;
    }
    intent.input = command->second.movement;
  }

  void OnMovement(float delta_time, Body& body, const Intent& intent) {
    body.state = physics.Step(body.handle, intent.input, delta_time);
  }

  void OnWeaponHandling() {
    // TODO(sergioffpc): not yet a module of its own - see simulation.h.
  }

  void OnBallistics() {
    // TODO(sergioffpc): ballistics::World::Step per in-flight bullet.
  }

  void OnHitDetection() {
    // Already folded into OnBallistics's ballistics::World::Step
    // call - see simulation.h's Phase::kHitDetection doc comment.
    // Kept as its own phase/system for pipeline ordering.
  }

  void OnDamage() {
    // TODO(sergioffpc): apply damage from each bullet's resolved
    // ballistics::BodyPart.
  }

  void OnScriptsBehaviours() {
    // TODO(sergioffpc): scripting::Engine::RunHook per relevant hook.
  }

  void OnCommit(const Player& player, const Body& body) {
    committed.players.push_back(PlayerState{.player = player.id, .body = body.state});
  }
};

World::World(const physics::StaminaConfig& stamina_config, const std::string& script_path)
    : impl_(std::make_unique<Impl>(stamina_config, script_path)) {}

World::~World() = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->physics.AddCollisionMesh(mesh);
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

void World::AddPlayer(PlayerId player, const math::Vec3& spawn) {
  Impl& impl = *impl_;
  if (impl.players.contains(player)) {
    return;
  }
  const physics::BodyHandle body = impl.physics.CreateBody(spawn);
  physics::BodyState initial{};
  initial.position = spawn;
  const flecs::entity entity =
      impl.ecs.entity().set<Player>({.id = player}).set<Body>({.handle = body, .state = initial}).set<Intent>({});
  impl.players.emplace(player, Impl::Slot{.entity = entity, .body = body});
}

void World::RemovePlayer(PlayerId player) {
  Impl& impl = *impl_;
  const auto slot = impl.players.find(player);
  if (slot == impl.players.end()) {
    return;
  }
  impl.physics.DestroyBody(slot->second.body);
  slot->second.entity.destruct();
  impl.players.erase(slot);
}

State World::Tick(const std::vector<PlayerCommand>& commands, float delta_time) {
  Impl& impl = *impl_;
  impl.tick_commands.clear();
  for (const PlayerCommand& entry : commands) {
    impl.tick_commands[entry.player] = entry.command;
  }
  impl.committed.players.clear();
  impl.ecs.progress(delta_time);
  // The ECS visits players in storage order; the state is ordered by id.
  std::ranges::sort(impl.committed.players,
                    [](const PlayerState& a, const PlayerState& b) { return a.player < b.player; });
  return impl.committed;
}

}  // namespace augusta::simulation
