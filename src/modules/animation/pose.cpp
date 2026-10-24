#include "augusta/pose.h"

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

// One SoA block's lanes of a SIMD value, read with ozz's own unaligned store.
std::array<float, 4> LanesOf(const ozz::math::SimdFloat4& value) {
  std::array<float, 4> lanes{};
  ozz::math::StorePtrU(value, lanes.data());
  return lanes;
}

}  // namespace

// The stances a pose blends at most: the one a body is in and, while a change
// of stance is under way, the one it left.
constexpr std::size_t kBlendedStances = 2;

struct Poser::Buffers {
  // One per Clip a blend space blends at once, for each stance blended: each
  // samples one of them, so a Clip sampled tick after tick keeps its context's
  // cache.
  std::array<ozz::animation::SamplingJob::Context, kBlendedStances * kMaxBlendedClips> contexts;
  std::array<std::vector<ozz::math::SoaTransform>, kBlendedStances * kMaxBlendedClips> sampled;
  // Each stance's blend space blended, the one the body is in first; and the
  // two stances blended by the change's progress.
  std::array<std::vector<ozz::math::SoaTransform>, kBlendedStances> stances;
  std::vector<ozz::math::SoaTransform> blended;

  // Fits every buffer to skeleton.
  void Fit(const ozz::animation::Skeleton& skeleton) {
    const auto soa_joints = static_cast<std::size_t>(skeleton.num_soa_joints());
    for (std::size_t slot = 0; slot < contexts.size(); ++slot) {
      if (contexts.at(slot).max_tracks() < skeleton.num_joints()) {
        contexts.at(slot).Resize(skeleton.num_joints());
      }
      sampled.at(slot).resize(soa_joints);
    }
    for (std::vector<ozz::math::SoaTransform>& stance : stances) {
      stance.resize(soa_joints);
    }
    blended.resize(soa_joints);
  }

  // Blends layers into output over skeleton's rest pose.
  static void Blend(const ozz::animation::Skeleton& skeleton,
                    std::span<const ozz::animation::BlendingJob::Layer> layers,
                    std::vector<ozz::math::SoaTransform>& output) {
    ozz::animation::BlendingJob blending;
    blending.layers = {layers.data(), layers.size()};
    blending.rest_pose = skeleton.joint_rest_poses();
    blending.output = {output.data(), output.size()};
    [[maybe_unused]] const bool blended_ok = blending.Run();
    assert(blended_ok);
  }

  // Each Clip of stance's blend space at input's velocity, sampled where the
  // Stride puts it, then blended by its weight into stances[index].
  void BlendStance(const CharacterClips& clips, physics::Stance stance, const PoseInput& input, std::size_t index) {
    const LocomotionBlend blend = BlendSpace(clips.Of(stance), LocalVelocity(input.body.velocity, input.yaw));
    std::array<ozz::animation::BlendingJob::Layer, kMaxBlendedClips> layers{};
    for (std::size_t each_clip = 0; each_clip < blend.count; ++each_clip) {
      const ClipWeight& each = blend.clips.at(each_clip);
      const std::size_t slot = (index * kMaxBlendedClips) + each_clip;
      std::vector<ozz::math::SoaTransform>& output = sampled.at(slot);
      ozz::animation::SamplingJob sampling;
      sampling.animation = &each.clip->animation;
      sampling.context = &contexts.at(slot);
      sampling.ratio = RatioAt(*each.clip, input.body.stride);
      sampling.output = {output.data(), output.size()};
      [[maybe_unused]] const bool sampled_ok = sampling.Run();
      // LoadClips checked every Clip's tracks against the Skeleton.
      assert(sampled_ok);
      layers.at(each_clip) = {.weight = each.weight, .transform = {output.data(), output.size()}, .joint_weights = {}};
    }
    Blend(clips.skeleton, {layers.data(), blend.count}, stances.at(index));
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

  // The stance's blend space; while a change of stance is under way, the
  // stance it left blended into it by how far the change has gone.
  buffers.BlendStance(clips, input.body.stance, input, 0);
  const std::vector<ozz::math::SoaTransform>* pose = &buffers.stances.at(0);
  if (input.body.previous_stance != input.body.stance && input.body.stance_progress < physics::kStanceProgressSteps) {
    buffers.BlendStance(clips, input.body.previous_stance, input, 1);
    const float progress =
        static_cast<float>(input.body.stance_progress) / static_cast<float>(physics::kStanceProgressSteps);
    const std::array<ozz::animation::BlendingJob::Layer, kBlendedStances> layers{{
        {.weight = progress,
         .transform = {buffers.stances.at(0).data(), buffers.stances.at(0).size()},
         .joint_weights = {}},
        {.weight = 1.0F - progress,
         .transform = {buffers.stances.at(1).data(), buffers.stances.at(1).size()},
         .joint_weights = {}},
    }};
    Buffers::Blend(clips.skeleton, layers, buffers.blended);
    pose = &buffers.blended;
  }

  // Each ozz-animation Joint into its Joint of the Skeleton, lane by lane.
  posed.local.resize(skeleton.joints.size());
  const auto joint_count = static_cast<std::size_t>(clips.skeleton.num_joints());
  for (std::size_t block = 0; block * 4 < joint_count; ++block) {
    const ozz::math::SoaTransform& soa = pose->at(block);
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
