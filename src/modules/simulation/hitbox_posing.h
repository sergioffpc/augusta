#ifndef AUGUSTA_SIMULATION_HITBOX_POSING_H_
#define AUGUSTA_SIMULATION_HITBOX_POSING_H_

#include <array>
#include <cassert>
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

/// How far from its root a character's hitboxes reach in a pose (ADR-0061): a
/// bullet that passes farther from where the pose puts its root crosses none of
/// them. Each Hitbox's farthest point from its Joint is measured once, when the
/// character is added; a pose adds how far it holds each Joint from the root,
/// so the reach holds whatever a Clip does with the Joints.
class Reach {
 public:
  /// Measures hitboxes, each in the space of its Joint.
  explicit Reach(std::span<const hitbox::CharacterHitbox> hitboxes);
  /// Reaching nowhere: what a character with no hitboxes, which cannot be hit,
  /// would reach, but for the margin.
  Reach() = default;

  /// How far the hitboxes reach from root, where a pose puts the character's
  /// root, when joints are each Joint's world transform in it, plus a margin.
  /// Each Hitbox's Joint must be one joints has, as hitbox::Place requires.
  [[nodiscard]] float Of(const math::Vec3& root, std::span<const math::Mat4> joints) const;

 private:
  // One Hitbox's Joint, and how far from the Joint's origin it reaches in the
  // Joint's own space.
  struct Extent {
    std::uint32_t joint = 0;
    float radius = 0.0F;
  };

  std::vector<Extent> extents_;
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
