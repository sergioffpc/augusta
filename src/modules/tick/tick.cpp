#include "augusta/tick.h"

#include <chrono>

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

}  // namespace augusta::tick
