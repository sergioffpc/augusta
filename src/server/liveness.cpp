#include "liveness.h"

#include "augusta/tick.h"

namespace augusta::server {

bool IsLive(tick::Clock::time_point last_tick_end, tick::Clock::time_point now) {
  return now - last_tick_end <= kLivenessWindow;
}

}  // namespace augusta::server
