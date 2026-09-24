#include "wire.h"

#include <algorithm>
#include <cstdint>
#include <utility>

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

// The protocol carries a pack's hash as the assets module computes it.
static_assert(protocol::kPackHashSize == assets::kPackHashSize);

}  // namespace

protocol::SessionIdWire ToWire(identity::SessionId session) {
  return static_cast<protocol::SessionIdWire>(static_cast<std::uint32_t>(session));
}

protocol::JoinRefusalWire ToWire(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return protocol::JoinRefusalWire::kVersionMismatch;
    case JoinRefusal::kLobbyFull:
      return protocol::JoinRefusalWire::kLobbyFull;
    case JoinRefusal::kUnknownCharacter:
      return protocol::JoinRefusalWire::kUnknownCharacter;
    case JoinRefusal::kMatchInProgress:
      return protocol::JoinRefusalWire::kMatchInProgress;
    case JoinRefusal::kPackMismatch:
      return protocol::JoinRefusalWire::kPackMismatch;
  }
  std::unreachable();
}

protocol::JoinAcceptedWire ToWire(const Admission& admission, std::uint8_t tick_rate_hz,
                                  const parameters::Parameters& parameters) {
  return protocol::JoinAcceptedWire{
      .session = ToWire(admission.session),
      .tick_rate_hz = tick_rate_hz,
      .parameters = ToWire(parameters),
      .character = admission.character,
  };
}

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

protocol::LobbyWire ToWire(const Roster& roster) {
  protocol::LobbyWire lobby{.version = roster.version, .roster = {}};
  lobby.roster.reserve(roster.players.size());
  for (const RosterEntry& entry : roster.players) {
    lobby.roster.push_back(protocol::RosterEntryWire{.session = ToWire(entry.session), .character = entry.character});
  }
  return lobby;
}

protocol::MatchStartWire ToWire(const MatchStart& start) {
  protocol::MatchStartWire message;
  message.players.reserve(start.players.size());
  for (const MatchPlayer& player : start.players) {
    message.players.push_back(protocol::MatchPlayerWire{
        .spawn = player.spawn, .session = ToWire(player.session), .character = player.character});
  }
  return message;
}

protocol::AuthoritativeStateWire ToWire(const replication::Update& update) {
  protocol::AuthoritativeStateWire state{
      .tick = update.tick,
      .acknowledged_sequence = update.acknowledged_sequence,
      .players = {},
  };
  state.players.reserve(update.players.size());
  for (const replication::PlayerBody& player : update.players) {
    state.players.push_back(protocol::PlayerStateWire{.session = ToWire(player.session), .body = ToWire(player.body)});
  }
  return state;
}

assets::PackHash FromWire(const protocol::PackHashWire& hash) {
  assets::PackHash result{};
  std::ranges::copy(hash, result.begin());
  return result;
}

JoinRequest FromWire(const protocol::JoinRequestWire& request) {
  return JoinRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .character = request.character,
  };
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

std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message) {
  std::vector<SequencedCommand> commands;
  commands.reserve(message.commands.size());
  for (const protocol::SequencedCommandWire& command : message.commands) {
    commands.push_back(FromWire(command));
  }
  return commands;
}

}  // namespace augusta::server
