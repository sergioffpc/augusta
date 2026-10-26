#include "augusta/weapon.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"

namespace augusta::weapon {

namespace {

constexpr float kSecondsPerMinute = 60.0F;

// A wait with this little left is over: one that is a whole number of ticks
// counts down to within rounding of 0, not to 0.
constexpr float kReadyWithin = 1e-4F;

// What state's rifle points off the view by once the round it just fired has
// kicked it: the pattern's kick for the round's place in its Burst, the last
// one again past the pattern's end.
RecoilOffset Kicked(const Rifle& rifle, const State& state, bool ads) {
  if (rifle.recoil_pattern.empty()) {
    return state.recoil;
  }
  const std::size_t last = rifle.recoil_pattern.size() - 1;
  const RecoilKick& kick = rifle.recoil_pattern[std::min<std::size_t>(state.burst_index, last)];
  const float scale = ads ? rifle.ads_recoil_scale : 1.0F;
  return RecoilOffset{.pitch = math::SnapAngle(state.recoil.pitch + (kick.pitch * scale)),
                      .yaw = math::SnapAngle(state.recoil.yaw + (kick.yaw * scale))};
}

// recoil after shrinking by angle, in radians, toward zero: along its own line,
// so the rifle settles straight back on the view.
RecoilOffset Recovered(const RecoilOffset& recoil, float angle) {
  const float length = std::sqrt((recoil.pitch * recoil.pitch) + (recoil.yaw * recoil.yaw));
  if (length <= angle) {
    return RecoilOffset{};
  }
  const float kept = (length - angle) / length;
  return RecoilOffset{.pitch = math::SnapAngle(recoil.pitch * kept), .yaw = math::SnapAngle(recoil.yaw * kept)};
}

}  // namespace

State Loaded(const Rifle& rifle) {
  return State{
      .cooldown = 0.0F, .reload_remaining = 0.0F, .recoil = {}, .rounds = rifle.magazine_capacity, .burst_index = 0};
}

Result Step(const Rifle& rifle, const State& state, const command::Command& command, float delta_time) {
  Result result{.state = state};
  State& next = result.state;
  const bool starts_reload = command.reload && state.reload_remaining <= 0.0F && state.rounds < rifle.magazine_capacity;
  if (starts_reload) {
    next.reload_remaining = rifle.reload_seconds;
  }
  const bool reloading = starts_reload || state.reload_remaining > 0.0F;

  // An empty magazine pulls no trigger, and neither does a rifle being
  // reloaded: it fires nothing and keeps no time.
  const bool trigger = command.fire && state.rounds > 0 && !reloading;
  if (trigger && next.cooldown <= kReadyWithin) {
    result.fired = true;
    result.yaw = command.yaw + state.recoil.yaw;
    result.pitch = command.pitch + state.recoil.pitch;
    --next.rounds;
    next.cooldown += kSecondsPerMinute / rifle.rounds_per_minute;
    next.recoil = Kicked(rifle, state, command.ads);
    // Never past what it holds: a Burst fires no more rounds than a magazine
    // has, and a reload takes a tick off the trigger.
    ++next.burst_index;
  }
  if (!trigger) {
    next.burst_index = 0;
    next.recoil = Recovered(state.recoil, rifle.recoil_recovery_per_second * delta_time);
  }
  next.cooldown -= delta_time;
  // Held, the trigger keeps the time a round has been ready and not yet fired,
  // no more than the tick a faster rifle than the tick rate would need; let go,
  // it keeps none, so a rifle at rest never fires sooner for having rested.
  next.cooldown = std::max(next.cooldown, trigger ? -delta_time : 0.0F);

  if (reloading) {
    next.reload_remaining -= delta_time;
    if (next.reload_remaining <= kReadyWithin) {
      next.reload_remaining = 0.0F;
      next.rounds = rifle.magazine_capacity;
    }
  }
  return result;
}

Markers MarkersOf(const assets::RifleData& rifle) {
  const auto frame = [](const assets::RifleMarker& marker) {
    return math::ToMat4(marker.translation, marker.rotation, math::Vec3(1.0F));
  };
  return Markers{.grip = frame(rifle.grip),
                 .muzzle = frame(rifle.muzzle),
                 .sight = frame(rifle.sight),
                 .fore_end = frame(rifle.fore_end)};
}

math::Mat4 PlaceMuzzle(const math::Mat4& hand, const Markers& markers) {
  return hand * math::Inverse(markers.grip) * markers.muzzle;
}

math::Mat4 PlaceSight(const math::Mat4& hand, const Markers& markers) {
  return hand * math::Inverse(markers.grip) * markers.sight;
}

math::Mat4 PlaceForeEnd(const math::Mat4& hand, const Markers& markers) {
  return hand * math::Inverse(markers.grip) * markers.fore_end;
}

math::Mat4 Recoiled(const math::Mat4& hand, float yaw, float pitch, const RecoilOffset& recoil) {
  // The hand in the frame of the view at the Grip, then out of the frame of the
  // view turned by recoil, at the same Grip.
  const math::Vec3 grip = math::TranslationOf(hand);
  const math::Mat4 view = math::ToMat4(grip, command::ViewRotation(yaw, pitch), math::Vec3(1.0F));
  const math::Mat4 kicked =
      math::ToMat4(grip, command::ViewRotation(yaw + recoil.yaw, pitch + recoil.pitch), math::Vec3(1.0F));
  return kicked * math::Inverse(view) * hand;
}

Heading Zeroed(const Rifle& rifle, const ballistics::BulletConfig& flight, float step_seconds, const math::Vec3& muzzle,
               const math::Vec3& eye, const Heading& sight) {
  // After t seconds in steps of step_seconds, semi-implicit Euler has dropped a
  // round g(t^2 + t*step)/2 below the line it left along (ballistics::World), so
  // a round that reaches the zero point at t leaves along the way to it raised
  // by that drop, and t is how long that way takes at the muzzle velocity. A
  // fixed number of rounds of that settles it far below the angle grid's step,
  // the drop being small beside the range, and keeps it deterministic.
  constexpr int kSettlingRounds = 4;
  const math::Vec3 view = command::ViewDirection(sight.yaw, sight.pitch);
  const math::Vec3 toward = eye + (view * rifle.zero_range) - muzzle;
  math::Vec3 along = toward;
  for (int round = 0; round < kSettlingRounds; ++round) {
    const float seconds = math::Length(along) / rifle.muzzle_velocity;
    const float drop = flight.gravity * ((seconds * seconds) + (seconds * step_seconds)) / 2.0F;
    along = toward + math::Vec3(0.0F, drop, 0.0F);
  }
  // The turn from sight to along, added to sight's own angles rather than
  // taking along's afresh: most of what atan2 and asin round off cancels out,
  // and a round with little to turn leaves along sight's own angles.
  constexpr float kTurn = 2.0F * std::numbers::pi_v<float>;
  const auto yaw_of = [](const math::Vec3& direction) { return std::atan2(-direction.x, -direction.z); };
  const auto pitch_of = [](const math::Vec3& direction) {
    return std::asin(std::clamp(math::Normalize(direction).y, -1.0F, 1.0F));
  };
  return Heading{.yaw = sight.yaw + std::remainder(yaw_of(along) - yaw_of(view), kTurn),
                 .pitch = sight.pitch + (pitch_of(along) - pitch_of(view))};
}

}  // namespace augusta::weapon
