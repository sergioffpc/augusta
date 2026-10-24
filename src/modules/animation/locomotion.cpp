#include "augusta/locomotion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

#include <glm/ext/quaternion_trigonometric.hpp>

#include "augusta/clips.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::animation {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Below this speed, in m/s, a body is still.
constexpr float kStill = 1e-4F;

// A directional Clip and the direction it moves in, as an angle from ahead
// towards the right, in radians.
struct Direction {
  const Clip* clip = nullptr;
  float angle = 0.0F;
};

// clips' directional Clips, by angle from -pi (backward) up: backward, the left
// side, ahead, the right side. Every stance has forward and backward.
std::size_t DirectionsOf(const StanceClips& clips, std::array<Direction, 8>& directions) {
  std::size_t count = 0;
  const auto add = [&](const Clip* clip, float angle) {
    if (clip != nullptr) {
      directions.at(count++) = Direction{.clip = clip, .angle = angle};
    }
  };
  const auto optional = [](const std::optional<Clip>& clip) { return clip.has_value() ? &*clip : nullptr; };
  add(&clips.backward, -kPi);
  add(optional(clips.backward_left), -kPi * 3.0F / 4.0F);
  add(optional(clips.left), -kPi / 2.0F);
  add(optional(clips.forward_left), -kPi / 4.0F);
  add(&clips.forward, 0.0F);
  add(optional(clips.forward_right), kPi / 4.0F);
  add(optional(clips.right), kPi / 2.0F);
  add(optional(clips.backward_right), kPi * 3.0F / 4.0F);
  return count;
}

// How fast clip moves a body, in m/s: its stride over its duration.
float SpeedOf(const Clip& clip) {
  const float duration = clip.animation.duration();
  return duration > 0.0F ? clip.stride_length / duration : 0.0F;
}

void Add(LocomotionBlend& blend, const Clip* clip, float weight) {
  if (weight > 0.0F) {
    blend.clips.at(blend.count++) = ClipWeight{.clip = clip, .weight = weight};
  }
}

}  // namespace

LocomotionBlend BlendSpace(const StanceClips& clips, const math::Vec3& local_velocity) {
  LocomotionBlend blend;
  const float ahead = -local_velocity.z;
  const float right = local_velocity.x;
  const float speed = std::hypot(ahead, right);
  if (speed < kStill) {
    Add(blend, &clips.idle, 1.0F);
    return blend;
  }

  // The two directions either side of the velocity's, round the circle: the
  // last one's neighbour is backward again, at pi.
  std::array<Direction, 8> directions{};
  const std::size_t count = DirectionsOf(clips, directions);
  const float angle = std::atan2(right, ahead);
  Direction below = directions.at(count - 1);
  Direction above{.clip = directions.front().clip, .angle = kPi};
  for (std::size_t i = 0; i + 1 < count; ++i) {
    if (angle >= directions.at(i).angle && angle < directions.at(i + 1).angle) {
      below = directions.at(i);
      above = directions.at(i + 1);
      break;
    }
  }
  const float toward_above = (angle - below.angle) / (above.angle - below.angle);
  float below_weight = 1.0F - toward_above;
  float above_weight = toward_above;

  // Slower than the two Clips move, idle makes up the rest.
  const float clip_speed = (below_weight * SpeedOf(*below.clip)) + (above_weight * SpeedOf(*above.clip));
  float idle_weight = 0.0F;
  if (clip_speed > 0.0F && speed < clip_speed) {
    const float moving = speed / clip_speed;
    below_weight *= moving;
    above_weight *= moving;
    idle_weight = 1.0F - moving;
  }

  // Faster, standing, forward's weight goes over to sprint as the speed nears its.
  float sprint_weight = 0.0F;
  float* forward_weight = below.clip == &clips.forward   ? &below_weight
                          : above.clip == &clips.forward ? &above_weight
                                                         : nullptr;
  if (clips.sprint.has_value() && forward_weight != nullptr && speed > clip_speed &&
      SpeedOf(*clips.sprint) > clip_speed) {
    const float toward_sprint = std::min((speed - clip_speed) / (SpeedOf(*clips.sprint) - clip_speed), 1.0F);
    sprint_weight = *forward_weight * toward_sprint;
    *forward_weight -= sprint_weight;
  }

  Add(blend, &clips.idle, idle_weight);
  Add(blend, below.clip, below_weight);
  Add(blend, above.clip, above_weight);
  Add(blend, clips.sprint.has_value() ? &*clips.sprint : nullptr, sprint_weight);
  return blend;
}

float StrideLength(const StanceClips& clips, const math::Vec3& local_velocity) {
  float weighted = 0.0F;
  float moving = 0.0F;
  for (const ClipWeight& each : BlendSpace(clips, local_velocity).Weights()) {
    if (each.clip->stride_length > 0.0F) {
      weighted += each.weight * each.clip->stride_length;
      moving += each.weight;
    }
  }
  return moving > 0.0F ? weighted / moving : clips.forward.stride_length;
}

float StrideLength(const CharacterClips& clips, const physics::BodyState& body, float yaw) {
  // Turned back by the yaw: the body's own space, where -Z is ahead.
  const math::Vec3 local = glm::angleAxis(-yaw, math::Vec3(0.0F, 1.0F, 0.0F)) * body.velocity;
  return StrideLength(clips.Of(body.stance), local);
}

}  // namespace augusta::animation
