#include "augusta/animation.h"

#include <vector>

#include "augusta/assets.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"

namespace augusta::animation {

namespace {

void WorldAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose, std::vector<math::Mat4>& world) {
  const math::Mat4 root = hitbox::RootOf(pose);
  world.clear();
  world.reserve(skeleton.rest_transforms.size());
  for (const math::Mat4& rest : skeleton.rest_transforms) {
    world.push_back(root * rest);
  }
}

}  // namespace

void PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose, SkeletonPose& posed) {
  posed.root = pose;
  posed.local.clear();
  posed.local.reserve(skeleton.joints.size());
  for (const assets::JointData& joint : skeleton.joints) {
    posed.local.push_back(JointPose{.translation = joint.translation, .rotation = joint.rotation});
  }
  WorldAtRest(skeleton, pose, posed.world);
}

std::vector<math::Mat4> PoseAtRest(const assets::Skeleton& skeleton, const hitbox::Pose& pose) {
  std::vector<math::Mat4> world;
  WorldAtRest(skeleton, pose, world);
  return world;
}

math::Mat4 Compose(const math::Mat4& parent, const JointPose& local, const math::Vec3& scale) {
  return parent * math::ToMat4(local.translation, local.rotation, scale);
}

}  // namespace augusta::animation
