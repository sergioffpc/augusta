#ifndef AUGUSTA_SIMULATION_COMPONENTS_H_
#define AUGUSTA_SIMULATION_COMPONENTS_H_

#include <memory>
#include <optional>
#include <vector>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/clips.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "augusta/weapon.h"
#include "hitbox_history.h"
#include "hitbox_posing.h"

/// \file
/// The Flecs components of SimulationWorld's entities (ADR-0001, ADR-0023):
/// what each player and each bullet in flight carries, which the phases' systems
/// read and write. Private to the simulation module: Flecs is encapsulated
/// behind World::Impl, so no other module sees an entity or its components.
namespace augusta::simulation {

/// A player-controlled entity's components: the entity it is, which commands name.
struct Player {
  EntityId entity{};
};

struct Body {
  physics::BodyHandle handle{};
  physics::BodyState state{};
};

/// What CommandIngestion last decided the player is trying to do; Movement acts on it.
struct Intent {
  physics::MovementInput input{};
};

/// Where a player's body faces and looks: its last command's view, on its grid,
/// and whether that command aimed down sights.
struct Facing {
  float yaw = 0.0F;
  float pitch = 0.0F;
  bool ads = false;
};

/// What is left of a player's health, 0 or more.
struct Health {
  float value = 0.0F;
};

/// A player's character's hitboxes, each in the space of its Joint.
struct Hitboxes {
  std::vector<hitbox::CharacterHitbox> bound;
  /// How far from its root they reach in a pose: a bullet that passes farther
  /// from where the pose puts its root crosses none of them.
  Reach reach;
};

/// A player's character's Skeleton, which its hitboxes are bound to.
struct CharacterSkeleton {
  assets::Skeleton skeleton;
};

/// A player's Hitbox pose as Posing evaluated it this tick: its root, and each
/// Joint relative to its parent and as its world transform, index for index with
/// its Skeleton's Joints.
struct PosedJoints {
  animation::SkeletonPose posed;
};

/// A player's Hitbox history: its Joints as Commit recorded each of its recent ticks.
struct HitboxHistory {
  PoseHistory poses;
};

/// A player's rifle, as WeaponHandling left it.
struct Rifle {
  weapon::State state{};
};

/// A player's character's Clips (Character::clips).
struct Clips {
  std::shared_ptr<const animation::CharacterClips> clips;
};

/// Where a player's character sees from standing, relative to its feet (ADR-0040).
struct Eye {
  math::Vec3 standing{};
};

/// Where a player's rifle is, as Posing last placed it; nullopt until it has.
struct Held {
  std::optional<HeldRifle> rifle;
};

/// A bullet in flight: an entity of its own, from the tick it is fired on until
/// the tick ballistics::World resolves it.
struct Bullet {
  ballistics::BulletHandle handle{};
  EntityId shooter{};
  /// Its Shooter's delay (ADR-0044), in ticks: how long before each tick of its
  /// flight the players it is judged against were where it judges them.
  float shooters_delay = 0.0F;
  /// Where it was fired for, as its Shot's yaw and pitch: what a Death it deals tells.
  float yaw = 0.0F;
  float pitch = 0.0F;
};

#if AUGUSTA_DEBUG_TOOLS
/// What a bullet fired while Shot judgements are on records of its flight: its
/// Shot judgement, as far as the flight has gone.
struct ShotRecord {
  ShotJudgement judgement;
};
#endif

/// A bullet that struck a player this tick, for Damage to resolve.
struct PlayerHit {
  EntityId shooter{};
  EntityId target{};
  float yaw = 0.0F;
  float pitch = 0.0F;
  body_part::BodyPart part = body_part::BodyPart::kTorso;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_COMPONENTS_H_
