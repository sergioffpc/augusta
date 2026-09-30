#include "augusta/weapon.h"

#include <algorithm>

#include "augusta/command.h"
#include "augusta/parameters.h"

namespace augusta::weapon {

namespace {

constexpr float kSecondsPerMinute = 60.0F;

// A round whose wait has this little left fires: a wait that is a whole number
// of ticks counts down to within rounding of 0, not to 0.
constexpr float kReadyWithin = 1e-4F;

}  // namespace

State Loaded(const parameters::Rifle& rifle) { return State{.cooldown = 0.0F, .rounds = rifle.magazine_capacity}; }

Result Step(const parameters::Rifle& rifle, const State& state, const command::Command& command, float delta_time) {
  Result result{.state = state};
  float& cooldown = result.state.cooldown;
  // An empty magazine pulls no trigger: it fires nothing and keeps no time.
  const bool trigger = command.fire && state.rounds > 0;
  if (trigger && cooldown <= kReadyWithin) {
    result.fired = true;
    --result.state.rounds;
    cooldown += kSecondsPerMinute / rifle.rounds_per_minute;
  }
  cooldown -= delta_time;
  // Held, the trigger keeps the time a round has been ready and not yet fired,
  // no more than the tick a faster rifle than the tick rate would need; let go,
  // it keeps none, so a rifle at rest never fires sooner for having rested.
  cooldown = std::max(cooldown, trigger ? -delta_time : 0.0F);
  return result;
}

}  // namespace augusta::weapon
