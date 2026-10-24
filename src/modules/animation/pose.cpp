#include "augusta/pose.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/internal/simd_math_config.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_float.h>
#include <ozz/base/maths/soa_quaternion.h>
#include <ozz/base/maths/soa_transform.h>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/command.h"
#include "augusta/hitbox.h"
#include "augusta/locomotion.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::animation {

namespace {

// The ratio of a Foot contact of clip, nullopt if it has none for foot.
std::optional<float> ContactOf(const Clip& clip, assets::Foot foot) {
  for (const assets::FootContact& contact : clip.foot_contacts) {
    if (contact.foot == foot) {
      return contact.ratio;
    }
  }
  return std::nullopt;
}

// Where clip is at stride, as a ratio of its duration: the Stride's cycle
// mapped onto the Clip's, its first half from the left foot's contact to the
// right's and its second back to the left's, so every Clip lands a foot at the
// same Stride. A Clip without both contacts (an idle) is held at its start.
float RatioAt(const Clip& clip, std::uint8_t stride) {
  const std::optional<float> left = ContactOf(clip, assets::Foot::kLeft);
  const std::optional<float> right = ContactOf(clip, assets::Foot::kRight);
  if (!left || !right) {
    return 0.0F;
  }
  const float cycle = static_cast<float>(stride) / static_cast<float>(physics::kStrideSteps);
  const float left_to_right = std::fmod(*right - *left + 1.0F, 1.0F);
  const float from_left =
      cycle < 0.5F ? cycle * 2.0F * left_to_right : left_to_right + ((cycle - 0.5F) * 2.0F * (1.0F - left_to_right));
  return std::fmod(*left + from_left, 1.0F);
}

// Samples clip at ratio into output, with context.
void Sample(const Clip& clip, float ratio, ozz::animation::SamplingJob::Context& context,
            std::vector<ozz::math::SoaTransform>& output) {
  ozz::animation::SamplingJob sampling;
  sampling.animation = &clip.animation;
  sampling.context = &context;
  sampling.ratio = ratio;
  sampling.output = {output.data(), output.size()};
  [[maybe_unused]] const bool sampled = sampling.Run();
  // LoadClips checked every Clip's tracks against the Skeleton.
  assert(sampled);
}

// Turns pose into what it adds onto reference, the way ozz-animation's additive
// layers take it: its translation less reference's, its rotation after
// reference's undone, its scale over reference's.
void ToDifference(std::span<const ozz::math::SoaTransform> reference, std::span<ozz::math::SoaTransform> pose) {
  for (std::size_t block = 0; block < pose.size(); ++block) {
    const ozz::math::SoaTransform& from = reference[block];
    ozz::math::SoaTransform& each = pose[block];
    each.translation = each.translation - from.translation;
    each.rotation = ozz::math::Conjugate(from.rotation) * each.rotation;
    each.scale = each.scale / from.scale;
  }
}

// One SoA block's lanes of a SIMD value, read with ozz's own unaligned store.
std::array<float, 4> LanesOf(const ozz::math::SimdFloat4& value) {
  std::array<float, 4> lanes{};
  ozz::math::StorePtrU(value, lanes.data());
  return lanes;
}

}  // namespace

// The Clips an Aim offset and an ADS grip sample: the level aim, which the
// others add their difference from, the lowest or the highest, and the grip.
enum AimSlot : std::uint8_t { kLevel, kPitched, kGrip, kAimSlots };

struct Poser::Buffers {
  // One per Clip a blend space blends at once: each samples one of them, so a
  // Clip sampled tick after tick keeps its context's cache.
  std::array<ozz::animation::SamplingJob::Context, kMaxBlendedClips> contexts;
  std::array<std::vector<ozz::math::SoaTransform>, kMaxBlendedClips> sampled;
  std::vector<ozz::math::SoaTransform> blended;
  // One per Clip of a stance's Aim offset and ADS grip, by AimSlot.
  std::array<ozz::animation::SamplingJob::Context, kAimSlots> aim_contexts;
  std::array<std::vector<ozz::math::SoaTransform>, kAimSlots> aim_sampled;
  // The blended pose with them added.
  std::vector<ozz::math::SoaTransform> aimed;

  // Fits every buffer to skeleton.
  void Fit(const ozz::animation::Skeleton& skeleton) {
    const auto soa_joints = static_cast<std::size_t>(skeleton.num_soa_joints());
    for (std::size_t slot = 0; slot < kMaxBlendedClips; ++slot) {
      if (contexts.at(slot).max_tracks() < skeleton.num_joints()) {
        contexts.at(slot).Resize(skeleton.num_joints());
      }
      sampled.at(slot).resize(soa_joints);
    }
    blended.resize(soa_joints);
    for (std::size_t slot = 0; slot < kAimSlots; ++slot) {
      if (aim_contexts.at(slot).max_tracks() < skeleton.num_joints()) {
        aim_contexts.at(slot).Resize(skeleton.num_joints());
      }
      aim_sampled.at(slot).resize(soa_joints);
    }
    aimed.resize(soa_joints);
  }

  // Adds stance's Aim offset at pitch, and its ADS grip if ads, onto pose, into
  // aimed: the lowest or highest aim's difference from the level one, weighted
  // by how far pitch is toward it, and the grip's whole.
  void AddAim(const ozz::animation::Skeleton& skeleton, const StanceClips& stance, float pitch, bool ads,
              std::span<const ozz::math::SoaTransform> pose) {
    const float toward = std::min(std::fabs(pitch) / command::kMaxLookPitch, 1.0F);
    std::array<ozz::animation::BlendingJob::Layer, 2> additive{};
    std::size_t count = 0;
    if (toward > 0.0F || ads) {
      Sample(stance.aim_center, 0.0F, aim_contexts[kLevel], aim_sampled[kLevel]);
    }
    if (toward > 0.0F) {
      Sample(pitch < 0.0F ? stance.aim_down : stance.aim_up, 0.0F, aim_contexts[kPitched], aim_sampled[kPitched]);
      ToDifference(aim_sampled[kLevel], aim_sampled[kPitched]);
      additive.at(count++) = {.weight = toward,
                              .transform = {aim_sampled[kPitched].data(), aim_sampled[kPitched].size()},
                              .joint_weights = {}};
    }
    if (ads) {
      Sample(stance.ads, 0.0F, aim_contexts[kGrip], aim_sampled[kGrip]);
      ToDifference(aim_sampled[kLevel], aim_sampled[kGrip]);
      additive.at(count++) = {
          .weight = 1.0F, .transform = {aim_sampled[kGrip].data(), aim_sampled[kGrip].size()}, .joint_weights = {}};
    }
    const ozz::animation::BlendingJob::Layer whole{
        .weight = 1.0F, .transform = {pose.data(), pose.size()}, .joint_weights = {}};
    ozz::animation::BlendingJob adding;
    adding.layers = {&whole, 1};
    adding.additive_layers = {additive.data(), count};
    adding.rest_pose = skeleton.joint_rest_poses();
    adding.output = {aimed.data(), aimed.size()};
    [[maybe_unused]] const bool added = adding.Run();
    assert(added);
  }
};

Poser::Poser() : buffers_(std::make_unique<Buffers>()) {}
Poser::~Poser() = default;
Poser::Poser(Poser&&) noexcept = default;
Poser& Poser::operator=(Poser&&) noexcept = default;

void Poser::Pose(const CharacterClips& clips, const assets::Skeleton& skeleton, const PoseInput& input,
                 SkeletonPose& posed) {
  Buffers& buffers = *buffers_;
  buffers.Fit(clips.skeleton);

  // Each Clip of the stance's blend space at the body's velocity, sampled
  // where the Stride puts it, then blended by its weight.
  const LocomotionBlend blend = BlendSpace(clips.Of(input.body.stance), LocalVelocity(input.body.velocity, input.yaw));
  std::array<ozz::animation::BlendingJob::Layer, kMaxBlendedClips> layers{};
  for (std::size_t slot = 0; slot < blend.count; ++slot) {
    const ClipWeight& each = blend.clips.at(slot);
    std::vector<ozz::math::SoaTransform>& sampled = buffers.sampled.at(slot);
    Sample(*each.clip, RatioAt(*each.clip, input.body.stride), buffers.contexts.at(slot), sampled);
    layers.at(slot) = {.weight = each.weight, .transform = {sampled.data(), sampled.size()}, .joint_weights = {}};
  }
  ozz::animation::BlendingJob blending;
  blending.layers = {layers.data(), blend.count};
  blending.rest_pose = clips.skeleton.joint_rest_poses();
  blending.output = {buffers.blended.data(), buffers.blended.size()};
  [[maybe_unused]] const bool blended_ok = blending.Run();
  assert(blended_ok);

  // The stance's Aim offset and ADS grip added on top.
  buffers.AddAim(clips.skeleton, clips.Of(input.body.stance), input.pitch, input.ads, buffers.blended);

  // Each ozz-animation Joint into its Joint of the Skeleton, lane by lane.
  posed.local.resize(skeleton.joints.size());
  const auto joint_count = static_cast<std::size_t>(clips.skeleton.num_joints());
  for (std::size_t block = 0; block * 4 < joint_count; ++block) {
    const ozz::math::SoaTransform& soa = buffers.aimed.at(block);
    const std::array<float, 4> tx = LanesOf(soa.translation.x);
    const std::array<float, 4> ty = LanesOf(soa.translation.y);
    const std::array<float, 4> tz = LanesOf(soa.translation.z);
    const std::array<float, 4> rx = LanesOf(soa.rotation.x);
    const std::array<float, 4> ry = LanesOf(soa.rotation.y);
    const std::array<float, 4> rz = LanesOf(soa.rotation.z);
    const std::array<float, 4> rw = LanesOf(soa.rotation.w);
    for (std::size_t lane = 0; lane < 4 && (block * 4) + lane < joint_count; ++lane) {
      posed.local.at(clips.joints.at((block * 4) + lane)) =
          JointPose{.translation = math::Vec3(tx.at(lane), ty.at(lane), tz.at(lane)),
                    .rotation = math::Quat(rw.at(lane), rx.at(lane), ry.at(lane), rz.at(lane))};
    }
  }

  // Composed from the root down, parents before children.
  posed.root = hitbox::Pose{.position = input.body.position, .yaw = input.yaw, .stance = input.body.stance};
  const math::Mat4 root = hitbox::RootOf(posed.root);
  posed.world.resize(skeleton.joints.size());
  for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
    const assets::JointData& data = skeleton.joints[joint];
    const math::Mat4& parent = data.parent == assets::kJointNoParent ? root : posed.world.at(data.parent);
    posed.world[joint] = Compose(parent, posed.local[joint], data.scale);
  }
}

}  // namespace augusta::animation
