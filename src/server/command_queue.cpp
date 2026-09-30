#include "command_queue.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <string_view>

#include "augusta/command.h"
#include "augusta/math.h"

namespace augusta::server {

namespace {

bool IsFinite(const math::Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The command as a tick that repeats or idles would run it: nothing one-shot
// fires again.
command::Command WithoutActions(command::Command command) {
  command.fire = false;
  command.reload = false;
  return command;
}

}  // namespace

std::string_view DescribeRejection(Rejection rejection) {
  switch (rejection) {
    case Rejection::kStale:
      return "sequence not newer";
    case Rejection::kNonFinite:
      return "non-finite value";
    case Rejection::kOutOfRange:
      return "value out of range";
  }
  return "unknown rejection";
}

std::expected<void, Rejection> Validate(const SequencedCommand& command, std::uint32_t last_sequence) {
  if (command.sequence <= last_sequence) {
    return std::unexpected(Rejection::kStale);
  }
  const command::Command& input = command.command;
  if (!IsFinite(input.movement.direction) || !std::isfinite(input.yaw) || !std::isfinite(input.pitch) ||
      !std::isfinite(input.view_fraction)) {
    return std::unexpected(Rejection::kNonFinite);
  }
  if (math::Length(input.movement.direction) > kMaxMovementMagnitude || std::fabs(input.pitch) > kMaxPitch ||
      std::fabs(input.yaw) > kMaxYaw) {
    return std::unexpected(Rejection::kOutOfRange);
  }
  return {};
}

int HeldTicks(std::uint8_t tick_rate_hz) {
  return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(kMaxHeldTime * tick_rate_hz).count());
}

std::expected<Enqueued, Rejection> CommandQueue::TryEnqueue(const SequencedCommand& command) {
  if (const auto validated = Validate(command, last_offered_); !validated.has_value()) {
    return std::unexpected(validated.error());
  }
  last_offered_ = command.sequence;
  queued_.push_back(command);
  if (queued_.size() > kMaxQueuedCommands) {
    queued_.pop_front();
    return Enqueued::kDroppedOldest;
  }
  return Enqueued::kQueued;
}

TickCommand CommandQueue::Next() {
  if (!queued_.empty()) {
    const SequencedCommand next = queued_.front();
    queued_.pop_front();
    last_ = next.command;
    held_ticks_ = 0;
    acknowledged_ = next.sequence;
    return TickCommand{.command = next.command, .acknowledged_sequence = acknowledged_};
  }
  if (!last_.has_value()) {
    TickCommand idle{};
    idle.acknowledged_sequence = acknowledged_;
    return idle;
  }
  command::Command command = WithoutActions(*last_);
  if (held_ticks_ < max_held_ticks_) {
    ++held_ticks_;
  } else {
    command.movement.direction = math::Vec3{};
    command.movement.sprint = false;
  }
  return TickCommand{.command = command, .acknowledged_sequence = acknowledged_};
}

}  // namespace augusta::server
