#ifndef AUGUSTA_SIMULATION_H_
#define AUGUSTA_SIMULATION_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/scripting.h"
#include "augusta/weapon.h"

// augusta::simulation orchestrates SimulationWorld (ADR-0023): the single
// authoritative ECS pipeline, run once per tick on the server's
// Simulation thread (ADR-0005). It composes the mechanism modules that
// already exist (physics, ballistics, scripting) into the eight ordered
// phases ADR-0023 defines (see Phase below); server::Host hands its
// per-tick output to augusta::replication to reach clients
// (ARCHITECTURE.md §5's "SimulationWorld... emits authoritative state
// each tick").
//
// World owns one Flecs world (ADR-0001) internally, entirely
// encapsulated behind Impl (simulation.cpp) - Flecs is this module's
// implementation detail, not part of its public interface, so no flecs
// header leaks in here. Phase's eight values become, in the same order,
// eight dependency-chained flecs::Phase entities, each with one
// registered flecs::system (named "<Phase>System") - see simulation.cpp.
// A player is one entity with a physics body, a rifle, its Character's
// hitboxes, its Hitbox history and its health, and a bullet in flight is one
// entity too.
// CommandIngestion, Movement, WeaponHandling, Damage and Commit act on players
// and Ballistics and HitDetection on bullets, which end on the Map, on a
// player or at their range; Scripts/Behaviours calls the scenario's Game policy.
// What each one does is documented on its Phase enumerator below.
namespace augusta::simulation {

// SimulationWorld's eight phases (ADR-0023), executed in this exact
// order every tick. Scripts/Behaviours runs last, after Damage has
// resolved the tick's deaths, so a hook can react to what just happened
// (e.g. evaluate a win condition) and schedule what follows (spawns,
// Match end) for the next tick.
enum class Phase {
  // Mechanism. Applies this tick's already-validated client commands
  // (augusta::command::Command; Input Validation - US-15 - is a boundary
  // component outside this World, per ARCHITECTURE.md §8, and has
  // already run by the time World::Tick sees them) to their entities.
  kCommandIngestion,
  // Mechanism. PhysX integration, stamina, collision resolution
  // (US-04, US-05) - augusta::physics::World::Step, one call per player
  // body. The same physics::World interface PredictionWorld's Movement
  // phase uses (ARCHITECTURE.md §5).
  kMovement,
  // Mechanism. Aim/ADS, fire, reload, recoil, ammo rules (US-06-US-09) -
  // augusta::weapon::Step, one call per player: the same function
  // PredictionWorld's WeaponHandling phase predicts with (ARCHITECTURE.md
  // §5). Here its result is authoritative: each round fired is a Shot, and a
  // bullet in ballistics::World from this tick on. A Command's fire and reload
  // are only intent: the fire rate, the magazine and the reload are this
  // phase's to keep, whatever a client sends.
  kWeaponHandling,
  // Mechanism. Advances in-flight bullet trajectories (US-10) -
  // augusta::ballistics::World::Step, one call per in-flight bullet.
  kBallistics,
  // Mechanism. Resolves impact point + body part (US-11) against every
  // player's hitboxes, placed by its pose: where its body was, lowered for
  // its stance as its eye is, and turned by its yaw. They are judged where
  // the bullet's shooter saw the players (Lag compensation, ADR-0044): as
  // they were the bullet's Shooter's delay ago, fixed when it was fired and
  // kept for its whole flight, from the Hitbox history. The Map is judged in
  // the present. A bullet is never tested against its own shooter. The test
  // itself is folded into the same ballistics::World::Step call as
  // kBallistics - see ballistics.h - so the hitboxes are posed for each
  // bullet ahead of it, and one Step call returns the nearest of the Map and
  // the players along the tick's segment, with its BodyPart. What this phase
  // runs itself is the Hitbox history: it keeps the pose this tick's State
  // reports of every player, for the bullets of the ticks to come.
  kHitDetection,
  // Mechanism, reads Data/Config. Takes the damage the Parameters give the
  // ammo for the body part hit (US-12) off the target's health, which stops
  // at zero and never regenerates in a Match. The hit that takes it to zero
  // kills its player (US-13) for the rest of the Match: from that tick its
  // body leaves physics and its hitboxes are no longer tested, and
  // CommandIngestion drops its commands, so nothing it sends moves, turns or
  // fires. The bullets it fired while alive fly on. The player stays in the
  // world, bodiless, until it is removed.
  kDamage,
  // Policy, sandboxed Lua (ADR-0022). Win condition, round transitions,
  // spawn logic (US-14, US-03) - augusta::scripting::Engine::Call: the
  // objectives' on_tick hook, handed a read-only view of the tick. A hook
  // that fails, or returns a decision it cannot make, is logged and decides
  // nothing, and the tick goes on. The only phase not implemented in C++.
  kScriptsBehaviours,
  // Mechanism. Packages the tick's resolved state into Authoritative
  // State (State, below), which augusta::replication plans into each
  // client's update and server::Host sends.
  kCommit,
};

/// The server's name for one dynamic body inside SimulationWorld - today a
/// player's, later any that moves. The caller picks it (server::Match hands one
/// to each player at match start) and it is unique among the bodies currently
/// in the world. It names the body, not whoever controls it: a player's
/// session is a different number.
enum class EntityId : std::uint32_t {};

/// The longest Shooter's delay (CONTEXT.md, ADR-0044): a round whose shooter
/// reports an older view is judged against the players as they were this long ago.
inline constexpr std::chrono::milliseconds kMaxShootersDelay{250};

/// How many ticks of every player's poses the Hitbox history (CONTEXT.md)
/// keeps at tick_rate_hz: kMaxShootersDelay in ticks, rounded up, which is all
/// the cap can ask for. 15 at 60 Hz.
[[nodiscard]] std::size_t HitboxHistoryTicks(std::uint8_t tick_rate_hz);

/// The validated command for one tick of the player who controls entity. The
/// view it reports (command::Command) is only what its client says:
/// SimulationWorld holds it within the States it has emitted and within
/// kMaxShootersDelay.
struct PlayerCommand {
  EntityId entity{};
  command::Command command{};
};

/// One hitbox of a Character (CONTEXT.md's Hitbox, ADR-0040): the body part it
/// stands for and its triangles, in the character's root space.
struct CharacterHitbox {
  /// What a bullet that crosses it hits.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
  /// Its surface: a bullet is tested against each, from either side.
  std::vector<ballistics::Triangle> triangles;
};

/// What SimulationWorld needs of the Character a player plays (ADR-0040), in
/// the character's root space: standing, its feet at the origin, facing where a
/// view of yaw 0 looks (command::Command).
struct Character {
  /// Where it sees from, and where its Shots leave from.
  math::Vec3 eye{};
  /// What a bullet that reaches its body is judged against. One without any
  /// cannot be hit.
  std::vector<CharacterHitbox> hitboxes;
};

/// One dynamic body as of the end of a tick.
struct EntityState {
  EntityId entity{};
  physics::BodyState body{};
  /// Where the body faces: the yaw of its player's last Command
  /// (command::Command), in radians, on the grid the Networking Protocol sends
  /// it on (ADR-0038). Its hitboxes turn with it.
  float yaw = 0.0F;
  /// What is left of its player's health: the Parameters' starting health at
  /// Match start, less the damage taken since, never below 0. Its own player
  /// alone is told it (ADR-0038).
  float health = 0.0F;
  /// The rifle of the player who controls it.
  weapon::State rifle{};
};

/// One round a player fired on a tick (CONTEXT.md's Shot, ADR-0044), with its
/// numbers on the grids the Networking Protocol sends them on (ADR-0038), so
/// the round the server fires is exactly the one its clients are told of.
struct Shot {
  /// The body of the player who fired it.
  EntityId shooter{};
  /// Where the round left from: the shooter's eye for its stance.
  math::Vec3 origin{};
  /// Where it left for, as a view's yaw and pitch in radians
  /// (command::ViewDirection gives the direction): the view of the shooter's
  /// Command, turned by its rifle's Recoil offset (weapon::Step).
  float yaw = 0.0F;
  float pitch = 0.0F;
};

/// One bullet that struck a player on a tick (US-11), and what it did (US-12).
struct Hit {
  /// The body of the player who fired the bullet, which may have left the world since.
  EntityId shooter{};
  /// The body it struck: never the shooter's own.
  EntityId target{};
  /// The damage the Parameters give the ammo for part.
  float damage = 0.0F;
  /// The health the target has left after it.
  float health = 0.0F;
  /// Where on the target it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
  /// Whether this is the hit that took the target's health to zero: true of at
  /// most one hit on a player in a Match.
  bool reached_zero = false;
};

/// A player's death (US-13): the Hit that took its health to zero, told with
/// what a ragdoll would start from (ADR-0045). Permanent for the rest of the Match.
struct Death {
  /// The body of the player who died.
  EntityId victim{};
  /// The body of the player who fired the killing bullet, which may have left the world since.
  EntityId killer{};
  /// Where the killing bullet was fired for, as a view's yaw and pitch in
  /// radians: its Shot's, on the angle grid (ADR-0038).
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Where on the victim it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

// SimulationWorld's per-tick output - ADR-0023/ARCHITECTURE.md's
// "Authoritative State", for augusta::replication to send to clients.
// It holds every living player's body, the rounds fired, what became of the
// bullets in flight (the Map impacts and the hits on players), and the
// tick's deaths.
struct State {
  /// Which tick of its World this is the State of, from 1: what a client names
  /// the view its Commands were sampled against by (command::Command).
  std::uint32_t tick = 0;
  /// Every dynamic body in the world, ordered by EntityId. A dead player has none.
  std::vector<EntityState> bodies;
  /// Every player in the world that has not died, ordered by EntityId.
  std::vector<EntityId> alive;
  /// Every player that died this tick, ordered by victim: at most once a player in a Match.
  std::vector<Death> deaths;
  /// Every round fired this tick, ordered by shooter: at most one a player.
  std::vector<Shot> shots;
  /// Where each bullet that struck the Map this tick struck it.
  std::vector<math::Vec3> map_impacts;
  /// Every bullet that struck a player this tick, ordered by target; those on
  /// one target in the order they took its health.
  std::vector<Hit> hits;
  /// How many bullets are still flying after this tick: fired and neither
  /// stopped by the Map nor past the ammo's max range.
  std::uint32_t bullets_in_flight = 0;
};

// The single authoritative SimulationWorld. The server constructs
// exactly one, on the Simulation thread (ADR-0005). Owns the mechanism
// sub-worlds each tick drives through in Phase order, plus the Flecs
// world they run inside of (see header comment); no I/O happens inside
// Tick (ARCHITECTURE.md §8) - augusta::replication, not this class, is
// responsible for getting State to the network.
//
// Move-only: copying would either duplicate or alias the owned Flecs
// world, neither of which is meaningful.
class World {
 public:
  // Constructs an empty World running on parameters (ADR-0039), copied and
  // fixed for its lifetime, and ticking tick_rate_hz times a second, which
  // sets how many ticks kMaxShootersDelay and the Hitbox history span: an
  // empty physics::World (its stamina rules for every player body) and an
  // empty ballistics::World (no bullets in flight yet), policy, the scenario's
  // Game policy (ADR-0022) as the server loaded it from its pack (none by
  // default), and the Flecs world with Phase's eight phases and their systems
  // registered (see header comment).
  World(const parameters::Parameters& parameters, std::uint8_t tick_rate_hz, scripting::Engine policy = {});
  ~World();

  /// Adds immovable level geometry to this world's physics, the same way PredictionWorld does.
  std::expected<void, physics::CollisionMeshError> AddCollisionMesh(const physics::CollisionMesh& mesh);

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  /// Puts a new player-controlled body entity, standing, facing yaw 0, at full
  /// stamina, with the Parameters' starting health and a rifle ready to fire, at
  /// spawn. character is the one its player plays, copied. entity must not
  /// already be in the world.
  void AddPlayer(EntityId entity, const math::Vec3& spawn, const Character& character);

  /// Takes entity's body out of the world; a no-op if it is not in it. The
  /// bullets it fired fly on.
  void RemovePlayer(EntityId entity);

  // Runs all eight Phase values above, in their declared order, for one
  // fixed tick of duration delta_time seconds (internally, one
  // flecs::world::progress(delta_time) call). commands holds this tick's
  // validated input, at most one per player (US-02, 2-8 players) - unlike
  // PredictionWorld, which only ever ticks the local player (see
  // augusta::prediction::World::Tick). A player with no command this tick
  // stops moving, keeps its stance and its facing and does not fire; a reload
  // it had started goes on. A round fired is judged, for its whole flight,
  // against the other players as they were its Shooter's delay ago: from the
  // view its Command reports, held to no newer than the last tick's State and
  // its fraction within 0 to 1, to this tick, and no longer than
  // kMaxShootersDelay. Returns the tick's Authoritative State.
  State Tick(const std::vector<PlayerCommand>& commands, float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_H_
