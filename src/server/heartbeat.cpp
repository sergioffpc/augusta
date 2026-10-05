#include "heartbeat.h"

#include <chrono>
#include <optional>

#include "augusta/tick.h"

namespace augusta::server {

Heartbeat::Heartbeat(std::chrono::steady_clock::time_point start) : since_(start) {}

Activity& Heartbeat::Current() { return current_; }

std::optional<Activity> Heartbeat::RecordTick(const tick::Timing& timing, std::chrono::steady_clock::time_point now) {
  ++current_.ticks;
  current_.late += timing.late ? 1U : 0U;
  current_.overrun += timing.overrun ? 1U : 0U;
  if (now - since_ < kHeartbeatInterval) {
    return std::nullopt;
  }
  const Activity second = current_;
  current_ = Activity{};
  since_ = now;
  return second;
}

}  // namespace augusta::server
