#include "augusta/simulation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <flecs.h>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/scripting.h"
#include "augusta/weapon.h"

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

// A player-controlled entity's components: the entity it is, which commands name.
struct Player {
  EntityId entity{};
};

struct Body {
  physics::BodyHandle handle{};
  physics::BodyState state{};
};

// What CommandIngestion last decided the player is trying to do; Movement acts on it.
struct Intent {
  physics::MovementInput input{};
};

// Where a player's body faces: the yaw of its last command's view, on its grid.
struct Facing {
  float yaw = 0.0F;
};

// What is left of a player's health, 0 or more.
struct Health {
  float value = 0.0F;
};

// A player's character's hitboxes, as authored: standing, in its root space.
struct Hitboxes {
  std::vector<CharacterHitbox> standing;
};

// A player's rifle, as WeaponHandling left it.
struct Rifle {
  weapon::State state{};
};

// Where a player's character sees from standing, relative to its feet (ADR-0040).
struct Eye {
  math::Vec3 standing{};
};

// A bullet in flight: an entity of its own, from the tick it is fired on until
// the tick ballistics::World resolves it.
struct Bullet {
  ballistics::BulletHandle handle{};
  EntityId shooter{};
};

// One hitbox of one player, where that player is this tick: its triangles are
// count of the tick's posed triangles, from first.
struct PosedHitbox {
  EntityId target{};
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
  std::size_t first = 0;
  std::size_t count = 0;
};

// A bullet that struck a player this tick, for Damage to resolve.
struct PlayerHit {
  EntityId shooter{};
  EntityId target{};
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

// point, of a standing character's root space, where it is on a body at
// position in stance and facing yaw: lowered as the eye is, turned about the
// vertical as the view is, then moved to the body.
math::Vec3 PosePoint(const math::Vec3& point, const physics::BodyState& body, float yaw) {
  return body.position + (command::ViewRotation(yaw, 0.0F) * physics::LowerToStance(point, body.stance));
}

// What damage gives for a hit on part.
float DamageFor(const parameters::Damage& damage, ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return damage.head;
    case ballistics::BodyPart::kTorso:
      return damage.torso;
    case ballistics::BodyPart::kLimb:
      return damage.limb;
  }
  std::unreachable();
}

// Ballistics names the player a hitbox belongs to by the body's own number.
ballistics::TargetId ToTarget(EntityId entity) { return static_cast<ballistics::TargetId>(std::to_underlying(entity)); }

EntityId FromTarget(ballistics::TargetId target) { return static_cast<EntityId>(std::to_underlying(target)); }

}  // namespace

struct World::Impl {
  // Where a player lives, for RemovePlayer.
  struct Slot {
    flecs::entity entity;
    physics::BodyHandle body{};
  };

  const parameters::Parameters parameters;
  flecs::world ecs;
  physics::World physics;
  ballistics::World ballistics;
  scripting::Engine scripting;
  PhaseEntities phases;
  std::unordered_map<EntityId, Slot> players;
  // Set by Tick for CommandIngestion to read, and filled by Commit for Tick to return.
  std::unordered_map<EntityId, command::Command> tick_commands;
  State committed;
  // Every player's hitboxes as posed for this tick's bullets, and the players
  // those bullets struck, for Damage. Kept between ticks for their storage only.
  std::vector<ballistics::Triangle> posed_triangles;
  std::vector<PosedHitbox> posed_hitboxes;
  std::vector<ballistics::Hitbox> bullet_hitboxes;
  std::vector<PlayerHit> player_hits;

  explicit Impl(const parameters::Parameters& params) : parameters(params), physics(params.stamina) {
    ChainPhases();
    RegisterSystems();
  }

  // Chains the eight phases in Phase's declared order (ADR-0023): each
  // depends_on the previous one, and the first depends on Flecs's
  // built-in OnUpdate phase, so a single ecs.progress() call runs them
  // in exactly this sequence - the built-in phases around OnUpdate
  // (OnLoad, PostLoad, ...) stay empty and cost nothing.
  void ChainPhases() {
    phases[kCommandIngestion] = ecs.entity("CommandIngestion").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kMovement] = ecs.entity("Movement").add(flecs::Phase).depends_on(phases[kCommandIngestion]);
    phases[kWeaponHandling] = ecs.entity("WeaponHandling").add(flecs::Phase).depends_on(phases[kMovement]);
    phases[kBallistics] = ecs.entity("Ballistics").add(flecs::Phase).depends_on(phases[kWeaponHandling]);
    phases[kHitDetection] = ecs.entity("HitDetection").add(flecs::Phase).depends_on(phases[kBallistics]);
    phases[kDamage] = ecs.entity("Damage").add(flecs::Phase).depends_on(phases[kHitDetection]);
    phases[kScriptsBehaviours] = ecs.entity("ScriptsBehaviours").add(flecs::Phase).depends_on(phases[kDamage]);
    phases[kCommit] = ecs.entity("Commit").add(flecs::Phase).depends_on(phases[kScriptsBehaviours]);
  }

  // Registers one system per phase, matching the responsibility documented
  // on Phase's matching enumerator in simulation.h, and one more for the
  // hitboxes (below). The stubs use run() rather than each(): it fires
  // exactly once per Tick regardless of matched entities, since their
  // component shapes aren't designed yet (see simulation.h's header
  // comment) - there is nothing to iterate. This only establishes each
  // stub's place in the pipeline.
  void RegisterSystems() {
    ecs.system<const Player, Intent, Facing>("CommandIngestionSystem")
        .kind(phases[kCommandIngestion])
        .each([this](const Player& player, Intent& intent, Facing& facing) {
          OnCommandIngestion(player, intent, facing);
        });
    ecs.system<Body, const Intent>("MovementSystem")
        .kind(phases[kMovement])
        .each([this](flecs::iter& it, std::size_t /*row*/, Body& body, const Intent& intent) {
          OnMovement(it.delta_time(), body, intent);
        });
    // Writes Bullet, so the bullets it fires exist by the time Ballistics runs
    // and fly their first tick on the tick they are fired.
    ecs.system<const Player, const Body, const Eye, Rifle>("WeaponHandlingSystem")
        .kind(phases[kWeaponHandling])
        .write<Bullet>()
        .each([this](flecs::iter& it, std::size_t /*row*/, const Player& player, const Body& body, const Eye& eye,
                     Rifle& rifle) { OnWeaponHandling(it.delta_time(), player, body, eye, rifle); });
    // HitDetection's posing. It runs in the Ballistics phase, ahead of the
    // bullets (systems of one phase run in the order they are declared), since
    // the test is folded into their Step.
    ecs.system<const Player, const Body, const Facing, const Hitboxes>("HitboxPosingSystem")
        .kind(phases[kBallistics])
        .each([this](const Player& player, const Body& body, const Facing& facing, const Hitboxes& hitboxes) {
          OnHitboxPosing(player, body, facing, hitboxes);
        });
    ecs.system<const Bullet>("BallisticsSystem")
        .kind(phases[kBallistics])
        .each([this](flecs::iter& it, std::size_t row, const Bullet& bullet) {
          OnBallistics(it.delta_time(), it.entity(row), bullet);
        });
    ecs.system("HitDetectionSystem").kind(phases[kHitDetection]).run([this](flecs::iter&) { OnHitDetection(); });
    ecs.system<const Player, Health>("DamageSystem")
        .kind(phases[kDamage])
        .each([this](const Player& player, Health& health) { OnDamage(player, health); });
    ecs.system("ScriptsBehavioursSystem").kind(phases[kScriptsBehaviours]).run([this](flecs::iter&) {
      OnScriptsBehaviours();
    });
    ecs.system<const Player, const Body, const Facing, const Health, const Rifle>("CommitSystem")
        .kind(phases[kCommit])
        .each([this](const Player& player, const Body& body, const Facing& facing, const Health& health,
                     const Rifle& rifle) { OnCommit(player, body, facing, health, rifle); });
  }

  // A player with no command this tick stops, and keeps the stance it asked
  // for and where it faced.
  void OnCommandIngestion(const Player& player, Intent& intent, Facing& facing) {
    const auto command = tick_commands.find(player.entity);
    if (command == tick_commands.end()) {
      intent.input.direction = math::Vec3{};
      intent.input.sprint = false;
      return;
    }
    intent.input = command->second.movement;
    facing.yaw = math::SnapAngle(command->second.yaw);
  }

  void OnMovement(float delta_time, Body& body, const Intent& intent) {
    body.state = physics.Step(body.handle, intent.input, delta_time);
  }

  // A player with no command this tick holds nothing: its rifle only waits.
  // A round leaves from where Movement just put the shooter's eye.
  void OnWeaponHandling(float delta_time, const Player& player, const Body& body, const Eye& eye, Rifle& rifle) {
    const auto found = tick_commands.find(player.entity);
    const command::Command command = found == tick_commands.end() ? command::Command{} : found->second;
    const weapon::Result result = weapon::Step(parameters.rifle, rifle.state, command, delta_time);
    rifle.state = result.state;
    if (!result.fired) {
      return;
    }
    Fire(Shot{
        .shooter = player.entity,
        .origin = math::SnapPosition(body.state.position + physics::LowerToStance(eye.standing, body.state.stance)),
        .yaw = math::SnapAngle(command.yaw),
        .pitch = math::SnapAngle(command.pitch),
    });
  }

  // Announces shot in the tick's state and puts its bullet in flight.
  void Fire(const Shot& shot) {
    committed.shots.push_back(shot);
    const ballistics::BulletHandle bullet =
        ballistics.Fire(shot.origin, command::ViewDirection(shot.yaw, shot.pitch), parameters.rifle.muzzle_velocity,
                        {.gravity = parameters.ammo.gravity, .max_range = parameters.ammo.max_range});
    ecs.entity().set<Bullet>({.handle = bullet, .shooter = shot.shooter});
  }

  // Places one player's hitboxes where Movement just put its body.
  void OnHitboxPosing(const Player& player, const Body& body, const Facing& facing, const Hitboxes& hitboxes) {
    for (const CharacterHitbox& hitbox : hitboxes.standing) {
      posed_hitboxes.push_back(PosedHitbox{.target = player.entity,
                                           .part = hitbox.part,
                                           .first = posed_triangles.size(),
                                           .count = hitbox.triangles.size()});
      for (const ballistics::Triangle& triangle : hitbox.triangles) {
        posed_triangles.push_back(ballistics::Triangle{.a = PosePoint(triangle.a, body.state, facing.yaw),
                                                       .b = PosePoint(triangle.b, body.state, facing.yaw),
                                                       .c = PosePoint(triangle.c, body.state, facing.yaw)});
      }
    }
  }

  // The tick's posed hitboxes of every player but shooter: a bullet never hits
  // the player who fired it. Valid until the next call.
  std::span<const ballistics::Hitbox> HitboxesFor(EntityId shooter) {
    bullet_hitboxes.clear();
    const std::span<const ballistics::Triangle> triangles(posed_triangles);
    for (const PosedHitbox& hitbox : posed_hitboxes) {
      if (hitbox.target != shooter) {
        bullet_hitboxes.push_back(ballistics::Hitbox{.target = ToTarget(hitbox.target),
                                                     .part = hitbox.part,
                                                     .triangles = triangles.subspan(hitbox.first, hitbox.count)});
      }
    }
    return bullet_hitboxes;
  }

  // Advances one bullet a tick, to the nearest of the Map and the other
  // players' hitboxes along it, or to its range.
  void OnBallistics(float delta_time, flecs::entity entity, const Bullet& bullet) {
    const ballistics::StepResult result =
        ballistics.Step(bullet.handle, delta_time, physics, HitboxesFor(bullet.shooter));
    switch (result.outcome) {
      case ballistics::Outcome::kInFlight:
        ++committed.bullets_in_flight;
        return;
      case ballistics::Outcome::kHitMap:
        committed.map_impacts.push_back(result.impact_point);
        break;
      case ballistics::Outcome::kHitPlayer:
        player_hits.push_back(
            PlayerHit{.shooter = bullet.shooter, .target = FromTarget(result.target), .part = result.part});
        break;
      case ballistics::Outcome::kExpired:
        break;
    }
    // The handle is gone with the bullet (ballistics::World::Step).
    entity.destruct();
  }

  void OnHitDetection() {
    // Posed by OnHitboxPosing and folded into OnBallistics's
    // ballistics::World::Step call - see simulation.h's Phase::kHitDetection
    // doc comment. Kept as its own phase/system for pipeline ordering.
  }

  // Takes this tick's hits on one player off its health, in the order they struck.
  void OnDamage(const Player& player, Health& health) {
    for (const PlayerHit& hit : player_hits) {
      if (hit.target != player.entity) {
        continue;
      }
      const float damage = DamageFor(parameters.ammo.damage, hit.part);
      const bool had_health = health.value > 0.0F;
      health.value = std::max(health.value - damage, 0.0F);
      committed.hits.push_back(Hit{.shooter = hit.shooter,
                                   .target = hit.target,
                                   .damage = damage,
                                   .health = health.value,
                                   .part = hit.part,
                                   .reached_zero = had_health && health.value <= 0.0F});
    }
  }

  void OnScriptsBehaviours() {
    // TODO(sergioffpc): scripting::Engine::RunHook per relevant hook.
  }

  void OnCommit(const Player& player, const Body& body, const Facing& facing, const Health& health,
                const Rifle& rifle) {
    committed.bodies.push_back(EntityState{
        .entity = player.entity, .body = body.state, .yaw = facing.yaw, .health = health.value, .rifle = rifle.state});
  }
};

World::World(const parameters::Parameters& parameters) : impl_(std::make_unique<Impl>(parameters)) {}

World::~World() = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->physics.AddCollisionMesh(mesh);
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

void World::AddPlayer(EntityId entity, const math::Vec3& spawn, const Character& character) {
  Impl& impl = *impl_;
  if (impl.players.contains(entity)) {
    return;
  }
  const physics::BodyHandle body = impl.physics.CreateBody(spawn);
  physics::BodyState initial{};
  initial.position = spawn;
  const flecs::entity ecs_entity = impl.ecs.entity()
                                       .set<Player>({.entity = entity})
                                       .set<Body>({.handle = body, .state = initial})
                                       .set<Intent>({})
                                       .set<Facing>({})
                                       .set<Health>({.value = impl.parameters.starting_health})
                                       .set<Eye>({.standing = character.eye})
                                       .set<Hitboxes>({.standing = character.hitboxes})
                                       .set<Rifle>({.state = weapon::Loaded(impl.parameters.rifle)});
  impl.players.emplace(entity, Impl::Slot{.entity = ecs_entity, .body = body});
}

void World::RemovePlayer(EntityId entity) {
  Impl& impl = *impl_;
  const auto slot = impl.players.find(entity);
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
    impl.tick_commands[entry.entity] = entry.command;
  }
  // Cleared rather than replaced, so a tick reuses the last one's storage.
  impl.committed.bodies.clear();
  impl.committed.shots.clear();
  impl.committed.map_impacts.clear();
  impl.committed.hits.clear();
  impl.committed.bullets_in_flight = 0;
  impl.posed_triangles.clear();
  impl.posed_hitboxes.clear();
  impl.player_hits.clear();
  impl.ecs.progress(delta_time);
  // The ECS visits players in storage order; the state is ordered by id.
  std::ranges::sort(impl.committed.bodies, {}, &EntityState::entity);
  std::ranges::sort(impl.committed.shots, {}, &Shot::shooter);
  std::ranges::stable_sort(impl.committed.hits, {}, &Hit::target);
  return impl.committed;
}

}  // namespace augusta::simulation
