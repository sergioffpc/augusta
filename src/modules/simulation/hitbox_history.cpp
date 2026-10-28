#include "hitbox_history.h"

#include <algorithm>
#include <cassert>
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
#include "hitbox_posing.h"

namespace augusta::simulation {

namespace {

// Below this fraction of the way from one tick to the next the earlier stance
// is the nearer one, as on the client (presentation's RemoteInterpolator).
constexpr float kMidpointFraction = 0.5F;

// The most scale stretches a length by: its largest component's magnitude.
float LargestOf(const math::Vec3& scale) { return std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}); }

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
  std::vector<std::size_t> kept_of(joint_count_, kNoKeptParent);
  for (std::uint32_t joint = 0; joint < joint_count_; ++joint) {
    if (carries[joint]) {
      const assets::JointData& data = skeleton.joints[joint];
      // Parents before children, so a kept parent is already in bounds_.
      const std::size_t parent = data.parent == assets::kJointNoParent ? kNoKeptParent : kept_of.at(data.parent);
      const float parent_stretch = parent == kNoKeptParent ? 1.0F : bounds_[parent].stretch;
      kept_of[joint] = kept_.size();
      kept_.push_back(KeptJoint{.joint = joint, .parent = data.parent, .scale = data.scale});
      bounds_.push_back(JointBound{.parent = parent,
                                   .stretch = parent_stretch * LargestOf(data.scale),
                                   .frobenius = parent_stretch * math::Length(data.scale)});
    }
  }
  for (const hitbox::CharacterHitbox& hitbox : hitboxes) {
    JointBound& bound = bounds_[kept_of[hitbox.joint]];
    bound.extent = std::max(bound.extent, ExtentOf(hitbox));
  }
  BoundReach();
  roots_.resize(capacity_);
  joints_.resize(capacity_ * kept_.size());
}

// A Joint's origin is its parent's world transform applied to its translation,
// so no farther from its parent's than the parent's stretch times the
// translation's length: summed from the root's, which hitbox::RootOf neither
// scales nor stretches, down the Joint's ancestors. A Hitbox's point is no
// farther from its Joint's origin than the Frobenius bound of the Joint's
// stretch times the Hitbox's extent, as Reach::Of measures it; so neither
// quantity Reach::Of sums exceeds its bound here, for any pose whose Joints
// are each no farther from their parents than a kept tick held them, which
// interpolating two such ticks keeps them. The margin is Reach::Of's, once more
// for the rounding between its sum and this one.
void PoseHistory::BoundReach() {
  float reach = 0.0F;
  for (JointBound& bound : bounds_) {
    const bool root = bound.parent == kNoKeptParent;
    const JointBound* parent = root ? nullptr : &bounds_[bound.parent];
    bound.from_root = root ? bound.from_parent : parent->from_root + (parent->stretch * bound.from_parent);
    reach = std::max(reach, bound.from_root + (bound.extent * bound.frobenius));
  }
  reach_ = reach + kReachMargin + kReachMargin;
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
  // posed is of the Skeleton the kept Joints were chosen from.
  assert(posed.local.size() == joint_count_);
  bool farther = false;
  for (std::size_t kept = 0; kept < kept_.size(); ++kept) {
    const animation::JointPose& local = posed.local[kept_[kept].joint];
    joints_[(slot * kept_.size()) + kept] = local;
    const float from_parent = math::Length(local.translation);
    if (from_parent > bounds_[kept].from_parent) {
      bounds_[kept].from_parent = from_parent;
      farther = true;
    }
  }
  if (farther) {
    BoundReach();
  }
  newest_tick_ = tick;
}

std::optional<hitbox::Pose> PoseHistory::RootAt(double time) const {
  if (size_ == 0) {
    return std::nullopt;
  }
  return Around(time).root;
}

PoseHistory::Moment PoseHistory::Around(double time) const {
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
  return Moment{.earlier = earlier,
                .later = later,
                .fraction = fraction,
                .root = {.position = math::Lerp(from.position, to.position, fraction),
                         .yaw = math::LerpAngle(from.yaw, to.yaw, fraction),
                         .stance = fraction < kMidpointFraction ? from.stance : to.stance}};
}

std::optional<hitbox::Pose> PoseHistory::At(double time, std::vector<math::Mat4>& joints) const {
  if (size_ == 0) {
    return std::nullopt;
  }
  const Moment moment = Around(time);
  joints.assign(joint_count_, math::Mat4(1.0F));
  const math::Mat4 root_world = hitbox::RootOf(moment.root);
  for (std::size_t kept = 0; kept < kept_.size(); ++kept) {
    const KeptJoint& joint = kept_[kept];
    const animation::JointPose& joint_from = joints_[(moment.earlier * kept_.size()) + kept];
    const animation::JointPose& joint_to = joints_[(moment.later * kept_.size()) + kept];
    const animation::JointPose between{
        .translation = math::Lerp(joint_from.translation, joint_to.translation, moment.fraction),
        .rotation = math::Slerp(joint_from.rotation, joint_to.rotation, moment.fraction)};
    // Kept parents before children, so a parent's is already in joints.
    const math::Mat4& parent = joint.parent == assets::kJointNoParent ? root_world : joints[joint.parent];
    joints[joint.joint] = animation::Compose(parent, between, joint.scale);
  }
  return moment.root;
}

}  // namespace augusta::simulation
