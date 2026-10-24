#include "hitbox_history.h"

#include <algorithm>
#include <cmath>
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

namespace augusta::simulation {

namespace {

// Below this fraction of the way from one tick to the next the earlier stance
// is the nearer one, as on the client (presentation's RemoteInterpolator).
constexpr float kMidpointFraction = 0.5F;

}  // namespace

PoseHistory::PoseHistory(std::size_t capacity, const assets::Skeleton& skeleton,
                         std::span<const hitbox::CharacterHitbox> hitboxes)
    : capacity_(capacity), joint_count_(skeleton.joints.size()) {
  // Each Hitbox's Joint and its ancestors, up to a root or to one already marked.
  std::vector<bool> carries(joint_count_, false);
  for (const hitbox::CharacterHitbox& hitbox : hitboxes) {
    for (std::uint32_t joint = hitbox.joint; joint != assets::kJointNoParent && !carries.at(joint);
         joint = skeleton.joints[joint].parent) {
      carries[joint] = true;
    }
  }
  for (std::uint32_t joint = 0; joint < joint_count_; ++joint) {
    if (carries[joint]) {
      const assets::JointData& data = skeleton.joints[joint];
      kept_.push_back(KeptJoint{.joint = joint, .parent = data.parent, .scale = data.scale});
    }
  }
  roots_.resize(capacity_);
  joints_.resize(capacity_ * kept_.size());
}

void PoseHistory::Record(tick::Tick tick, const animation::SkeletonPose& posed) {
  if (capacity_ == 0) {
    return;
  }
  std::size_t slot = 0;
  if (size_ < capacity_) {
    slot = SlotOf(size_);
    ++size_;
  } else {
    slot = oldest_slot_;
    oldest_slot_ = SlotOf(1);
  }
  roots_[slot] = posed.root;
  for (std::size_t kept = 0; kept < kept_.size(); ++kept) {
    joints_[(slot * kept_.size()) + kept] = posed.local.at(kept_[kept].joint);
  }
  newest_tick_ = tick;
}

std::optional<hitbox::Pose> PoseHistory::At(double time, std::vector<math::Mat4>& joints) const {
  if (size_ == 0) {
    return std::nullopt;
  }
  const auto newest = static_cast<double>(newest_tick_);
  const double oldest = newest - static_cast<double>(size_ - 1);
  const double held = std::clamp(time, oldest, newest);
  const double whole = std::floor(held);
  const auto earlier_index = static_cast<std::size_t>(whole - oldest);
  // The newest tick is held at a fraction of 0, so its own pose comes back.
  const std::size_t earlier = SlotOf(earlier_index);
  const std::size_t later = SlotOf(std::min(earlier_index + 1, size_ - 1));
  const auto fraction = static_cast<float>(held - whole);

  const hitbox::Pose& from = roots_[earlier];
  const hitbox::Pose& to = roots_[later];
  const hitbox::Pose root{.position = math::Lerp(from.position, to.position, fraction),
                          .yaw = math::LerpAngle(from.yaw, to.yaw, fraction),
                          .stance = fraction < kMidpointFraction ? from.stance : to.stance};

  joints.assign(joint_count_, math::Mat4(1.0F));
  const math::Mat4 root_world = hitbox::RootOf(root);
  for (std::size_t kept = 0; kept < kept_.size(); ++kept) {
    const KeptJoint& joint = kept_[kept];
    const animation::JointPose& joint_from = joints_[(earlier * kept_.size()) + kept];
    const animation::JointPose& joint_to = joints_[(later * kept_.size()) + kept];
    const animation::JointPose between{
        .translation = math::Lerp(joint_from.translation, joint_to.translation, fraction),
        .rotation = math::Slerp(joint_from.rotation, joint_to.rotation, fraction)};
    // Kept parents before children, so a parent's is already in joints.
    const math::Mat4& parent = joint.parent == assets::kJointNoParent ? root_world : joints[joint.parent];
    joints[joint.joint] = animation::Compose(parent, between, joint.scale);
  }
  return root;
}

}  // namespace augusta::simulation
