#include "augusta/weapon.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/parameters.h"

namespace augusta::weapon {

namespace {

constexpr float kSecondsPerMinute = 60.0F;

// A wait with this little left is over: one that is a whole number of ticks
// counts down to within rounding of 0, not to 0.
constexpr float kReadyWithin = 1e-4F;

// What state's rifle points off the view by once the round it just fired has
// kicked it: the pattern's kick for the round's place in its Burst, the last
// one again past the pattern's end.
RecoilOffset Kicked(const parameters::Rifle& rifle, const State& state, bool ads) {
  if (rifle.recoil_pattern.empty()) {
    return state.recoil;
  }
  const std::size_t last = rifle.recoil_pattern.size() - 1;
  const parameters::RecoilKick& kick = rifle.recoil_pattern[std::min<std::size_t>(state.burst_index, last)];
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

State Loaded(const parameters::Rifle& rifle) {
  return State{
      .cooldown = 0.0F, .reload_remaining = 0.0F, .recoil = {}, .rounds = rifle.magazine_capacity, .burst_index = 0};
}

Result Step(const parameters::Rifle& rifle, const State& state, const command::Command& command, float delta_time) {
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

}  // namespace augusta::weapon
