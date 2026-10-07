#include "heartbeat.h"

#include <chrono>
#include <optional>

namespace augusta::server {

namespace {

// What happened between earlier and later, two running totals.
Activity Since(const Activity& earlier, const Activity& later) {
  return Activity{
      .ticks = later.ticks - earlier.ticks,
      .late = later.late - earlier.late,
      .overrun = later.overrun - earlier.overrun,
      .messages = later.messages - earlier.messages,
      .stale = later.stale - earlier.stale,
      .dropped = later.dropped - earlier.dropped,
      .overflow = later.overflow - earlier.overflow,
      .misbehaving = later.misbehaving - earlier.misbehaving,
  };
}

}  // namespace

Heartbeat::Heartbeat(std::chrono::steady_clock::time_point start) : since_(start) {}

std::optional<Activity> Heartbeat::Record(const Activity& totals, std::chrono::steady_clock::time_point now) {
  if (now - since_ < kHeartbeatInterval) {
    return std::nullopt;
  }
  const Activity second = Since(at_start_, totals);
  at_start_ = totals;
  since_ = now;
  return second;
}

}  // namespace augusta::server
