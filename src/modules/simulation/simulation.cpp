#include "augusta/simulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <flecs.h>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/scripting.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"
#include "hitbox_history.h"

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

// A player's Hitbox history: where the State of each of its recent ticks put it.
struct HitboxHistory {
  PoseHistory poses;
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
  // Its Shooter's delay (ADR-0044), in ticks: how long before each tick of its
  // flight the players it is judged against were where it judges them.
  float shooters_delay = 0.0F;
  // Where it was fired for, as its Shot's yaw and pitch: what a Death it deals tells.
  float yaw = 0.0F;
  float pitch = 0.0F;
};

// One hitbox of one player, where a bullet judges that player to be: its
// triangles are count of the posed triangles, from first.
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
  float yaw = 0.0F;
  float pitch = 0.0F;
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

// point, of a standing character's root space, where it is on a body in pose:
// lowered for its stance as the eye is, turned about the vertical as the view
// is, then moved to the body.
math::Vec3 PosePoint(const math::Vec3& point, const Pose& pose) {
  return pose.position + (command::ViewRotation(pose.yaw, 0.0F) * physics::LowerToStance(point, pose.stance));
}

// Where a player's body is and faces this tick, as the tick's State reports it.
Pose PoseOf(const Body& body, const Facing& facing) {
  return Pose{.position = body.state.position, .yaw = facing.yaw, .stance = body.state.stance};
}

// kMaxShootersDelay in ticks at tick_rate_hz: not a whole number at every rate.
float MaxShootersDelayTicks(std::uint8_t tick_rate_hz) {
  return std::chrono::duration<float>(kMaxShootersDelay).count() * static_cast<float>(tick_rate_hz);
}

// The Shooter's delay (ADR-0044), in ticks, of a round fired on tick by
// command: from the view command reports to tick, and no more than max_delay.
// The view is only what a client says, so its fraction is held within 0 to 1
// and the whole of it to no newer than the last tick's State, the newest any
// client has been sent.
float ShootersDelay(tick::Tick tick, const command::Command& command, float max_delay) {
  const auto now = static_cast<double>(tick);
  const double reported =
      static_cast<double>(command.view_tick) + static_cast<double>(std::clamp(command.view_fraction, 0.0F, 1.0F));
  const double view = std::min(reported, now - 1.0);
  return std::min(static_cast<float>(now - view), max_delay);
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

// The objectives' hook called every tick in Scripts/Behaviours (ADR-0022).
constexpr std::string_view kOnTick = "on_tick";

// How often a failing Game policy hook is logged: one that fails every tick
// would otherwise write a line a tick (ADR-0029).
constexpr std::chrono::seconds kPolicyWarningInterval{1};

// The behaviours' hook called once at Match start (ADR-0022).
constexpr std::string_view kAssignSpawns = "assign_spawns";

// The Spawn point, from 0, each of a Match's players takes with no Game policy
// for it: in order, starting over after the last.
std::vector<std::size_t> InOrderSpawns(std::size_t players, std::size_t spawn_points) {
  std::vector<std::size_t> indices(players);
  for (std::size_t i = 0; i < players; ++i) {
    indices[i] = i % spawn_points;
  }
  return indices;
}

scripting::Field NumberField(std::string key, double value) {
  return scripting::Field{.key = std::move(key), .value = {.data = value}};
}

scripting::Field BoolField(std::string key, bool value) {
  return scripting::Field{.key = std::move(key), .value = {.data = value}};
}

// One player in the Match as Game policy sees it (ADR-0022).
struct ViewedPlayer {
  EntityId entity{};
  PlayerIdentity identity{};
  bool alive = true;
  float health = 0.0F;
  // The body of whoever killed it, only on the tick it was killed.
  std::optional<EntityId> killer;
};

// player as a hook sees it, its fields ordered by key.
scripting::Value PlayerView(const ViewedPlayer& player) {
  scripting::Value::Record record{
      BoolField("alive", player.alive),
      NumberField("character", player.identity.character),
      NumberField("entity", std::to_underlying(player.entity)),
      NumberField("health", player.health),
      BoolField("killed", player.killer.has_value()),
  };
  if (player.killer.has_value()) {
    record.push_back(NumberField("killer", std::to_underlying(*player.killer)));
  }
  record.push_back(NumberField("session", std::to_underlying(player.identity.session)));
  return scripting::Value{.data = std::move(record)};
}

// The read-only view of the Match every Game policy hook is handed (ADR-0022):
// the Player count and every player in the Match, ordered by session. Each hook
// appends what is its own (the tick, the number of Spawn points), keeping the
// keys in order, as Value::Record wants.
scripting::Value::Record MatchView(std::size_t player_count, std::vector<ViewedPlayer> players) {
  std::ranges::sort(players, {}, [](const ViewedPlayer& player) { return player.identity.session; });
  scripting::Value::List roster;
  roster.reserve(players.size());
  for (const ViewedPlayer& player : players) {
    roster.push_back(PlayerView(player));
  }
  return scripting::Value::Record{
      NumberField("player_count", static_cast<double>(player_count)),
      scripting::Field{.key = "players", .value = {.data = std::move(roster)}},
  };
}

}  // namespace

std::size_t HitboxHistoryTicks(std::uint8_t tick_rate_hz) {
  return static_cast<std::size_t>(std::chrono::ceil<std::chrono::seconds>(kMaxShootersDelay * tick_rate_hz).count());
}

struct World::Impl {
  // Where a player lives, for RemovePlayer, and who plays it, for Game policy;
  // a dead player's body has left physics.
  struct Slot {
    flecs::entity entity;
    std::optional<physics::BodyHandle> body;
    PlayerIdentity identity;
  };

  const parameters::Parameters parameters;
  // The Shooter's delay's cap in ticks, and how many ticks of poses a player's
  // Hitbox history keeps for it; neither changes.
  const float max_shooters_delay;
  const std::size_t history_ticks;
  // The tick being run, from 1: the number its State goes out under.
  tick::Tick tick = 0;
  flecs::world ecs;
  physics::World physics;
  ballistics::World ballistics;
  scripting::Engine policy;
  logging::Throttle policy_warnings{kPolicyWarningInterval};
  // Whether policy has ended the Match the world's players are in: it then
  // decides no more until EndMatch.
  bool match_ended_by_policy = false;
  PhaseEntities phases;
  std::unordered_map<EntityId, Slot> players;
  // Set by Tick for CommandIngestion to read, and filled by Commit and
  // Scripts/Behaviours for Tick to return.
  std::unordered_map<EntityId, command::Command> tick_commands;
  State committed;
  std::vector<PolicyAction> actions;
  // Every player a bullet may strike, for posing its hitboxes where the bullet
  // judges it to be.
  flecs::query<const Player, const Body, const Facing, const Hitboxes, const HitboxHistory> targets;
  // The other players' hitboxes as posed for one bullet, and the players the
  // tick's bullets struck, for Damage. Kept between bullets and ticks for their
  // storage only.
  std::vector<ballistics::Triangle> posed_triangles;
  std::vector<PosedHitbox> posed_hitboxes;
  std::vector<ballistics::Hitbox> bullet_hitboxes;
  std::vector<PlayerHit> player_hits;

  Impl(const parameters::Parameters& params, std::uint8_t tick_rate_hz, scripting::Engine policy_engine)
      : parameters(params),
        max_shooters_delay(MaxShootersDelayTicks(tick_rate_hz)),
        history_ticks(HitboxHistoryTicks(tick_rate_hz)),
        physics(params.stamina),
        policy(std::move(policy_engine)),
        targets(ecs.query<const Player, const Body, const Facing, const Hitboxes, const HitboxHistory>()) {
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
  // on Phase's matching enumerator in simulation.h. Scripts/Behaviours uses
  // run() rather than each(): it fires exactly once per Tick regardless of
  // matched entities, since a hook is called once a tick, not once a player.
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
    ecs.system<const Bullet>("BallisticsSystem")
        .kind(phases[kBallistics])
        .each([this](flecs::iter& it, std::size_t row, const Bullet& bullet) {
          OnBallistics(it.delta_time(), it.entity(row), bullet);
        });
    ecs.system<const Body, const Facing, HitboxHistory>("HitDetectionSystem")
        .kind(phases[kHitDetection])
        .each([this](const Body& body, const Facing& facing, HitboxHistory& history) {
          OnHitDetection(body, facing, history);
        });
    // Writes Body and Intent, so a player it kills is out of the State Commit
    // packages on the same tick.
    ecs.system<const Player, Health>("DamageSystem")
        .kind(phases[kDamage])
        .write<Body>()
        .write<Intent>()
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
  // A round leaves from where Movement just put the shooter's eye, for where
  // its rifle points.
  void OnWeaponHandling(float delta_time, const Player& player, const Body& body, const Eye& eye, Rifle& rifle) {
    const auto found = tick_commands.find(player.entity);
    const command::Command command = found == tick_commands.end() ? command::Command{} : found->second;
    const weapon::Result result = weapon::Step(parameters.rifle, rifle.state, command, delta_time);
    rifle.state = result.state;
    if (!result.fired) {
      return;
    }
    const Shot shot{
        .shooter = player.entity,
        .origin = math::SnapPosition(body.state.position + physics::LowerToStance(eye.standing, body.state.stance)),
        .yaw = math::SnapAngle(result.yaw),
        .pitch = math::SnapAngle(result.pitch),
    };
    Fire(shot, ShootersDelay(tick, command, max_shooters_delay));
  }

  // Announces shot in the tick's state and puts its bullet in flight, to be
  // judged against the players as they were delay ticks before each tick of it.
  void Fire(const Shot& shot, float delay) {
    committed.shots.push_back(shot);
    const ballistics::BulletHandle bullet =
        ballistics.Fire(shot.origin, command::ViewDirection(shot.yaw, shot.pitch), parameters.rifle.muzzle_velocity,
                        {.gravity = parameters.ammo.gravity, .max_range = parameters.ammo.max_range});
    ecs.entity().set<Bullet>(
        {.handle = bullet, .shooter = shot.shooter, .shooters_delay = delay, .yaw = shot.yaw, .pitch = shot.pitch});
  }

  // Places hitboxes, target's, where pose puts them.
  void PoseHitboxes(EntityId target, const Hitboxes& hitboxes, const Pose& pose) {
    for (const CharacterHitbox& hitbox : hitboxes.standing) {
      posed_hitboxes.push_back(PosedHitbox{
          .target = target, .part = hitbox.part, .first = posed_triangles.size(), .count = hitbox.triangles.size()});
      for (const ballistics::Triangle& triangle : hitbox.triangles) {
        posed_triangles.push_back(ballistics::Triangle{
            .a = PosePoint(triangle.a, pose), .b = PosePoint(triangle.b, pose), .c = PosePoint(triangle.c, pose)});
      }
    }
  }

  // The hitboxes of every player but bullet's shooter, which a bullet never
  // hits, each posed as it was bullet's Shooter's delay before this tick. A
  // player with no tick behind it yet is judged where it is. Valid until the
  // next call.
  std::span<const ballistics::Hitbox> HitboxesFor(const Bullet& bullet) {
    posed_triangles.clear();
    posed_hitboxes.clear();
    const double time = static_cast<double>(tick) - static_cast<double>(bullet.shooters_delay);
    targets.each([&](const Player& player, const Body& body, const Facing& facing, const Hitboxes& hitboxes,
                     const HitboxHistory& history) {
      if (player.entity == bullet.shooter) {
        return;
      }
      PoseHitboxes(player.entity, hitboxes, history.poses.At(time).value_or(PoseOf(body, facing)));
    });
    // Only now, with every triangle in place, can the hitboxes view them.
    bullet_hitboxes.clear();
    const std::span<const ballistics::Triangle> triangles(posed_triangles);
    for (const PosedHitbox& hitbox : posed_hitboxes) {
      bullet_hitboxes.push_back(ballistics::Hitbox{.target = ToTarget(hitbox.target),
                                                   .part = hitbox.part,
                                                   .triangles = triangles.subspan(hitbox.first, hitbox.count)});
    }
    return bullet_hitboxes;
  }

  // Advances one bullet a tick, to the nearest of the Map and the other
  // players' hitboxes along it, or to its range.
  void OnBallistics(float delta_time, flecs::entity entity, const Bullet& bullet) {
    const ballistics::StepResult result = ballistics.Step(bullet.handle, delta_time, physics, HitboxesFor(bullet));
    switch (result.outcome) {
      case ballistics::Outcome::kInFlight:
        ++committed.bullets_in_flight;
        return;
      case ballistics::Outcome::kHitMap:
        committed.map_impacts.push_back(result.impact_point);
        break;
      case ballistics::Outcome::kHitPlayer:
        player_hits.push_back(PlayerHit{.shooter = bullet.shooter,
                                        .target = FromTarget(result.target),
                                        .yaw = bullet.yaw,
                                        .pitch = bullet.pitch,
                                        .part = result.part});
        break;
      case ballistics::Outcome::kExpired:
        break;
    }
    // The handle is gone with the bullet (ballistics::World::Step).
    entity.destruct();
  }

  // Keeps one player's pose of this tick in its Hitbox history. The test
  // itself is folded into OnBallistics's ballistics::World::Step call - see
  // simulation.h's Phase::kHitDetection doc comment - which has run by now: a
  // Shooter's delay is a tick or more, so no bullet of this tick needs it.
  void OnHitDetection(const Body& body, const Facing& facing, HitboxHistory& history) const {
    history.poses.Record(tick, PoseOf(body, facing));
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
      const bool reached_zero = had_health && health.value <= 0.0F;
      committed.hits.push_back(Hit{.shooter = hit.shooter,
                                   .target = hit.target,
                                   .damage = damage,
                                   .health = health.value,
                                   .part = hit.part,
                                   .reached_zero = reached_zero});
      if (reached_zero) {
        Kill(hit);
      }
    }
  }

  // The player hit took its health to zero: it dies of it. Its body leaves
  // physics now, and loses its Body and Intent when the phase ends: that takes
  // it out of every system that moves it, fires its rifle, tests or records its
  // hitboxes and commits it, and its commands are ingested no more.
  void Kill(const PlayerHit& hit) {
    committed.deaths.push_back(
        Death{.victim = hit.target, .killer = hit.shooter, .yaw = hit.yaw, .pitch = hit.pitch, .part = hit.part});
    Slot& slot = players.at(hit.target);
    physics.DestroyBody(*slot.body);
    slot.body.reset();
    slot.entity.remove<Body>().remove<Intent>();
  }

  // The Match view on_tick is handed, as Damage left it this tick: every player
  // in the world, with who killed it if it died this tick, and the tick.
  // Players who left are not in the world, so not in it either.
  [[nodiscard]] scripting::Value::Record TickView() const {
    std::vector<ViewedPlayer> viewed;
    viewed.reserve(players.size());
    for (const auto& [entity, slot] : players) {
      const auto death = std::ranges::find(committed.deaths, entity, &Death::victim);
      viewed.push_back(ViewedPlayer{
          .entity = entity,
          .identity = slot.identity,
          .alive = slot.body.has_value(),
          .health = slot.entity.get<Health>().value,
          .killer = death == committed.deaths.end() ? std::nullopt : std::optional<EntityId>(death->killer),
      });
    }
    scripting::Value::Record view = MatchView(parameters.player_count, std::move(viewed));
    view.push_back(NumberField("tick", static_cast<double>(tick)));
    return view;
  }

  // The sessions of the players alive in the world.
  [[nodiscard]] std::vector<SessionId> AliveSessions() const {
    std::vector<SessionId> alive;
    for (const auto& [entity, slot] : players) {
      if (slot.body.has_value()) {
        alive.push_back(slot.identity.session);
      }
    }
    return alive;
  }

  // Calls the objectives' on_tick with the Match view while the world has a
  // Match policy has not ended, and records the action it takes, if any, in
  // the tick's result. A hook that fails or decides what it may not is logged
  // and decides nothing; either way the tick goes on.
  void OnScriptsBehaviours() {
    if (players.empty() || match_ended_by_policy) {
      return;
    }
    const auto returned = policy.Call(scripting::Script::kObjectives, kOnTick, TickView());
    if (!returned) {
      LW_LIMITED(policy_warnings,
                 "subsystem=simulationworld event=policy_hook_failed script={} hook={} tick={} error=\"{}\"",
                 scripting::ScriptPath(scripting::Script::kObjectives), kOnTick, tick,
                 scripting::DescribeHookError(returned.error()));
      return;
    }
    const auto action = ReadTickAction(*returned, AliveSessions());
    if (!action) {
      LW_LIMITED(policy_warnings,
                 "subsystem=simulationworld event=policy_decision_refused script={} hook={} tick={} reason=\"{}\"",
                 scripting::ScriptPath(scripting::Script::kObjectives), kOnTick, tick,
                 DescribeActionRefusal(action.error()));
      return;
    }
    if (action->has_value()) {
      match_ended_by_policy = std::holds_alternative<MatchEnd>(**action);
      actions.push_back(**action);
    }
  }

  // The Spawn point, from 0 and below spawn_points, each of match_players takes:
  // what the behaviours' assign_spawns answers, or in order when it is not
  // defined, answers nil or is refused. A refusal is logged; as a hook is
  // called once a Match, it is not limited.
  std::vector<std::size_t> AssignSpawns(std::span<const MatchPlayer> match_players, std::size_t spawn_points) {
    std::vector<ViewedPlayer> viewed;
    viewed.reserve(match_players.size());
    for (const MatchPlayer& player : match_players) {
      viewed.push_back(ViewedPlayer{.entity = player.entity,
                                    .identity = player.identity,
                                    .alive = true,
                                    .health = parameters.starting_health,
                                    .killer = std::nullopt});
    }
    scripting::Value::Record view = MatchView(parameters.player_count, std::move(viewed));
    view.push_back(NumberField("spawn_points", static_cast<double>(spawn_points)));
    const auto answer = policy.Call(scripting::Script::kBehaviours, kAssignSpawns, view);
    if (!answer) {
      LW("subsystem=simulationworld event=policy_hook_failed script={} hook={} tick={} error=\"{}\"",
         scripting::ScriptPath(scripting::Script::kBehaviours), kAssignSpawns, tick,
         scripting::DescribeHookError(answer.error()));
      return InOrderSpawns(match_players.size(), spawn_points);
    }
    if (std::holds_alternative<std::monostate>(answer->data)) {
      return InOrderSpawns(match_players.size(), spawn_points);
    }
    std::vector<SessionId> sessions;
    sessions.reserve(match_players.size());
    for (const MatchPlayer& player : match_players) {
      sessions.push_back(player.identity.session);
    }
    auto assignment = ReadSpawnAssignment(*answer, sessions, spawn_points);
    if (!assignment) {
      LW("subsystem=simulationworld event=policy_decision_refused script={} hook={} tick={} reason=\"{}\"",
         scripting::ScriptPath(scripting::Script::kBehaviours), kAssignSpawns, tick,
         DescribeSpawnRefusal(assignment.error()));
      return InOrderSpawns(match_players.size(), spawn_points);
    }
    return *std::move(assignment);
  }

  void OnCommit(const Player& player, const Body& body, const Facing& facing, const Health& health,
                const Rifle& rifle) {
    committed.bodies.push_back(EntityState{
        .entity = player.entity, .body = body.state, .yaw = facing.yaw, .health = health.value, .rifle = rifle.state});
    committed.alive.push_back(player.entity);
  }
};

World::World(const parameters::Parameters& parameters, std::uint8_t tick_rate_hz, scripting::Engine policy)
    : impl_(std::make_unique<Impl>(parameters, tick_rate_hz, std::move(policy))) {}

World::~World() = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->physics.AddCollisionMesh(mesh);
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

void World::AddPlayer(EntityId entity, const math::Vec3& spawn, const Character& character,
                      const PlayerIdentity& identity) {
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
                                       .set<HitboxHistory>({.poses = PoseHistory(impl.history_ticks)})
                                       .set<Rifle>({.state = weapon::Loaded(impl.parameters.rifle)});
  impl.players.emplace(entity, Impl::Slot{.entity = ecs_entity, .body = body, .identity = identity});
}

void World::RemovePlayer(EntityId entity) {
  Impl& impl = *impl_;
  const auto slot = impl.players.find(entity);
  if (slot == impl.players.end()) {
    return;
  }
  if (slot->second.body.has_value()) {
    impl.physics.DestroyBody(*slot->second.body);
  }
  slot->second.entity.destruct();
  impl.players.erase(slot);
}

std::vector<math::Vec3> World::StartMatch(const std::vector<MatchPlayer>& players,
                                          const std::vector<math::Vec3>& spawn_points) {
  EndMatch();
  Impl& impl = *impl_;
  // A Map without Spawn points spawns everyone at the origin.
  const std::vector<math::Vec3> points = spawn_points.empty() ? std::vector<math::Vec3>{math::Vec3{}} : spawn_points;
  std::vector<math::Vec3> spawned;
  spawned.reserve(players.size());
  for (const std::size_t index : impl.AssignSpawns(players, points.size())) {
    spawned.push_back(points[index]);
  }
  for (std::size_t i = 0; i < players.size(); ++i) {
    AddPlayer(players[i].entity, spawned[i], players[i].character, players[i].identity);
  }
  return spawned;
}

void World::EndMatch() {
  Impl& impl = *impl_;
  while (!impl.players.empty()) {
    RemovePlayer(impl.players.begin()->first);
  }
  impl.ecs.delete_with<Bullet>();
  impl.ballistics = ballistics::World();
  impl.match_ended_by_policy = false;
}

TickResult World::Tick(const std::vector<PlayerCommand>& commands, float delta_time) {
  Impl& impl = *impl_;
  impl.tick_commands.clear();
  for (const PlayerCommand& entry : commands) {
    impl.tick_commands[entry.entity] = entry.command;
  }
  ++impl.tick;
  impl.committed.tick = impl.tick;
  // Cleared rather than replaced, so a tick reuses the last one's storage.
  impl.committed.bodies.clear();
  impl.committed.alive.clear();
  impl.committed.deaths.clear();
  impl.committed.shots.clear();
  impl.committed.map_impacts.clear();
  impl.committed.hits.clear();
  impl.committed.bullets_in_flight = 0;
  impl.actions.clear();
  impl.player_hits.clear();
  impl.ecs.progress(delta_time);
  // The ECS visits players in storage order; the state is ordered by id.
  std::ranges::sort(impl.committed.bodies, {}, &EntityState::entity);
  std::ranges::sort(impl.committed.alive);
  std::ranges::sort(impl.committed.deaths, {}, &Death::victim);
  std::ranges::sort(impl.committed.shots, {}, &Shot::shooter);
  std::ranges::stable_sort(impl.committed.hits, {}, &Hit::target);
  return TickResult{.state = impl.committed, .actions = impl.actions};
}

}  // namespace augusta::simulation
