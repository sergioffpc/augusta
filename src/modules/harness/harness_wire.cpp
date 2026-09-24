#include "augusta/harness_wire.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

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

// The protocol carries a pack's hash as the assets module computes it.
static_assert(protocol::kPackHashSize == assets::kPackHashSize);

}  // namespace

SessionId FromWire(protocol::SessionIdWire session) {
  return static_cast<SessionId>(static_cast<std::uint32_t>(session));
}

JoinRefusal FromWire(protocol::JoinRefusalWire reason) {
  switch (reason) {
    case protocol::JoinRefusalWire::kVersionMismatch:
      return JoinRefusal::kVersionMismatch;
    case protocol::JoinRefusalWire::kLobbyFull:
      return JoinRefusal::kLobbyFull;
    case protocol::JoinRefusalWire::kUnknownCharacter:
      return JoinRefusal::kUnknownCharacter;
    case protocol::JoinRefusalWire::kMatchInProgress:
      return JoinRefusal::kMatchInProgress;
    case protocol::JoinRefusalWire::kPackMismatch:
      return JoinRefusal::kPackMismatch;
  }
  // Decode admits only the reasons above.
  std::unreachable();
}

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

Admission FromWire(const protocol::JoinAcceptedWire& accepted) {
  return Admission{
      .session = FromWire(accepted.session),
      .tick_rate_hz = accepted.tick_rate_hz,
      .parameters = FromWire(accepted.parameters),
      .character = accepted.character,
  };
}

PlayerBody FromWire(const protocol::PlayerStateWire& player) {
  return PlayerBody{.session = FromWire(player.session), .body = FromWire(player.body)};
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
    result.roster.push_back(RosterEntry{.session = FromWire(entry.session), .character = entry.character});
  }
  return result;
}

MatchStart FromWire(const protocol::MatchStartWire& start) {
  MatchStart result;
  result.players.reserve(start.players.size());
  for (const protocol::MatchPlayerWire& player : start.players) {
    result.players.push_back(
        MatchPlayer{.session = FromWire(player.session), .character = player.character, .spawn = player.spawn});
  }
  return result;
}

protocol::PackHashWire ToWire(const assets::PackHash& hash) {
  protocol::PackHashWire result{};
  std::ranges::copy(hash, result.begin());
  return result;
}

protocol::JoinRequestWire ToWire(const JoinRequest& request) {
  return protocol::JoinRequestWire{
      .engine_version = request.engine_version,
      .client_pack = ToWire(request.client_pack),
      .character = request.character,
  };
}

protocol::CommandWire ToWire(const command::Command& command) {
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

protocol::CommandsWire ToWire(std::span<const SequencedCommand> commands) {
  protocol::CommandsWire message;
  message.commands.reserve(commands.size());
  for (const SequencedCommand& command : commands) {
    message.commands.push_back(
        protocol::SequencedCommandWire{.sequence = command.sequence, .command = ToWire(command.command)});
  }
  return message;
}

}  // namespace augusta::harness
