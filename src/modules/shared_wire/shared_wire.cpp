#include "augusta/shared_wire.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"

namespace augusta::wire {

// The protocol numbers its stances as the engine does; a stance added to one
// and not the other breaks the build here, not the wire.
static_assert(static_cast<std::uint8_t>(physics::Stance::kStanding) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kStanding));
static_assert(static_cast<std::uint8_t>(physics::Stance::kCrouching) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kCrouching));
static_assert(static_cast<std::uint8_t>(physics::Stance::kProne) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kProne));

// The protocol carries a pack's hash as the assets module computes it.
static_assert(protocol::kPackHashSize == assets::kPackHashSize);

protocol::StanceWire ToWire(physics::Stance stance) { return static_cast<protocol::StanceWire>(stance); }

physics::Stance FromWire(protocol::StanceWire stance) { return static_cast<physics::Stance>(stance); }

protocol::CommandWire ToWire(const command::Command& command, tick::Tick seen_tick) {
  const tick::Tick age = seen_tick > command.seen_tick ? seen_tick - command.seen_tick : 0U;
  std::uint8_t flags = 0;
  flags |= command.movement.sprint ? protocol::CommandWire::kSprint : std::uint8_t{0};
  flags |= command.ads ? protocol::CommandWire::kAds : std::uint8_t{0};
  flags |= command.fire ? protocol::CommandWire::kFire : std::uint8_t{0};
  flags |= command.reload ? protocol::CommandWire::kReload : std::uint8_t{0};
  return protocol::CommandWire{
      .direction = command.movement.direction,
      .yaw = command.yaw,
      .pitch = command.pitch,
      .seen_fraction = command.seen_fraction,
      .flags = flags,
      .desired_stance = ToWire(command.movement.desired_stance),
      .seen_age = static_cast<std::uint8_t>(std::min<tick::Tick>(age, std::numeric_limits<std::uint8_t>::max())),
  };
}

command::Command FromWire(const protocol::CommandWire& command, tick::Tick seen_tick) {
  command::Command result;
  result.movement.direction = command.direction;
  result.movement.sprint = (command.flags & protocol::CommandWire::kSprint) != 0;
  result.movement.desired_stance = FromWire(command.desired_stance);
  result.yaw = command.yaw;
  result.pitch = command.pitch;
  result.ads = (command.flags & protocol::CommandWire::kAds) != 0;
  result.fire = (command.flags & protocol::CommandWire::kFire) != 0;
  result.reload = (command.flags & protocol::CommandWire::kReload) != 0;
  result.seen_tick = seen_tick - std::min<tick::Tick>(command.seen_age, seen_tick);
  result.seen_fraction = command.seen_fraction;
  return result;
}

protocol::PackHashWire ToWire(const assets::PackHash& hash) {
  protocol::PackHashWire wire{};
  std::ranges::copy(hash, wire.begin());
  return wire;
}

assets::PackHash FromWire(const protocol::PackHashWire& hash) {
  assets::PackHash result{};
  std::ranges::copy(hash, result.begin());
  return result;
}

}  // namespace augusta::wire
