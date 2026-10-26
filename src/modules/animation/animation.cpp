#include "augusta/animation.h"

#include <span>
#include <vector>

#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"

namespace augusta::animation {

std::vector<math::Mat4> PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose) {
  const math::Mat4 root = hitbox::RootOf(pose);
  std::vector<math::Mat4> world;
  world.reserve(skeleton.rest_transforms.size());
  for (const math::Mat4& rest : skeleton.rest_transforms) {
    world.push_back(root * rest);
  }
  return world;
}

std::vector<hitbox::PlacedHitbox> PlaceAtRest(const assets::Skeleton& skeleton,
                                              std::span<const hitbox::CharacterHitbox> hitboxes,
                                              const hitbox::Pose& pose) {
  return hitbox::Place(hitboxes, PoseAtRest(skeleton, pose));
}

math::Mat4 Compose(const math::Mat4& parent, const JointPose& local, const math::Vec3& scale) {
  return parent * math::ToMat4(local.translation, local.rotation, scale);
}

}  // namespace augusta::animation
