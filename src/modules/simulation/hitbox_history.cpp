#include "hitbox_history.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "augusta/math.h"

namespace augusta::simulation {

namespace {

// Below this fraction of the way from one tick to the next the earlier stance
// is the nearer one, as on the client (presentation's RemoteInterpolator).
constexpr float kMidpointFraction = 0.5F;

}  // namespace

void PoseHistory::Record(std::uint64_t tick, const Pose& pose) {
  poses_.push_back(pose);
  newest_tick_ = tick;
  if (poses_.size() > capacity_) {
    poses_.pop_front();
  }
}

std::optional<Pose> PoseHistory::At(double time) const {
  if (poses_.empty()) {
    return std::nullopt;
  }
  const auto newest = static_cast<double>(newest_tick_);
  const double oldest = newest - static_cast<double>(poses_.size() - 1);
  const double held = std::clamp(time, oldest, newest);
  const double whole = std::floor(held);
  const auto earlier_index = static_cast<std::size_t>(whole - oldest);
  const auto fraction = static_cast<float>(held - whole);
  const Pose& earlier = poses_[earlier_index];
  if (earlier_index + 1 == poses_.size()) {
    return earlier;
  }
  const Pose& later = poses_[earlier_index + 1];
  return Pose{.position = math::Lerp(earlier.position, later.position, fraction),
              .yaw = math::LerpAngle(earlier.yaw, later.yaw, fraction),
              .stance = fraction < kMidpointFraction ? earlier.stance : later.stance};
}

}  // namespace augusta::simulation
