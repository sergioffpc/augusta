#include "augusta/local_view.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <glm/common.hpp>

#include "augusta/command.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/weapon.h"

namespace augusta::presentation {

namespace {

// Below this fraction of the way from previous to latest, previous's stance is
// shown; at or beyond it, latest's - the same rule as a remote player's
// (interpolation.cpp).
constexpr float kMidpointFraction = 0.5F;

// The first of living in players' order after after, wrapping around to after
// itself last; from the start of players if after is nullopt or not among them.
// nullopt if none of players is living.
std::optional<EntityId> NextLiving(std::span<const EntityId> players, std::span<const EntityId> living,
                                   std::optional<EntityId> after) {
  const auto found = after.has_value() ? std::ranges::find(players, *after) : players.end();
  const std::size_t start = found == players.end() ? 0 : static_cast<std::size_t>(found - players.begin()) + 1;
  for (std::size_t offset = 0; offset < players.size(); ++offset) {
    const EntityId candidate = players[(start + offset) % players.size()];
    if (std::ranges::contains(living, candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

}  // namespace

prediction::State BlendTicks(const prediction::State& previous, const prediction::State& latest, float fraction) {
  prediction::State blended = latest;
  blended.local_body.position = math::Lerp(previous.local_body.position, latest.local_body.position, fraction);
  blended.local_body.velocity = math::Lerp(previous.local_body.velocity, latest.local_body.velocity, fraction);
  blended.local_body.stance = fraction < kMidpointFraction ? previous.local_body.stance : latest.local_body.stance;
  blended.total_correction = math::Lerp(previous.total_correction, latest.total_correction, fraction);
  blended.rifle.recoil = weapon::RecoilOffset{
      .pitch = glm::mix(previous.rifle.recoil.pitch, latest.rifle.recoil.pitch, fraction),
      .yaw = glm::mix(previous.rifle.recoil.yaw, latest.rifle.recoil.yaw, fraction),
  };
  return blended;
}

Camera LocalCamera(const math::Vec3& feet, physics::Stance stance, const math::Vec3& standing_eye, const Aim& aim,
                   const weapon::RecoilOffset& recoil) {
  return Camera{.position = feet + physics::LowerToStance(standing_eye, stance),
                .rotation = command::ViewRotation(aim.yaw + recoil.yaw, aim.pitch + recoil.pitch),
                .vertical_fov = kHipFieldOfView};
}

Camera WatchedCamera(const RemoteBody& body, const math::Vec3& standing_eye) {
  return LocalCamera(body.position, body.stance, standing_eye, Aim{.yaw = body.yaw, .pitch = 0.0F, .ads = false},
                     weapon::RecoilOffset{});
}

float AdsZoom::Update(bool ads, float ads_field_of_view, float delta_time) {
  const float step = delta_time / kTransitionSeconds;
  progress_ = std::clamp(progress_ + (ads ? step : -step), 0.0F, 1.0F);
  // Eased at both ends, so the zoom neither starts nor stops with a jolt.
  return glm::mix(kHipFieldOfView, ads_field_of_view, glm::smoothstep(0.0F, 1.0F, progress_));
}

bool HitMarker::Update(std::uint32_t confirmations, float delta_time) {
  remaining_ = confirmations > 0 ? kShownSeconds : remaining_ - delta_time;
  return remaining_ > 0.0F;
}

std::optional<EntityId> Spectator::Update(std::span<const EntityId> players, std::span<const EntityId> living,
                                          bool fire_held) {
  const bool fire_pressed = fire_held && !fire_held_;
  fire_held_ = fire_held;
  if (!watched_.has_value() || !std::ranges::contains(living, *watched_) || fire_pressed) {
    watched_ = NextLiving(players, living, watched_);
  }
  return watched_;
}

}  // namespace augusta::presentation
