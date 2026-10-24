#ifndef AUGUSTA_SIMULATION_HITBOX_HISTORY_H_
#define AUGUSTA_SIMULATION_HITBOX_HISTORY_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/tick.h"

/// \file
/// The Hitbox history (GLOSSARY.md, ADR-0044): what SimulationWorld keeps of
/// every player's recent ticks, to judge a bullet against the players as its
/// shooter saw them (Lag compensation). Pure - no ECS, no physics, no clock - and
/// private to the simulation module: nothing but SimulationWorld judges a hit.
namespace augusta::simulation {

/// One player's posed Joints of its most recent ticks, each as that tick's
/// Authoritative State posed them: where the Hitbox pose put its root, and each
/// Joint that carries a Hitbox, or is an ancestor of one, relative to its
/// parent. No other Joint is kept, so a tick costs at most assets::kMaxJoints
/// of them.
class PoseHistory {
 public:
  /// A history that keeps the poses of the newest capacity ticks of a
  /// character with skeleton, whose hitboxes are bound to its Joints.
  PoseHistory(std::size_t capacity, const assets::Skeleton& skeleton,
              std::span<const hitbox::CharacterHitbox> hitboxes);
  /// A history that keeps nothing.
  PoseHistory() = default;

  /// Keeps posed as that of tick, the tick after the newest kept; beyond the
  /// capacity, the oldest goes. posed is the character's Skeleton posed, every
  /// Joint of it: only those kept are read.
  void Record(tick::Tick tick, const animation::SkeletonPose& posed);

  /// The Hitbox pose at time, a moment between ticks counted in ticks, with
  /// joints set to each kept Joint's world transform then, index for index with
  /// the Skeleton's Joints (any other is the identity). Between the two ticks
  /// around time, the root's position and yaw are interpolated, the yaw along
  /// the shorter arc, and its stance is the nearer tick's, which is how a client
  /// shows a position and a stance between two updates (presentation's
  /// RemoteInterpolator); each Joint is interpolated relative to its parent,
  /// its position linearly and its rotation along the shorter arc, and composed
  /// from the root down. A time before the oldest tick kept or after the newest
  /// gives that tick's pose. nullopt, and joints untouched, while none is kept.
  [[nodiscard]] std::optional<hitbox::Pose> At(double time, std::vector<math::Mat4>& joints) const;

 private:
  // A Joint the history keeps: its index in the Skeleton, its parent's (or
  // assets::kJointNoParent) and its scale, which no pose changes.
  struct KeptJoint {
    std::uint32_t joint = 0;
    std::uint32_t parent = assets::kJointNoParent;
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
  };

  // The tick count'th oldest of those kept is at, in a ring of capacity_.
  [[nodiscard]] std::size_t SlotOf(std::size_t count) const { return (oldest_slot_ + count) % capacity_; }

  std::size_t capacity_ = 0;
  std::size_t joint_count_ = 0;
  // Parents before children, as in the Skeleton.
  std::vector<KeptJoint> kept_;
  // A ring of capacity_ ticks, from oldest_slot_: each one's root, and its
  // kept_.size() kept Joints, in kept_'s order.
  std::vector<hitbox::Pose> roots_;
  std::vector<animation::JointPose> joints_;
  std::size_t oldest_slot_ = 0;
  std::size_t size_ = 0;
  tick::Tick newest_tick_ = 0;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_HITBOX_HISTORY_H_
