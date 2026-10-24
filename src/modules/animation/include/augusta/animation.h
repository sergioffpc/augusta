#ifndef AUGUSTA_ANIMATION_H_
#define AUGUSTA_ANIMATION_H_

#include <vector>

#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"

/// \file
/// augusta::animation poses a Character's Skeleton (GLOSSARY.md, ADR-0061):
/// from a player's Hitbox pose, each of its Joints relative to its parent and
/// the world transform that composes, which gameplay's hitbox::Place places the
/// Hitboxes bound to them by. Until a Character has Clips the pose is the Rest
/// pose, with the stance and the facing carried by the Skeleton's root exactly
/// as ADR-0040 has them (hitbox::RootOf). Pure, with no world or state of its
/// own: which players are posed, when, and what a posed Hitbox is judged
/// against are SimulationWorld's, its one consumer, in its Posing phase.
namespace augusta::animation {

/// One Joint of a pose, relative to its parent, or to the Character's root for
/// a root Joint: where it is and how it is turned. Its scale is its Skeleton's
/// (assets::JointData): nothing poses a scale.
struct JointPose {
  math::Vec3 translation{0.0F, 0.0F, 0.0F};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
};

/// A Character's Skeleton posed: where the Hitbox pose puts its root, and each
/// Joint, index for index with the Skeleton's, both relative to its parent and
/// as the world transform that composes.
struct SkeletonPose {
  hitbox::Pose root{};
  std::vector<JointPose> local;
  std::vector<math::Mat4> world;
};

/// Poses skeleton at pose in the Rest pose into posed: each Joint as the
/// Skeleton holds it, and its Rest transform in the character's space put where
/// pose puts the character's root. posed is overwritten, so a caller posing
/// every tick keeps its storage.
void PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose, SkeletonPose& posed);

/// The world transforms of PoseAtRest alone, into a new vector.
[[nodiscard]] std::vector<math::Mat4> PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose);

/// The world transform of a Joint posed at local with scale, under a parent
/// whose world transform is parent, or under hitbox::RootOf for a root Joint:
/// how a Skeleton's Joints compose, parent before child.
[[nodiscard]] math::Mat4 Compose(const math::Mat4& parent, const JointPose& local, const math::Vec3& scale);

}  // namespace augusta::animation

#endif  // AUGUSTA_ANIMATION_H_
