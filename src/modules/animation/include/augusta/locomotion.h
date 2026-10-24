#ifndef AUGUSTA_LOCOMOTION_H_
#define AUGUSTA_LOCOMOTION_H_

#include <array>
#include <cstddef>
#include <span>

#include "augusta/clips.h"
#include "augusta/math.h"
#include "augusta/physics.h"

/// \file
/// A stance's blend space (ADR-0066): which of its locomotion Clips make a
/// body's pose at a velocity, and how much of it each makes; and the stride
/// length its Stride advances by there, which Movement hands physics::World::Step
/// on both sides (SimulationWorld and PredictionWorld). Pure: a function of the
/// stance's Clips and the velocity alone.
///
/// The blend space is over the body's velocity in its own space (-Z ahead, +X
/// its right, as a view of yaw 0 looks, command::Command), along the ground:
/// - its direction picks the two directional Clips either side of it (forward,
///   backward, the sides and the diagonals the stance has), each weighted by
///   how near its direction is;
/// - its speed below theirs (each Clip's stride length over its duration) gives
///   the rest of the weight to idle; above it, standing, the forward Clip's
///   weight goes over to sprint as the speed nears sprint's.
namespace augusta::animation {

/// One Clip of a blend space and how much of the pose it makes, 0 to 1.
struct ClipWeight {
  const Clip* clip = nullptr;
  float weight = 0.0F;
};

/// The most Clips a blend space blends at once: idle, two directions and sprint.
inline constexpr std::size_t kMaxBlendedClips = 4;

/// A stance's blend space at one velocity: the Clips with a weight above 0,
/// whose weights sum to 1. It points into the StanceClips it was evaluated from.
struct LocomotionBlend {
  std::array<ClipWeight, kMaxBlendedClips> clips{};
  std::size_t count = 0;

  [[nodiscard]] std::span<const ClipWeight> Weights() const { return {clips.data(), count}; }
};

/// clips' blend space at local_velocity, the body's velocity in its own space;
/// its vertical part does not count. Still, it is idle alone.
[[nodiscard]] LocomotionBlend BlendSpace(const StanceClips& clips, const math::Vec3& local_velocity);

/// How far one Stride, a cycle of two steps, takes a body moving at
/// local_velocity in clips' blend space, in meters: its locomotion Clips' stride
/// lengths, each by its share of their weights; idle has none. Still, it is the
/// forward Clip's, so a body starting to move from rest advances its Stride.
[[nodiscard]] float StrideLength(const StanceClips& clips, const math::Vec3& local_velocity);

/// The stride length Movement steps body by (physics::World::Step): in its
/// stance's blend space of clips, at its velocity in the space of yaw, where it
/// faces. body is as the last tick left it, so its velocity is the one its pose
/// last showed.
[[nodiscard]] float StrideLength(const CharacterClips& clips, const physics::BodyState& body, float yaw);

}  // namespace augusta::animation

#endif  // AUGUSTA_LOCOMOTION_H_
