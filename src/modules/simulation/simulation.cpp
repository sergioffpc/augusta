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
#include <unordered_map>
#include <utility>
#include <vector>

#include <flecs.h>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/hitbox.h"
#include "augusta/limits.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"
#include "hitbox_history.h"
#include "hitbox_posing.h"
#include "policy_bridge.h"

namespace augusta::simulation {

namespace {

// One flecs::entity per Phase value, indexed by Phase's declared order -
// not by its enum value (Phase isn't contiguous-from-zero by design, see
// simulation.h), so PhaseIndex below is the single place that mapping is
// spelled out.
constexpr std::size_t kPhaseCount = 9;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

enum PhaseIndex : std::size_t {
  kCommandIngestion = 0,
  kMovement,
  kWeaponHandling,
  kPosing,
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

// Where a player's body faces and looks: its last command's view, on its grid,
// and whether that command aimed down sights.
struct Facing {
  float yaw = 0.0F;
  float pitch = 0.0F;
  bool ads = false;
};

// What is left of a player's health, 0 or more.
struct Health {
  float value = 0.0F;
};

// A player's character's hitboxes, each in the space of its Joint.
struct Hitboxes {
  std::vector<hitbox::CharacterHitbox> bound;
  // How far from its root they reach in each stance: a bullet that passes
  // farther from where a pose puts its root crosses none of them.
  Reach reach;
};

// A player's character's Skeleton, which its hitboxes are bound to.
struct CharacterSkeleton {
  assets::Skeleton skeleton;
};

// A player's Hitbox pose as Posing evaluated it this tick: its root, and each
// Joint relative to its parent and as its world transform, index for index with
// its Skeleton's Joints.
struct PosedJoints {
  animation::SkeletonPose posed;
};

// A player's Hitbox history: its Joints as Commit recorded each of its recent ticks.
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

// Where a player's rifle is, as Posing last placed it; nullopt until it has.
struct Held {
  std::optional<HeldRifle> rifle;
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

#if AUGUSTA_DEBUG_TOOLS
// What a bullet fired while Shot judgements are on records of its flight: its
// Shot judgement, as far as the flight has gone.
struct ShotRecord {
  ShotJudgement judgement;
};
#endif

// A bullet that struck a player this tick, for Damage to resolve.
struct PlayerHit {
  EntityId shooter{};
  EntityId target{};
  float yaw = 0.0F;
  float pitch = 0.0F;
  body_part::BodyPart part = body_part::BodyPart::kTorso;
};

// Where a player's body is and faces this tick, as the tick's State reports it.
hitbox::Pose PoseOf(const Body& body, const Facing& facing) {
  return hitbox::Pose{.position = body.state.position, .yaw = facing.yaw, .stance = body.state.stance};
}

// kMaxShootersDelay in ticks at tick_rate_hz: not a whole number at every rate.
float MaxShootersDelayTicks(std::uint8_t tick_rate_hz) {
  return std::chrono::duration<float>(kMaxShootersDelay).count() * static_cast<float>(tick_rate_hz);
}

// The Shooter's delay (ADR-0044), in ticks, of a round fired on tick by
// command: from the Seen time command reports to tick, and no more than
// max_delay. The Seen time is only what a client says, so its fraction is held within 0 to 1
// and the whole of it to no newer than the last tick's State, the newest any
// client has been sent.
float ShootersDelay(tick::Tick tick, const command::Command& command, float max_delay) {
  const auto now = static_cast<double>(tick);
  const double reported =
      static_cast<double>(command.seen_tick) + static_cast<double>(std::clamp(command.seen_fraction, 0.0F, 1.0F));
  const double seen = std::min(reported, now - 1.0);
  return std::min(static_cast<float>(now - seen), max_delay);
}

// What damage gives for a hit on part.
float DamageFor(const body_part::Damage& damage, body_part::BodyPart part) {
  switch (part) {
    case body_part::BodyPart::kHead:
      return damage.head;
    case body_part::BodyPart::kTorso:
      return damage.torso;
    case body_part::BodyPart::kLimb:
      return damage.limb;
  }
  std::unreachable();
}

// player as Game policy sees it (ADR-0022).
policy::PlayerView ViewOf(EntityId entity, const PlayerIdentity& identity, bool alive, float health,
                          std::optional<EntityId> killer) {
  return policy::PlayerView{
      .entity = std::to_underlying(entity),
      .session = identity.session,
      .character = identity.character,
      .alive = alive,
      .health = health,
      .killer = killer.transform([](EntityId body) { return std::to_underlying(body); }),
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
  // The markers of the rifle every player holds.
  const weapon::Markers rifle_markers;
  // The Shooter's delay's cap in ticks, and how many ticks of poses a player's
  // Hitbox history keeps for it; neither changes.
  const float max_shooters_delay;
  const std::size_t history_ticks;
  // The tick being run, from 1: the number its State goes out under.
  tick::Tick tick = 0;
  flecs::world ecs;
  physics::World physics;
  ballistics::World ballistics;
  PolicyBridge policy_bridge;
  PhaseEntities phases;
  std::unordered_map<EntityId, Slot> players;
  // Set by Tick for CommandIngestion to read, and filled by Commit and
  // Scripts/Behaviours for Tick to return.
  std::unordered_map<EntityId, command::Command> tick_commands;
  State committed;
  std::vector<policy::PolicyAction> actions;
  std::vector<float> shooters_delays;
  // Every player a bullet may strike, for posing its hitboxes where the bullet
  // judges it to be.
  flecs::query<const Player, const Body, const Hitboxes, const PosedJoints, const HitboxHistory> targets;
  // Their hitboxes, posed for the tick's bullets. Cleared each tick.
  HitboxPosing posing;
  // A target's Joints as the Hitbox history has them where a bullet judges it.
  // Kept between calls for its storage only.
  std::vector<math::Mat4> judged_joints;
  // The players the tick's bullets struck, for Damage. Kept between ticks for
  // its storage only.
  std::vector<PlayerHit> player_hits;
#if AUGUSTA_DEBUG_TOOLS
  // Whether the bullets fired from now on record their Shot judgements.
  bool judge_shots = false;
  // The Shot judgements of the bullets whose flight ended this tick, for Tick to return.
  std::vector<ShotJudgement> shot_judgements;
#endif

  Impl(const parameters::Parameters& params, std::uint8_t tick_rate_hz, policy::Policy game_policy,
       const weapon::Markers& rifle)
      : parameters(params),
        rifle_markers(rifle),
        max_shooters_delay(MaxShootersDelayTicks(tick_rate_hz)),
        history_ticks(HitboxHistoryTicks(tick_rate_hz)),
        physics(params.stamina),
        policy_bridge(std::move(game_policy)),
        targets(ecs.query<const Player, const Body, const Hitboxes, const PosedJoints, const HitboxHistory>()) {
    ChainPhases();
    RegisterSystems();
  }

  // Chains the nine phases in Phase's declared order (ADR-0023): each
  // depends_on the previous one, and the first depends on Flecs's
  // built-in OnUpdate phase, so a single ecs.progress() call runs them
  // in exactly this sequence - the built-in phases around OnUpdate
  // (OnLoad, PostLoad, ...) stay empty and cost nothing.
  void ChainPhases() {
    phases[kCommandIngestion] = ecs.entity("CommandIngestion").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kMovement] = ecs.entity("Movement").add(flecs::Phase).depends_on(phases[kCommandIngestion]);
    phases[kWeaponHandling] = ecs.entity("WeaponHandling").add(flecs::Phase).depends_on(phases[kMovement]);
    phases[kPosing] = ecs.entity("Posing").add(flecs::Phase).depends_on(phases[kWeaponHandling]);
    phases[kBallistics] = ecs.entity("Ballistics").add(flecs::Phase).depends_on(phases[kPosing]);
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
    ecs.system<const Body, const Facing, const CharacterSkeleton, PosedJoints>("PosingSystem")
        .kind(phases[kPosing])
        .each([](const Body& body, const Facing& facing, const CharacterSkeleton& skeleton, PosedJoints& posed) {
          OnPosing(body, facing, skeleton, posed);
        });
    // After PosingSystem, in the same phase: the hand is placed once the body is posed.
    ecs.system<const Body, const Facing, const Eye, const Rifle, Held>("HoldingSystem")
        .kind(phases[kPosing])
        .each([this](const Body& body, const Facing& facing, const Eye& eye, const Rifle& rifle, Held& held) {
          OnHolding(body, facing, eye, rifle, held);
        });
    ecs.system<const Bullet>("BallisticsSystem")
        .kind(phases[kBallistics])
        .each([this](flecs::iter& it, std::size_t row, const Bullet& bullet) {
          OnBallistics(it.delta_time(), it.entity(row), bullet);
        });
    // HitDetection registers no system: its test is folded into
    // BallisticsSystem's Step call (Phase::kHitDetection).
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
    ecs.system<const Player, const Body, const Facing, const Health, const Rifle, const PosedJoints, HitboxHistory>(
           "CommitSystem")
        .kind(phases[kCommit])
        .each([this](const Player& player, const Body& body, const Facing& facing, const Health& health,
                     const Rifle& rifle, const PosedJoints& posed, HitboxHistory& history) {
          PackageState(player, body, facing, health, rifle);
          RecordHitboxHistory(posed, history);
        });
  }

  // A player with no command this tick stops, and keeps the stance it asked
  // for, where it faced and looked, and whether it aimed down sights.
  void OnCommandIngestion(const Player& player, Intent& intent, Facing& facing) {
    const auto command = tick_commands.find(player.entity);
    if (command == tick_commands.end()) {
      intent.input.direction = math::Vec3{};
      intent.input.sprint = false;
      return;
    }
    intent.input = command->second.movement;
    facing.yaw = math::SnapAngle(command->second.yaw);
    facing.pitch = math::SnapAngle(command->second.pitch);
    facing.ads = command->second.ads;
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

  // Poses one player's Skeleton where Movement has just put its body.
  static void OnPosing(const Body& body, const Facing& facing, const CharacterSkeleton& skeleton, PosedJoints& posed) {
    animation::PoseAtRest(skeleton.skeleton, PoseOf(body, facing), posed.posed);
  }

  // Places one player's rifle where a Desktop player holds it, its view turned
  // by its rifle's Recoil offset.
  void OnHolding(const Body& body, const Facing& facing, const Eye& eye, const Rifle& rifle, Held& held) const {
    const math::Vec3 eye_position = body.state.position + physics::LowerToStance(eye.standing, body.state.stance);
    const math::Mat4 hand =
        weapon::DesktopHand(eye_position, facing.yaw + rifle.state.recoil.yaw, facing.pitch + rifle.state.recoil.pitch,
                            facing.ads, parameters.rifle, rifle_markers);
    held.rifle =
        HeldRifle{.muzzle = weapon::PlaceMuzzle(hand, rifle_markers), .sight = weapon::PlaceSight(hand, rifle_markers)};
  }

  // Announces shot in the tick's state and puts its bullet in flight, to be
  // judged against the players as they were delay ticks before each tick of it.
  void Fire(const Shot& shot, float delay) {
    committed.shots.push_back(shot);
    // As a fraction of the cap, so a delay held at it is the cap exactly.
    shooters_delays.push_back(delay / max_shooters_delay * std::chrono::duration<float>(kMaxShootersDelay).count());
    const ballistics::BulletHandle bullet = ballistics.Fire(shot.origin, command::ViewDirection(shot.yaw, shot.pitch),
                                                            parameters.rifle.muzzle_velocity, parameters.ammo.flight);
    [[maybe_unused]] const flecs::entity entity = ecs.entity().set<Bullet>(
        {.handle = bullet, .shooter = shot.shooter, .shooters_delay = delay, .yaw = shot.yaw, .pitch = shot.pitch});
#if AUGUSTA_DEBUG_TOOLS
    if (judge_shots) {
      entity.set<ShotRecord>(
          {.judgement = {.shooter = shot.shooter, .shot_tick = tick, .shooters_delay = delay, .entries = {}}});
    }
#endif
  }

  // The hitboxes of every player but bullet's shooter, which a bullet never
  // hits, each posed as it was bullet's Shooter's delay before this tick, of
  // those segment passes near enough to cross. A player with no tick behind it
  // yet is judged as Posing posed it this tick. Valid until the next call.
  std::span<const ballistics::Hitbox> HitboxesFor(const Bullet& bullet, const ballistics::Segment& segment) {
    const double time = static_cast<double>(tick) - static_cast<double>(bullet.shooters_delay);
    posing.BeginBullet(time, segment);
    targets.each([&](const Player& player, const Body& body, const Hitboxes& hitboxes, const PosedJoints& posed,
                     const HitboxHistory& history) {
      if (player.entity == bullet.shooter) {
        return;
      }
      const std::optional<hitbox::Pose> seen = history.poses.At(time, judged_joints);
      if (!seen) {
        posing.Consider(player.entity, hitboxes.bound, hitboxes.reach.In(body.state.stance), posed.posed.root,
                        posed.posed.world);
        return;
      }
      posing.Consider(player.entity, hitboxes.bound, hitboxes.reach.In(seen->stance), *seen, judged_joints);
    });
    return posing.BulletHitboxes();
  }

  // Advances one bullet a tick, to the nearest of the Map and the other
  // players' hitboxes along it, or to its range.
  void OnBallistics(float delta_time, flecs::entity entity, const Bullet& bullet) {
    const ballistics::StepResult result = ballistics.Step(
        bullet.handle, delta_time, physics, HitboxesFor(bullet, ballistics.NextSegment(bullet.handle, delta_time)));
#if AUGUSTA_DEBUG_TOOLS
    if (auto* record = entity.try_get_mut<ShotRecord>()) {
      Record(record->judgement, result);
    }
#endif
    switch (result.outcome) {
      case ballistics::Outcome::kInFlight:
        ++committed.bullets_in_flight;
        return;
      case ballistics::Outcome::kStruck:
        if (result.surface == kMapSurface) {
          committed.map_impacts.push_back(result.impact_point);
        } else {
          // Step hands back only an id it was given: kMapSurface, which every
          // Map mesh is added with, or one of this Step's posed hitboxes.
          const StruckHitbox struck = posing.Struck(result.surface);
          player_hits.push_back(PlayerHit{.shooter = bullet.shooter,
                                          .target = struck.target,
                                          .yaw = bullet.yaw,
                                          .pitch = bullet.pitch,
                                          .part = struck.part});
        }
        break;
      case ballistics::Outcome::kExpired:
        break;
    }
    // The handle is gone with the bullet (ballistics::World::Step).
    entity.destruct();
  }

#if AUGUSTA_DEBUG_TOOLS
  // Records in judgement the tick its bullet just flew, result, and once that
  // has ended its flight, hands judgement on in the tick's result.
  void Record(ShotJudgement& judgement, const ballistics::StepResult& result) {
    RecordCrossed(judgement);
    if (result.outcome == ballistics::Outcome::kInFlight) {
      return;
    }
    RecordEnd(judgement, result);
    shot_judgements.push_back(std::move(judgement));
  }

  // Records the players this tick's segment of the bullet reached as an entry
  // of judgement, if it reached any: those it was just tested against, as
  // posed for it.
  void RecordCrossed(ShotJudgement& judgement) const {
    const std::span<const PosedTarget> targets = posing.BulletTargets();
    if (targets.empty()) {
      return;
    }
    if (judgement.entries.size() == limits::kMaxShotJudgementEntries) {
      judgement.entries_left_out = true;
      return;
    }
    ShotJudgementEntry entry{.ticks_after_shot = static_cast<std::uint32_t>(tick - judgement.shot_tick), .players = {}};
    entry.players.reserve(targets.size());
    for (const PosedTarget& target : targets) {
      entry.players.push_back(JudgedPlayer{.entity = target.target,
                                           .position = target.pose.position,
                                           .stance = target.pose.stance,
                                           .yaw = target.pose.yaw});
    }
    std::ranges::sort(entry.players, {}, &JudgedPlayer::entity);
    judgement.entries.push_back(std::move(entry));
  }

  // Records in judgement what ended its flight, result: nothing, unless it struck.
  void RecordEnd(ShotJudgement& judgement, const ballistics::StepResult& result) const {
    if (result.outcome != ballistics::Outcome::kStruck) {
      return;
    }
    judgement.point = result.impact_point;
    if (result.surface == kMapSurface) {
      judgement.struck = Struck::kMap;
      return;
    }
    const StruckHitbox struck = posing.Struck(result.surface);
    judgement.struck = Struck::kHitbox;
    judgement.part = struck.part;
    judgement.target = struck.target;
  }
#endif

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
  // in the world, with who killed it if it died this tick. Players who left
  // are not in the world, so not in it either.
  [[nodiscard]] policy::MatchView TickView() const {
    policy::MatchView view{.player_count = parameters.player_count, .players = {}};
    view.players.reserve(players.size());
    for (const auto& [entity, slot] : players) {
      const auto death = std::ranges::find(committed.deaths, entity, &Death::victim);
      view.players.push_back(
          ViewOf(entity, slot.identity, slot.body.has_value(), slot.entity.get<Health>().value,
                 death == committed.deaths.end() ? std::nullopt : std::optional<EntityId>(death->killer)));
    }
    return view;
  }

  // Calls the rules' on_tick with the Match view while the world has a
  // Match policy has not ended, and records the action it takes, if any, in
  // the tick's result.
  void OnScriptsBehaviours() {
    if (players.empty() || policy_bridge.MatchEnded()) {
      return;
    }
    if (auto action = policy_bridge.OnTick(TickView(), tick)) {
      actions.push_back(*std::move(action));
    }
  }

  // The Spawn point, from 0 and below spawn_points, each of match_players
  // takes, as Game policy places them at the Match's start.
  std::vector<std::size_t> AssignSpawns(std::span<const MatchPlayer> match_players, std::size_t spawn_points) {
    policy::MatchView view{.player_count = parameters.player_count, .players = {}};
    view.players.reserve(match_players.size());
    for (const MatchPlayer& player : match_players) {
      view.players.push_back(ViewOf(player.entity, player.identity, true, parameters.starting_health, std::nullopt));
    }
    return policy_bridge.AssignSpawns(view, spawn_points, tick);
  }

  // Commit, first: packages one living player into the tick's State.
  void PackageState(const Player& player, const Body& body, const Facing& facing, const Health& health,
                    const Rifle& rifle) {
    committed.bodies.push_back(EntityState{.entity = player.entity,
                                           .body = body.state,
                                           .yaw = facing.yaw,
                                           .pitch = facing.pitch,
                                           .ads = facing.ads,
                                           .health = health.value,
                                           .rifle = rifle.state});
    committed.alive.push_back(player.entity);
  }

  // Commit, then: keeps one living player's Joints as Posing posed them, where
  // the State puts its body, in its Hitbox history. Every bullet of this tick
  // has been judged by now, and a Shooter's delay is a tick or more, so none of
  // them needs this tick's.
  void RecordHitboxHistory(const PosedJoints& posed, HitboxHistory& history) const {
    history.poses.Record(tick, posed.posed);
  }
};

World::World(const parameters::Parameters& parameters, std::uint8_t tick_rate_hz, policy::Policy policy,
             const weapon::Markers& rifle)
    : impl_(std::make_unique<Impl>(parameters, tick_rate_hz, std::move(policy), rifle)) {}

World::~World() = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->physics.AddCollisionMesh(mesh, kMapSurface);
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
  const flecs::entity ecs_entity =
      impl.ecs.entity()
          .set<Player>({.entity = entity})
          .set<Body>({.handle = body, .state = initial})
          .set<Intent>({})
          .set<Facing>({})
          .set<Health>({.value = impl.parameters.starting_health})
          .set<Eye>({.standing = character.eye})
          .set<Hitboxes>({.bound = character.hitboxes, .reach = Reach(character.skeleton, character.hitboxes)})
          .set<CharacterSkeleton>({.skeleton = character.skeleton})
          .set<PosedJoints>({})
          .set<HitboxHistory>({.poses = PoseHistory(impl.history_ticks, character.skeleton, character.hitboxes)})
          .set<Rifle>({.state = weapon::Loaded(impl.parameters.rifle)})
          .set<Held>({});
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
  // Game policy places the players that name no spawn of their own, in their order.
  std::vector<MatchPlayer> assigned;
  for (const MatchPlayer& player : players) {
    if (!player.spawn.has_value()) {
      assigned.push_back(player);
    }
  }
  std::vector<std::size_t> indices;
  if (!assigned.empty()) {
    indices = impl.AssignSpawns(assigned, points.size());
  }
  std::vector<math::Vec3> spawned;
  spawned.reserve(players.size());
  std::size_t next = 0;
  for (const MatchPlayer& player : players) {
    spawned.push_back(player.spawn.has_value() ? *player.spawn : points[indices[next++]]);
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
  impl.policy_bridge.ForgetMatchEnd();
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
  impl.shooters_delays.clear();
  impl.player_hits.clear();
#if AUGUSTA_DEBUG_TOOLS
  impl.shot_judgements.clear();
#endif
  impl.posing.Clear();
  impl.ecs.progress(delta_time);
  // The ECS visits players in storage order; the state is ordered by id.
  std::ranges::sort(impl.committed.bodies, {}, &EntityState::entity);
  std::ranges::sort(impl.committed.alive);
  std::ranges::sort(impl.committed.deaths, {}, &Death::victim);
  std::ranges::sort(impl.committed.shots, {}, &Shot::shooter);
  std::ranges::stable_sort(impl.committed.hits, {}, &Hit::target);
#if AUGUSTA_DEBUG_TOOLS
  std::ranges::stable_sort(impl.shot_judgements, {}, &ShotJudgement::shooter);
  return TickResult{.state = impl.committed,
                    .actions = impl.actions,
                    .shooters_delays = impl.shooters_delays,
                    .shot_judgements = impl.shot_judgements};
#else
  return TickResult{.state = impl.committed, .actions = impl.actions, .shooters_delays = impl.shooters_delays};
#endif
}

std::optional<HeldRifle> World::HeldRifleOf(EntityId entity) const {
  const auto slot = impl_->players.find(entity);
  if (slot == impl_->players.end() || !slot->second.body.has_value()) {
    return std::nullopt;
  }
  return slot->second.entity.get<Held>().rifle;
}

#if AUGUSTA_DEBUG_TOOLS
void World::JudgeShots(bool on) { impl_->judge_shots = on; }
#endif

}  // namespace augusta::simulation
