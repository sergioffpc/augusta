#include "wire.h"

#include <cstdint>

namespace augusta::server {

namespace {

// The protocol numbers its stances as the engine does; a stance added to one
// and not the other breaks the build here, not the wire.
static_assert(static_cast<std::uint8_t>(physics::Stance::kStanding) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kStanding));
static_assert(static_cast<std::uint8_t>(physics::Stance::kCrouching) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kCrouching));
static_assert(static_cast<std::uint8_t>(physics::Stance::kProne) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kProne));

protocol::StanceWire ToWire(physics::Stance stance) { return static_cast<protocol::StanceWire>(stance); }

physics::Stance FromWire(protocol::StanceWire stance) { return static_cast<physics::Stance>(stance); }

}  // namespace

protocol::BodyStateWire ToWire(const physics::BodyState& body) {
  return protocol::BodyStateWire{
      .position = body.position,
      .velocity = body.velocity,
      .stamina = body.stamina,
      .stance = ToWire(body.stance),
  };
}

protocol::ParametersWire ToWire(const parameters::Parameters& parameters) {
  return protocol::ParametersWire{
      .stamina =
          {
              .deplete_per_second = parameters.stamina.deplete_per_second,
              .regen_per_second = parameters.stamina.regen_per_second,
              .forced_walk_below = parameters.stamina.forced_walk_below,
          },
      .player_count = parameters.player_count,
  };
}

protocol::PlayerStateWire ToWire(const RosterEntry& entry) {
  return protocol::PlayerStateWire{.session = entry.session, .body = ToWire(entry.body)};
}

protocol::AuthoritativeStateWire ToWire(const replication::Update& update) {
  protocol::AuthoritativeStateWire state{
      .tick = update.tick,
      .acknowledged_sequence = update.acknowledged_sequence,
      .players = {},
  };
  state.players.reserve(update.players.size());
  for (const replication::PlayerBody& player : update.players) {
    state.players.push_back(protocol::PlayerStateWire{.session = player.session, .body = ToWire(player.body)});
  }
  return state;
}

input::Command FromWire(const protocol::CommandWire& command) {
  input::Command result;
  result.movement.direction = command.direction;
  result.movement.sprint = (command.flags & protocol::CommandWire::kSprint) != 0;
  result.movement.desired_stance = FromWire(command.desired_stance);
  result.yaw = command.yaw;
  result.pitch = command.pitch;
  result.ads = (command.flags & protocol::CommandWire::kAds) != 0;
  result.fire = (command.flags & protocol::CommandWire::kFire) != 0;
  result.reload = (command.flags & protocol::CommandWire::kReload) != 0;
  return result;
}

SequencedCommand FromWire(const protocol::SequencedCommandWire& command) {
  return SequencedCommand{.sequence = command.sequence, .command = FromWire(command.command)};
}

}  // namespace augusta::server
