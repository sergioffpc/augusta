#include "augusta/tick.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace augusta::tick {

namespace {

// Whether a loop whose Tick due at deadline ended at now is too far behind to
// catch up, and resynchronises to now.
bool TooFarBehind(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point now) {
  return now - (deadline + tick_duration) > kMaxTicksBehind * tick_duration;
}

}  // namespace

Clock::time_point NextDeadline(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point now) {
  return TooFarBehind(deadline, tick_duration, now) ? now : deadline + tick_duration;
}

Timing Measure(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point start,
               Clock::time_point end) {
  return {.duration = end - start,
          .late = start - deadline > kLateTolerance,
          .overrun = end - start > tick_duration,
          .resynchronised = TooFarBehind(deadline, tick_duration, end)};
}

Clock::duration PacedTickDuration(Clock::duration nominal, std::uint8_t queued_commands) {
  const float off_target = static_cast<float>(queued_commands) - kTargetQueuedCommands;
  const float pacing = std::clamp(off_target * kPacingPerCommand, -kMaxPacing, kMaxPacing);
  return std::chrono::duration_cast<Clock::duration>(nominal * (1.0 + pacing));
}

float FractionElapsed(Clock::time_point tick_start, Clock::duration tick_duration, Clock::time_point now) {
  if (tick_duration <= Clock::duration::zero()) {
    return 1.0F;
  }
  const std::chrono::duration<float> elapsed = now - tick_start;
  const std::chrono::duration<float> duration = tick_duration;
  return std::clamp(elapsed / duration, 0.0F, 1.0F);
}

}  // namespace augusta::tick
