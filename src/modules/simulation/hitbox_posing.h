#ifndef AUGUSTA_SIMULATION_HITBOX_POSING_H_
#define AUGUSTA_SIMULATION_HITBOX_POSING_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"

/// \file
/// Hitbox posing (ADR-0044): placing players' hitboxes where a bullet judges
/// them, with gameplay's shared placing (augusta/hitbox.h), for
/// ballistics::World::Step to test it against, culled by each Character's reach
/// and numbered as Surfaces, and reading back which player and BodyPart a struck
/// one is. Pure - no ECS, no physics World, no clock - and private to the
/// simulation module: SimulationWorld picks the players and their poses, and
/// decides what a strike means.
namespace augusta::simulation {

/// The Surface id every mesh of the Map is added with: the whole Map is one
/// Surface, reserved for it, so a strike on it is never read as anything else.
inline constexpr physics::SurfaceId kMapSurface{0};

/// How far from its root a character's hitboxes reach in each stance, posed in
/// the Rest pose (ADR-0061): a bullet that passes farther from where a pose puts
/// its root crosses none of them. Measured once, when the character is added.
class Reach {
 public:
  /// How far hitboxes, each in the space of its Joint of skeleton, reach in the
  /// Rest pose in each stance, plus a margin.
  Reach(const assets::Skeleton& skeleton, std::span<const hitbox::CharacterHitbox> hitboxes);
  /// Reaching nowhere: what a character with no hitboxes, which cannot be hit,
  /// would reach, but for the margin.
  Reach() = default;

  /// How far they reach in stance: as far as the root lowers them with it.
  [[nodiscard]] float In(physics::Stance stance) const { return by_stance_.at(std::to_underlying(stance)); }

 private:
  // Index for index with physics::Stance's values.
  std::array<float, 3> by_stance_{};
};

/// A player a bullet is tested against, and the pose its hitboxes are placed by.
struct PosedTarget {
  EntityId target{};
  hitbox::Pose pose{};
};

/// What a strike on a posed hitbox means: whose it is, and which part.
struct StruckHitbox {
  EntityId target{};
  body_part::BodyPart part = body_part::BodyPart::kTorso;
};

/// The hitboxes of one tick's bullets, posed. Each player's hitboxes are posed
/// once per moment a bullet of the tick judges them at, and kept for every
/// other bullet judged at that moment: nothing a pose is taken from changes
/// while the tick's bullets fly. Posed hitboxes are numbered as Surfaces from
/// one past kMapSurface, so none shares the Map's.
class HitboxPosing {
 public:
  /// Forgets every posing: called at the start of each tick.
  void Clear();

  /// Starts the hitboxes one bullet is tested against, judged at time, a
  /// moment counted in ticks, along segment, its path this tick.
  void BeginBullet(double time, const ballistics::Segment& segment);

  /// Adds target's hitboxes, each in the space of its Joint and reaching reach
  /// from its root at pose, placed by joints, each Joint's world transform at
  /// pose, to the bullet's, if its segment passes near enough to cross them.
  void Consider(EntityId target, std::span<const hitbox::CharacterHitbox> hitboxes, float reach,
                const hitbox::Pose& pose, std::span<const math::Mat4> joints);

  /// The bullet's hitboxes, each with its Surface id. Valid until the next
  /// BeginBullet or Clear.
  [[nodiscard]] std::span<const ballistics::Hitbox> BulletHitboxes();

  /// The players whose hitboxes the bullet is tested against, in the order
  /// they were considered, with their poses. Valid until the next BeginBullet
  /// or Clear.
  [[nodiscard]] std::span<const PosedTarget> BulletTargets() const { return bullet_targets_; }

  /// What a strike on surface, a Surface of this tick's posed hitboxes and not
  /// kMapSurface, means. Any other is a broken id, checked rather than read
  /// past the posed hitboxes.
  [[nodiscard]] StruckHitbox Struck(physics::SurfaceId surface) const;

 private:
  // One posed hitbox: its triangles are count of the posed triangles, from first.
  struct PosedHitbox {
    StruckHitbox struck;
    std::size_t first = 0;
    std::size_t count = 0;
  };

  // One player's hitboxes posed at one moment, counted in ticks.
  struct PosingKey {
    EntityId target{};
    double time = 0.0;

    bool operator==(const PosingKey&) const = default;
  };

  struct PosingKeyHash {
    std::size_t operator()(const PosingKey& key) const noexcept {
      return std::hash<double>{}(key.time) ^ std::hash<std::uint32_t>{}(std::to_underlying(key.target));
    }
  };

  // Where one posing's hitboxes are: count of the posed hitboxes, from first.
  struct Posing {
    std::size_t first = 0;
    std::size_t count = 0;
  };

  // Every posing of the tick.
  std::vector<ballistics::Triangle> posed_triangles_;
  std::vector<PosedHitbox> posed_hitboxes_;
  std::unordered_map<PosingKey, Posing, PosingKeyHash> posings_;
  // The bullet being judged, the posings it is tested against and their
  // hitboxes. Kept between bullets and ticks for their storage only.
  double bullet_time_ = 0.0;
  ballistics::Segment bullet_segment_{};
  std::vector<Posing> bullet_posings_;
  std::vector<PosedTarget> bullet_targets_;
  std::vector<ballistics::Hitbox> bullet_hitboxes_;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_HITBOX_POSING_H_
