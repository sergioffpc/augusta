#include "augusta/tick.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace augusta::tick {

Clock::time_point NextDeadline(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point now) {
  const Clock::time_point next = deadline + tick_duration;
  if (now - next > kMaxTicksBehind * tick_duration) {
    return now;
  }
  return next;
}

Timing Measure(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point start,
               Clock::time_point end) {
  return {.late = start - deadline > kLateTolerance, .overrun = end - start > tick_duration};
}

Clock::duration PacedTickDuration(Clock::duration nominal, std::uint8_t queued_commands) {
  const float off_target = static_cast<float>(queued_commands) - kTargetQueuedCommands;
  const float pacing = std::clamp(off_target * kPacingPerCommand, -kMaxPacing, kMaxPacing);
  return std::chrono::duration_cast<Clock::duration>(nominal * (1.0 + pacing));
}

}  // namespace augusta::tick
