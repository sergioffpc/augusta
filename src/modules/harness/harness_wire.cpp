#include "augusta/harness_wire.h"

#include <cstdint>

namespace augusta::harness {

namespace {

// The protocol numbers its stances as the engine does; a stance added to one
// and not the other breaks the build here, not the wire.
static_assert(static_cast<std::uint8_t>(physics::Stance::kStanding) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kStanding));
static_assert(static_cast<std::uint8_t>(physics::Stance::kCrouching) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kCrouching));
static_assert(static_cast<std::uint8_t>(physics::Stance::kProne) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kProne));

physics::Stance FromWire(protocol::StanceWire stance) { return static_cast<physics::Stance>(stance); }

protocol::StanceWire ToWire(physics::Stance stance) { return static_cast<protocol::StanceWire>(stance); }

}  // namespace

physics::BodyState FromWire(const protocol::BodyStateWire& body) {
  physics::BodyState result;
  result.position = body.position;
  result.velocity = body.velocity;
  result.stance = FromWire(body.stance);
  result.stamina = body.stamina;
  return result;
}

parameters::Parameters FromWire(const protocol::ParametersWire& parameters) {
  parameters::Parameters result;
  result.stamina.deplete_per_second = parameters.stamina.deplete_per_second;
  result.stamina.regen_per_second = parameters.stamina.regen_per_second;
  result.stamina.forced_walk_below = parameters.stamina.forced_walk_below;
  result.player_count = parameters.player_count;
  return result;
}

PlayerBody FromWire(const protocol::PlayerStateWire& player) {
  return PlayerBody{.session = player.session, .body = FromWire(player.body)};
}

AuthoritativeState FromWire(const protocol::AuthoritativeStateWire& state) {
  AuthoritativeState result{
      .tick = state.tick,
      .acknowledged_sequence = state.acknowledged_sequence,
      .players = {},
  };
  result.players.reserve(state.players.size());
  for (const protocol::PlayerStateWire& player : state.players) {
    result.players.push_back(FromWire(player));
  }
  return result;
}

Lobby FromWire(const protocol::LobbyWire& lobby) {
  Lobby result{.version = lobby.version, .roster = {}};
  result.roster.reserve(lobby.roster.size());
  for (const protocol::RosterEntryWire& entry : lobby.roster) {
    result.roster.push_back(RosterEntry{.session = entry.session, .character = entry.character});
  }
  return result;
}

MatchStart FromWire(const protocol::MatchStartWire& start) {
  MatchStart result;
  result.players.reserve(start.players.size());
  for (const protocol::MatchPlayerWire& player : start.players) {
    result.players.push_back(
        MatchPlayer{.session = player.session, .character = player.character, .spawn = player.spawn});
  }
  return result;
}

protocol::CommandWire ToWire(const input::Command& command) {
  std::uint8_t flags = 0;
  if (command.movement.sprint) {
    flags |= protocol::CommandWire::kSprint;
  }
  if (command.ads) {
    flags |= protocol::CommandWire::kAds;
  }
  if (command.fire) {
    flags |= protocol::CommandWire::kFire;
  }
  if (command.reload) {
    flags |= protocol::CommandWire::kReload;
  }
  return protocol::CommandWire{
      .direction = command.movement.direction,
      .yaw = command.yaw,
      .pitch = command.pitch,
      .flags = flags,
      .desired_stance = ToWire(command.movement.desired_stance),
  };
}

}  // namespace augusta::harness
