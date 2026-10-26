#include "animation.h"

#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/quaternion.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/transform.h>
#include <ozz/base/maths/vec_float.h>
#include <ozz/base/memory/unique_ptr.h>
#include <ozz/base/span.h>

#include "augusta/assets.h"
#include "augusta/math.h"

namespace augusta::pack {

namespace {

// The lanes of a SoA value: ozz-animation keeps four Joints in each.
constexpr std::size_t kSoaWidth = 4;

using Lanes = std::array<float, kSoaWidth>;

// Each lane of value. Floats, never a standard container of the SIMD values
// themselves: clang-cl aligns one of those to 8 bytes only, and the aligned
// loads of its elements crash (tests/animation_ozz_jobs_test.cpp).
Lanes LanesOf(ozz::math::_SimdFloat4 value) {
  Lanes lanes{};
  ozz::math::StorePtrU(value, lanes.data());
  return lanes;
}

ozz::math::Float3 Float3Of(const math::Vec3& vec) { return {vec.x, vec.y, vec.z}; }

ozz::math::Quaternion QuaternionOf(const math::Quat& quat) { return {quat.x, quat.y, quat.z, quat.w}; }

// The RawSkeleton Joint of joints[index], its children under it in joints'
// order.
ozz::animation::offline::RawSkeleton::Joint RawJointOf(std::span<const assets::JointData> joints,
                                                       const std::vector<std::vector<std::size_t>>& children,
                                                       std::size_t index) {
  const assets::JointData& joint = joints[index];
  ozz::animation::offline::RawSkeleton::Joint raw;
  raw.name = joint.name.c_str();
  raw.transform.translation = Float3Of(joint.translation);
  raw.transform.rotation = QuaternionOf(joint.rotation);
  raw.transform.scale = Float3Of(joint.scale);
  for (const std::size_t child : children[index]) {
    raw.children.push_back(RawJointOf(joints, children, child));
  }
  return raw;
}

ozz::unique_ptr<ozz::animation::Skeleton> BuildSkeleton(std::span<const assets::JointData> joints) {
  std::vector<std::vector<std::size_t>> children(joints.size());
  std::vector<std::size_t> roots;
  for (std::size_t index = 0; index < joints.size(); ++index) {
    const std::uint32_t parent = joints[index].parent;
    if (parent == assets::kJointNoParent) {
      roots.push_back(index);
    } else if (parent < index) {
      children[parent].push_back(index);
    } else {
      throw std::invalid_argument(std::format("joint {} is listed before its parent", joints[index].name));
    }
  }
  ozz::animation::offline::RawSkeleton raw;
  for (const std::size_t root : roots) {
    raw.roots.push_back(RawJointOf(joints, children, root));
  }
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  if (!skeleton) {
    throw std::invalid_argument(std::format("ozz-animation cannot build a skeleton of {} joints", joints.size()));
  }
  return skeleton;
}

template <typename Object>
std::vector<std::byte> ArchiveOf(const Object& object) {
  ozz::io::MemoryStream stream;
  ozz::io::OArchive archive(&stream);
  archive << object;
  std::vector<std::byte> bytes(stream.Size());
  stream.Seek(0, ozz::io::Stream::kSet);
  stream.Read(bytes.data(), bytes.size());
  return bytes;
}

template <typename Object>
Object Unarchive(std::span<const std::byte> bytes, std::string_view what) {
  ozz::io::MemoryStream stream;
  stream.Write(bytes.data(), bytes.size());
  stream.Seek(0, ozz::io::Stream::kSet);
  ozz::io::IArchive archive(&stream);
  if (!archive.TestTag<Object>()) {
    throw std::invalid_argument(std::format("not an ozz-animation {} archive", what));
  }
  Object object;
  archive >> object;
  return object;
}

// The track of the Joint at index in clip's samples.
ozz::animation::offline::RawAnimation::JointTrack TrackOf(const ClipSamples& clip, std::size_t index) {
  ozz::animation::offline::RawAnimation::JointTrack track;
  for (std::size_t sample = 0; sample < clip.times.size(); ++sample) {
    const float time = clip.times[sample];
    track.translations.push_back({.time = time, .value = Float3Of(clip.translations[sample][index])});
    track.rotations.push_back({.time = time, .value = QuaternionOf(clip.rotations[sample][index])});
    track.scales.push_back({.time = time, .value = Float3Of(clip.scales[sample][index])});
  }
  return track;
}

void CheckSamples(std::span<const assets::JointData> joints, const ClipSamples& clip) {
  const std::size_t count = clip.times.size();
  if (count == 0 || clip.translations.size() != count || clip.rotations.size() != count ||
      clip.scales.size() != count) {
    throw std::invalid_argument("a clip needs a translation, rotation and scale at each of one or more samples");
  }
  for (std::size_t sample = 0; sample < count; ++sample) {
    if (clip.translations[sample].size() != joints.size() || clip.rotations[sample].size() != joints.size() ||
        clip.scales[sample].size() != joints.size()) {
      throw std::invalid_argument(std::format("sample {} does not hold each of the {} joints", sample, joints.size()));
    }
  }
}

}  // namespace

std::vector<std::byte> BuildSkeletonArchive(std::span<const assets::JointData> joints) {
  return ArchiveOf(*BuildSkeleton(joints));
}

std::vector<std::byte> BuildClipArchive(std::span<const assets::JointData> joints, const ClipSamples& clip) {
  CheckSamples(joints, clip);
  const auto skeleton = BuildSkeleton(joints);
  std::unordered_map<std::string_view, std::size_t> index_of;
  for (std::size_t index = 0; index < joints.size(); ++index) {
    index_of.emplace(joints[index].name, index);
  }
  ozz::animation::offline::RawAnimation raw;
  raw.duration = clip.duration;
  // In the ozz Skeleton's order, which a track's index is.
  for (const char* const name : skeleton->joint_names()) {
    raw.tracks.push_back(TrackOf(clip, index_of.at(name)));
  }
  const auto animation = ozz::animation::offline::AnimationBuilder()(raw);
  if (!animation) {
    throw std::invalid_argument(
        std::format("ozz-animation cannot build a clip of {} s from {} samples", clip.duration, clip.times.size()));
  }
  return ArchiveOf(*animation);
}

std::vector<SampledJoint> SampleClip(std::span<const std::byte> skeleton_archive,
                                     std::span<const std::byte> clip_archive, float ratio) {
  const auto skeleton = Unarchive<ozz::animation::Skeleton>(skeleton_archive, "skeleton");
  const auto animation = Unarchive<ozz::animation::Animation>(clip_archive, "animation");
  if (animation.num_tracks() != skeleton.num_joints()) {
    throw std::invalid_argument(std::format("the clip has {} tracks, and its skeleton {} joints",
                                            animation.num_tracks(), skeleton.num_joints()));
  }
  ozz::animation::SamplingJob::Context context(animation.num_tracks());
  std::vector<ozz::math::SoaTransform> locals(static_cast<std::size_t>(skeleton.num_soa_joints()));
  ozz::animation::SamplingJob job;
  job.animation = &animation;
  job.context = &context;
  job.ratio = ratio;
  job.output = {locals.data(), locals.size()};
  if (!job.Run()) {
    throw std::invalid_argument("ozz-animation could not sample the clip");
  }
  std::vector<SampledJoint> sampled;
  const auto names = skeleton.joint_names();
  for (std::size_t soa = 0; soa < locals.size(); ++soa) {
    const ozz::math::SoaTransform& transform = locals[soa];
    const Lanes tx = LanesOf(transform.translation.x);
    const Lanes ty = LanesOf(transform.translation.y);
    const Lanes tz = LanesOf(transform.translation.z);
    const Lanes rx = LanesOf(transform.rotation.x);
    const Lanes ry = LanesOf(transform.rotation.y);
    const Lanes rz = LanesOf(transform.rotation.z);
    const Lanes rw = LanesOf(transform.rotation.w);
    const Lanes sx = LanesOf(transform.scale.x);
    const Lanes sy = LanesOf(transform.scale.y);
    const Lanes sz = LanesOf(transform.scale.z);
    for (std::size_t lane = 0; lane < kSoaWidth && soa * kSoaWidth + lane < names.size(); ++lane) {
      sampled.push_back({.name = names[soa * kSoaWidth + lane],
                         .translation = math::Vec3(tx[lane], ty[lane], tz[lane]),
                         .rotation = math::Quat(rw[lane], rx[lane], ry[lane], rz[lane]),
                         .scale = math::Vec3(sx[lane], sy[lane], sz[lane])});
    }
  }
  return sampled;
}

}  // namespace augusta::pack
