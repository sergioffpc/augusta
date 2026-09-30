#include "augusta/weapon.h"

#include <algorithm>

#include "augusta/command.h"
#include "augusta/parameters.h"

namespace augusta::weapon {

namespace {

constexpr float kSecondsPerMinute = 60.0F;

// A wait with this little left is over: one that is a whole number of ticks
// counts down to within rounding of 0, not to 0.
constexpr float kReadyWithin = 1e-4F;

}  // namespace

State Loaded(const parameters::Rifle& rifle) {
  return State{.cooldown = 0.0F, .reload_remaining = 0.0F, .rounds = rifle.magazine_capacity};
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
    --next.rounds;
    next.cooldown += kSecondsPerMinute / rifle.rounds_per_minute;
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
