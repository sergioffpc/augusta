#include "wire.h"

#include <cstdint>

namespace augusta::server {

namespace {

// The protocol numbers its stances as the engine does; a stance added to one
// and not the other breaks the build here, not the wire.
static_assert(static_cast<std::uint8_t>(physics::Stance::kStanding) ==
              static_cast<std::uint8_t>(protocol::Stance::kStanding));
static_assert(static_cast<std::uint8_t>(physics::Stance::kCrouching) ==
              static_cast<std::uint8_t>(protocol::Stance::kCrouching));
static_assert(static_cast<std::uint8_t>(physics::Stance::kProne) ==
              static_cast<std::uint8_t>(protocol::Stance::kProne));

protocol::Stance ToWire(physics::Stance stance) { return static_cast<protocol::Stance>(stance); }

physics::Stance FromWire(protocol::Stance stance) { return static_cast<physics::Stance>(stance); }

}  // namespace

protocol::BodyState ToWire(const physics::BodyState& body) {
  return protocol::BodyState{
      .position = body.position,
      .velocity = body.velocity,
      .stamina = body.stamina,
      .stance = ToWire(body.stance),
  };
}

protocol::Parameters ToWire(const parameters::Parameters& parameters) {
  return protocol::Parameters{
      .stamina =
          {
              .deplete_per_second = parameters.stamina.deplete_per_second,
              .regen_per_second = parameters.stamina.regen_per_second,
              .forced_walk_below = parameters.stamina.forced_walk_below,
          },
  };
}

protocol::PlayerState ToWire(const RosterEntry& entry) {
  return protocol::PlayerState{.session = entry.session, .body = ToWire(entry.body)};
}

protocol::AuthoritativeState ToWire(const replication::Update& update) {
  protocol::AuthoritativeState state{.tick = update.tick, .acknowledged_sequence = update.acknowledged_sequence};
  state.players.reserve(update.players.size());
  for (const replication::PlayerBody& player : update.players) {
    state.players.push_back(protocol::PlayerState{.session = player.session, .body = ToWire(player.body)});
  }
  return state;
}

input::Command FromWire(const protocol::Command& command) {
  input::Command result;
  result.movement.direction = command.direction;
  result.movement.sprint = (command.flags & protocol::Command::kSprint) != 0;
  result.movement.desired_stance = FromWire(command.desired_stance);
  result.yaw = command.yaw;
  result.pitch = command.pitch;
  result.ads = (command.flags & protocol::Command::kAds) != 0;
  result.fire = (command.flags & protocol::Command::kFire) != 0;
  result.reload = (command.flags & protocol::Command::kReload) != 0;
  return result;
}

SequencedCommand FromWire(const protocol::SequencedCommand& command) {
  return SequencedCommand{.sequence = command.sequence, .command = FromWire(command.command)};
}

}  // namespace augusta::server
