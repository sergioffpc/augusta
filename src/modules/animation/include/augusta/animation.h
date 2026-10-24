#ifndef AUGUSTA_ANIMATION_H_
#define AUGUSTA_ANIMATION_H_

#include <vector>

#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"

/// \file
/// augusta::animation poses a Character's Skeleton (GLOSSARY.md, ADR-0061):
/// from a player's Hitbox pose, the world transform of each of its Joints, which
/// gameplay's hitbox::Place places the Hitboxes bound to them by. Until a
/// Character has Clips the pose is the Rest pose, with the stance and the facing
/// carried by the Skeleton's root exactly as ADR-0040 has them (hitbox::RootOf).
/// Pure, with no world or state of its own: which players are posed, when, and
/// what a posed Hitbox is judged against are SimulationWorld's, its one
/// consumer, in its Posing phase.
namespace augusta::animation {

/// Poses skeleton at pose in the Rest pose into joints, index for index with
/// skeleton.joints: each Joint's Rest transform in the character's space, put
/// where pose puts the character's root. joints is overwritten, so a caller
/// posing every tick keeps its storage.
void PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose, std::vector<math::Mat4>& joints);

/// As PoseAtRest, into a new vector.
[[nodiscard]] std::vector<math::Mat4> PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose);

}  // namespace augusta::animation

#endif  // AUGUSTA_ANIMATION_H_
