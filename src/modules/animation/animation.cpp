#include "augusta/animation.h"

#include <vector>

#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"

namespace augusta::animation {

void PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose, std::vector<math::Mat4>& joints) {
  const math::Mat4 root = hitbox::RootOf(pose);
  joints.clear();
  joints.reserve(skeleton.rest_transforms.size());
  for (const math::Mat4& rest : skeleton.rest_transforms) {
    joints.push_back(root * rest);
  }
}

std::vector<math::Mat4> PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose) {
  std::vector<math::Mat4> joints;
  PoseAtRest(skeleton, pose, joints);
  return joints;
}

}  // namespace augusta::animation
