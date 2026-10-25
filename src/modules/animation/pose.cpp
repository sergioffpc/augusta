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
#include <ozz/animation/runtime/ik_two_bone_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/internal/simd_math_config.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/simd_quaternion.h>
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
#include "augusta/weapon.h"

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

// Below this, a length is none: a bone along another, or no pole.
constexpr float kDegenerate = 1e-5F;

ozz::math::SimdFloat4 SimdOf(const math::Vec3& vector, float w = 0.0F) {
  return ozz::math::simd_float4::Load(vector.x, vector.y, vector.z, w);
}

ozz::math::Float4x4 SimdOf(const math::Mat4& matrix) {
  ozz::math::Float4x4 simd;
  for (int column = 0; column < 4; ++column) {
    simd.cols[column] =
        ozz::math::simd_float4::Load(matrix[column].x, matrix[column].y, matrix[column].z, matrix[column].w);
  }
  return simd;
}

math::Quat QuatOf(const ozz::math::SimdQuaternion& quaternion) {
  const std::array<float, 4> xyzw = LanesOf(quaternion.xyzw);
  return {xyzw[3], xyzw[0], xyzw[1], xyzw[2]};
}

// The part of offset off the line along direction, a unit vector.
math::Vec3 OffTheLine(const math::Vec3& offset, const math::Vec3& direction) {
  return offset - (direction * math::Dot(offset, direction));
}

// Composes posed's world transforms from its root and its local ones, parents
// before children.
void ComposeWorld(const assets::Skeleton& skeleton, SkeletonPose& posed) {
  const math::Mat4 root = hitbox::RootOf(posed.root);
  posed.world.resize(skeleton.joints.size());
  for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
    const assets::JointData& data = skeleton.joints[joint];
    const math::Mat4& parent = data.parent == assets::kJointNoParent ? root : posed.world.at(data.parent);
    posed.world[joint] = Compose(parent, posed.local[joint], data.scale);
  }
}

}  // namespace

// The stances a pose blends at most: the one a body is in and, while a change
// of stance is under way, the one it left.
constexpr std::size_t kBlendedStances = 2;

// The Clips an Aim offset and an ADS grip sample: the level aim, which the
// others add their difference from, the lowest or the highest, and the grip.
enum AimSlot : std::uint8_t { kLevel, kPitched, kGrip, kAimSlots };

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
  // One per Clip of a stance's Aim offset and ADS grip, by AimSlot.
  std::array<ozz::animation::SamplingJob::Context, kAimSlots> aim_contexts;
  std::array<std::vector<ozz::math::SoaTransform>, kAimSlots> aim_sampled;
  // A stance's blended pose with them added, before it takes the stance's place.
  std::vector<ozz::math::SoaTransform> aimed;

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
    for (std::size_t slot = 0; slot < kAimSlots; ++slot) {
      if (aim_contexts.at(slot).max_tracks() < skeleton.num_joints()) {
        aim_contexts.at(slot).Resize(skeleton.num_joints());
      }
      aim_sampled.at(slot).resize(soa_joints);
    }
    aimed.resize(soa_joints);
  }

  // Adds stance's Aim offset at pitch, and its ADS grip if ads, onto
  // stances[index]: the lowest or highest aim's difference from the level one,
  // weighted by how far pitch is toward it, and the grip's whole.
  void AddAim(const ozz::animation::Skeleton& skeleton, const StanceClips& stance, float pitch, bool ads,
              std::size_t index) {
    const float toward = std::min(std::fabs(pitch) / command::kMaxLookPitch, 1.0F);
    if (toward == 0.0F && !ads) {
      return;
    }
    std::array<ozz::animation::BlendingJob::Layer, 2> additive{};
    std::size_t count = 0;
    std::vector<ozz::math::SoaTransform>& level = aim_sampled.at(kLevel);
    Sample(stance.aim_center, 0.0F, aim_contexts.at(kLevel), level);
    if (toward > 0.0F) {
      std::vector<ozz::math::SoaTransform>& pitched = aim_sampled.at(kPitched);
      Sample(pitch < 0.0F ? stance.aim_down : stance.aim_up, 0.0F, aim_contexts.at(kPitched), pitched);
      ToDifference(level, pitched);
      additive.at(count++) = {.weight = toward, .transform = {pitched.data(), pitched.size()}, .joint_weights = {}};
    }
    if (ads) {
      std::vector<ozz::math::SoaTransform>& grip = aim_sampled.at(kGrip);
      Sample(stance.ads, 0.0F, aim_contexts.at(kGrip), grip);
      ToDifference(level, grip);
      additive.at(count++) = {.weight = 1.0F, .transform = {grip.data(), grip.size()}, .joint_weights = {}};
    }
    std::vector<ozz::math::SoaTransform>& pose = stances.at(index);
    const ozz::animation::BlendingJob::Layer whole{
        .weight = 1.0F, .transform = {pose.data(), pose.size()}, .joint_weights = {}};
    ozz::animation::BlendingJob adding;
    adding.layers = {&whole, 1};
    adding.additive_layers = {additive.data(), count};
    adding.rest_pose = skeleton.joint_rest_poses();
    adding.output = {aimed.data(), aimed.size()};
    [[maybe_unused]] const bool added = adding.Run();
    assert(added);
    pose.swap(aimed);
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
      Sample(*each.clip, RatioAt(*each.clip, input.body.stride), contexts.at(slot), output);
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

  // The stance's blend space, its Aim offset and ADS grip on top; while a
  // change of stance is under way, the stance it left, posed the same way,
  // blended into it by how far the change has gone.
  buffers.BlendStance(clips, input.body.stance, input, 0);
  buffers.AddAim(clips.skeleton, clips.Of(input.body.stance), input.pitch, input.ads, 0);
  const std::vector<ozz::math::SoaTransform>* pose = &buffers.stances.at(0);
  if (input.body.previous_stance != input.body.stance && input.body.stance_progress < physics::kStanceProgressSteps) {
    buffers.BlendStance(clips, input.body.previous_stance, input, 1);
    buffers.AddAim(clips.skeleton, clips.Of(input.body.previous_stance), input.pitch, input.ads, 1);
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
  ComposeWorld(skeleton, posed);
}

math::Mat4 HoldRifle(const assets::Skeleton& skeleton, const WeaponSocket& socket, const weapon::Markers& rifle,
                     SkeletonPose& posed) {
  const math::Mat4 hand = posed.world.at(socket.joint) * socket.frame;
  const std::uint32_t end = socket.left_hand;
  const std::uint32_t mid = skeleton.joints.at(end).parent;
  const std::uint32_t start = skeleton.joints.at(mid).parent;
  const math::Vec3 shoulder = math::TranslationOf(posed.world.at(start));
  const math::Vec3 elbow = math::TranslationOf(posed.world.at(mid));
  const math::Vec3 wrist = math::TranslationOf(posed.world.at(end));

  // The elbow points where the pose bent it, off the line from the shoulder to
  // the hand; down, for an arm held straight.
  math::Vec3 pole = OffTheLine(elbow - shoulder, math::Normalize(wrist - shoulder));
  if (math::Length(pole) < kDegenerate) {
    pole = math::Vec3(0.0F, -1.0F, 0.0F);
  }
  // It opens about the axis across both bones, or for a straight arm across the
  // upper arm and the pole; in the elbow's own space, as the job takes it.
  math::Vec3 axis = math::Cross(shoulder - elbow, wrist - elbow);
  if (math::Length(axis) < kDegenerate) {
    axis = math::Cross(shoulder - elbow, pole);
  }
  const math::Mat4 into_elbow = math::Inverse(posed.world.at(mid));
  const math::Vec3 mid_axis =
      math::Normalize(math::TransformPoint(into_elbow, axis) - math::TransformPoint(into_elbow, math::Vec3(0.0F)));

  const ozz::math::Float4x4 start_joint = SimdOf(posed.world.at(start));
  const ozz::math::Float4x4 mid_joint = SimdOf(posed.world.at(mid));
  const ozz::math::Float4x4 end_joint = SimdOf(posed.world.at(end));
  ozz::math::SimdQuaternion start_correction;
  ozz::math::SimdQuaternion mid_correction;
  ozz::animation::IKTwoBoneJob reaching;
  reaching.target = SimdOf(math::TranslationOf(weapon::PlaceForeEnd(hand, rifle)), 1.0F);
  reaching.pole_vector = SimdOf(pole);
  reaching.mid_axis = SimdOf(mid_axis);
  reaching.start_joint = &start_joint;
  reaching.mid_joint = &mid_joint;
  reaching.end_joint = &end_joint;
  reaching.start_joint_correction = &start_correction;
  reaching.mid_joint_correction = &mid_correction;
  [[maybe_unused]] const bool reached_ok = reaching.Run();
  // The axis is normalised, and every pointer set.
  assert(reached_ok);

  posed.local.at(start).rotation = posed.local.at(start).rotation * QuatOf(start_correction);
  posed.local.at(mid).rotation = posed.local.at(mid).rotation * QuatOf(mid_correction);
  ComposeWorld(skeleton, posed);
  return hand;
}

}  // namespace augusta::animation
