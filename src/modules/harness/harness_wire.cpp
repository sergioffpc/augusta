#include "augusta/harness_wire.h"

#include <cstdint>

namespace augusta::harness {

namespace {

// The protocol numbers its stances as the engine does; a stance added to one
// and not the other breaks the build here, not the wire.
static_assert(static_cast<std::uint8_t>(physics::Stance::kStanding) ==
              static_cast<std::uint8_t>(protocol::Stance::kStanding));
static_assert(static_cast<std::uint8_t>(physics::Stance::kCrouching) ==
              static_cast<std::uint8_t>(protocol::Stance::kCrouching));
static_assert(static_cast<std::uint8_t>(physics::Stance::kProne) ==
              static_cast<std::uint8_t>(protocol::Stance::kProne));

physics::Stance FromWire(protocol::Stance stance) { return static_cast<physics::Stance>(stance); }

protocol::Stance ToWire(physics::Stance stance) { return static_cast<protocol::Stance>(stance); }

}  // namespace

physics::BodyState FromWire(const protocol::BodyState& body) {
  physics::BodyState result;
  result.position = body.position;
  result.velocity = body.velocity;
  result.stance = FromWire(body.stance);
  result.stamina = body.stamina;
  return result;
}

parameters::Parameters FromWire(const protocol::Parameters& parameters) {
  parameters::Parameters result;
  result.stamina.deplete_per_second = parameters.stamina.deplete_per_second;
  result.stamina.regen_per_second = parameters.stamina.regen_per_second;
  result.stamina.forced_walk_below = parameters.stamina.forced_walk_below;
  return result;
}

PlayerBody FromWire(const protocol::PlayerState& player) {
  return PlayerBody{.session = player.session, .body = FromWire(player.body)};
}

AuthoritativeState FromWire(const protocol::AuthoritativeState& state) {
  AuthoritativeState result{.tick = state.tick, .acknowledged_sequence = state.acknowledged_sequence};
  result.players.reserve(state.players.size());
  for (const protocol::PlayerState& player : state.players) {
    result.players.push_back(FromWire(player));
  }
  return result;
}

protocol::Command ToWire(const input::Command& command) {
  std::uint8_t flags = 0;
  if (command.movement.sprint) {
    flags |= protocol::Command::kSprint;
  }
  if (command.ads) {
    flags |= protocol::Command::kAds;
  }
  if (command.fire) {
    flags |= protocol::Command::kFire;
  }
  if (command.reload) {
    flags |= protocol::Command::kReload;
  }
  return protocol::Command{
      .direction = command.movement.direction,
      .yaw = command.yaw,
      .pitch = command.pitch,
      .flags = flags,
      .desired_stance = ToWire(command.movement.desired_stance),
  };
}

}  // namespace augusta::harness
