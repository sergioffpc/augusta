#include "augusta/pose.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/internal/simd_math_config.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/clips.h"
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

// Where the air Clip is at vertical_speed, as a ratio of its duration: its start
// rising at kAirVerticalSpeed or faster, its end falling as fast, linearly
// between, so the top of a jump is halfway through it.
float AirRatio(float vertical_speed) {
  return std::clamp(0.5F - (vertical_speed / (2.0F * kAirVerticalSpeed)), 0.0F, 1.0F);
}

// One SoA block's lanes of a SIMD value, read with ozz's own unaligned store.
std::array<float, 4> LanesOf(const ozz::math::SimdFloat4& value) {
  std::array<float, 4> lanes{};
  ozz::math::StorePtrU(value, lanes.data());
  return lanes;
}

}  // namespace

struct Poser::Buffers {
  // One per Clip a blend space blends at once: each samples one of them, so a
  // Clip sampled tick after tick keeps its context's cache.
  std::array<ozz::animation::SamplingJob::Context, kMaxBlendedClips> contexts;
  std::array<std::vector<ozz::math::SoaTransform>, kMaxBlendedClips> sampled;
  std::vector<ozz::math::SoaTransform> blended;

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
  }

  // clip sampled at ratio into slot's buffer, as a layer of weight to blend.
  ozz::animation::BlendingJob::Layer Sample(std::size_t slot, const Clip& clip, float ratio, float weight) {
    std::vector<ozz::math::SoaTransform>& output = sampled.at(slot);
    ozz::animation::SamplingJob sampling;
    sampling.animation = &clip.animation;
    sampling.context = &contexts.at(slot);
    sampling.ratio = ratio;
    sampling.output = {output.data(), output.size()};
    [[maybe_unused]] const bool sampled_ok = sampling.Run();
    // LoadClips checked every Clip's tracks against the Skeleton.
    assert(sampled_ok);
    return {.weight = weight, .transform = {output.data(), output.size()}, .joint_weights = {}};
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

  std::array<ozz::animation::BlendingJob::Layer, kMaxBlendedClips> layers{};
  std::size_t layer_count = 0;
  if (input.body.grounded) {
    // Each Clip of the stance's blend space at the body's velocity, sampled
    // where the Stride puts it, then blended by its weight.
    const LocomotionBlend blend =
        BlendSpace(clips.Of(input.body.stance), LocalVelocity(input.body.velocity, input.yaw));
    for (std::size_t slot = 0; slot < blend.count; ++slot) {
      const ClipWeight& each = blend.clips.at(slot);
      layers.at(slot) = buffers.Sample(slot, *each.clip, RatioAt(*each.clip, input.body.stride), each.weight);
    }
    layer_count = blend.count;
  } else {
    // Off the ground, the air Clip alone in its place, from the tick the body
    // leaves the ground to the tick it lands: no crossfade, which would need a
    // time of its own (ADR-0066).
    layers.at(0) = buffers.Sample(0, clips.air, AirRatio(input.body.velocity.y), 1.0F);
    layer_count = 1;
  }
  ozz::animation::BlendingJob blending;
  blending.layers = {layers.data(), layer_count};
  blending.rest_pose = clips.skeleton.joint_rest_poses();
  blending.output = {buffers.blended.data(), buffers.blended.size()};
  [[maybe_unused]] const bool blended_ok = blending.Run();
  assert(blended_ok);

  // Each ozz-animation Joint into its Joint of the Skeleton, lane by lane.
  posed.local.resize(skeleton.joints.size());
  const auto joint_count = static_cast<std::size_t>(clips.skeleton.num_joints());
  for (std::size_t block = 0; block * 4 < joint_count; ++block) {
    const ozz::math::SoaTransform& soa = buffers.blended.at(block);
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
