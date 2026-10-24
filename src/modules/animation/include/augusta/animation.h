#ifndef AUGUSTA_ANIMATION_H_
#define AUGUSTA_ANIMATION_H_

#include <span>
#include <vector>

#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"

/// \file
/// augusta::animation poses a Character's Skeleton (GLOSSARY.md, ADR-0061):
/// from a player's Hitbox pose, each of its Joints relative to its parent and
/// the world transform that composes, which gameplay's hitbox::Place places the
/// Hitboxes bound to them by. The pose is its Clips' (augusta/pose.h); this
/// header holds what a pose is and the Rest pose, which poses a Character
/// without Clips, its root where the position and facing put it
/// (hitbox::RootOf). Pure, with no world or state of its
/// own: which players are posed, when, and what a posed Hitbox is judged
/// against are SimulationWorld's, in its Posing phase. Its one other consumer
/// is a Development build's client (ADR-0074), whose Hitboxes Debug overlay
/// places a Character's Hitboxes in its root space once, when the character
/// loads, by PlaceAtRest; no client World poses a player.
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

/// hitboxes, each in the space of its Joint of skeleton, placed where the Rest
/// pose at pose holds them: hitbox::Place by PoseAtRest's world transforms.
/// Where a Debug overlay draws them.
[[nodiscard]] std::vector<hitbox::PlacedHitbox> PlaceAtRest(const assets::Skeleton& skeleton,
                                                            std::span<const hitbox::CharacterHitbox> hitboxes,
                                                            const hitbox::Pose& pose);

/// The world transform of a Joint posed at local with scale, under a parent
/// whose world transform is parent, or under hitbox::RootOf for a root Joint:
/// how a Skeleton's Joints compose, parent before child.
[[nodiscard]] math::Mat4 Compose(const math::Mat4& parent, const JointPose& local, const math::Vec3& scale);

}  // namespace augusta::animation

#endif  // AUGUSTA_ANIMATION_H_
