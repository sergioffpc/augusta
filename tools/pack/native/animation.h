#ifndef AUGUSTA_PACK_ANIMATION_H_
#define AUGUSTA_PACK_ANIMATION_H_

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"

/// \file
/// The ozz-animation half of the cook (ADR-0066): a Character's Skeleton and
/// each of its Clips built into ozz-animation's archives with its offline
/// libraries, which the cooker files as pack entries, and a Clip sampled back
/// out of its archive with its runtime, for a tool or a test to see what a Clip
/// cooked to. The Clips' reading from USD, their checks and the removal of
/// their root motion are the cooker's (augusta_pack.cook); loading the archives
/// at runtime is the Animation module's. Nothing here knows of Python.
namespace augusta::pack {

/// A Clip as sampled from its authored animation, every Joint at each sample.
struct ClipSamples {
  /// Seconds since the Clip's first sample, ascending, the first 0 and none
  /// past duration.
  std::vector<float> times;
  /// In seconds, above 0.
  float duration = 0.0F;
  /// Each indexed [sample][joint], the Joints in their Skeleton's order
  /// (assets::JointData's), each transform relative to its parent, or to the
  /// character's root for a root Joint.
  std::vector<std::vector<math::Vec3>> translations;
  std::vector<std::vector<math::Quat>> rotations;
  std::vector<std::vector<math::Vec3>> scales;
};

/// A Joint's transform relative to its parent as a Clip puts it at a moment.
struct SampledJoint {
  std::string name;
  math::Vec3 translation{0.0F, 0.0F, 0.0F};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  math::Vec3 scale{1.0F, 1.0F, 1.0F};
};

/// The ozz::animation::Skeleton archive of joints, parents before children
/// (assets::SkeletonData's order). ozz-animation keeps them depth first, which
/// may be another order: a Joint is found in it by its name. Throws
/// std::invalid_argument for joints ozz-animation cannot build a Skeleton of.
std::vector<std::byte> BuildSkeletonArchive(std::span<const assets::JointData> joints);

/// The ozz::animation::Animation archive of clip, played on the Skeleton of
/// joints: a track for each Joint, in the order of joints' Skeleton archive.
/// Throws std::invalid_argument for a clip ozz-animation cannot build, or one
/// whose samples do not hold every one of joints.
std::vector<std::byte> BuildClipArchive(std::span<const assets::JointData> joints, const ClipSamples& clip);

/// Every Joint of the Skeleton in skeleton_archive, in its order, as the Clip in
/// clip_archive puts it at ratio of its duration (0 its start, 1 its end).
/// Throws std::invalid_argument for an archive that does not load, or a Clip
/// of another Skeleton.
std::vector<SampledJoint> SampleClip(std::span<const std::byte> skeleton_archive,
                                     std::span<const std::byte> clip_archive, float ratio);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_ANIMATION_H_
