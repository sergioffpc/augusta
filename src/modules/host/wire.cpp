#include "augusta/wire.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/command_queue.h"
#include "augusta/failure.h"
#include "augusta/host_metrics.h"
#include "augusta/host_wire.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replay.h"
#include "augusta/replay_catalog.h"
#include "augusta/replication.h"
#include "augusta/simulation_mapping.h"
#include "augusta/tick.h"

namespace augusta::host {

namespace {

// The shared core's own types convert as the client converts them.
using wire::FromWire;
using wire::ToWire;

protocol::BodyPartWire ToWire(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return protocol::BodyPartWire::kHead;
    case ballistics::BodyPart::kTorso:
      return protocol::BodyPartWire::kTorso;
    case ballistics::BodyPart::kLimb:
      return protocol::BodyPartWire::kLimb;
  }
  // A part none of the above names is a corrupted one: it travels on as a
  // value the protocol lacks, which Encode refuses (ADR-0033).
  return protocol::BodyPartWire{};
}

// A payload the protocol could not carry, of the message type named under
// key: a broken invariant of the server's, never a peer's input.
failure::Failure BrokenInvariant(protocol::EncodeError error, std::string_view key, std::uint8_t type) {
  return failure::Failure{.code = failure::Code::kInvariantViolated,
                          .context = {{.key = std::string(key), .value = std::to_string(type)}},
                          .detail = std::string(protocol::DescribeEncodeError(error))};
}

}  // namespace

std::expected<protocol::BytesWire, failure::Failure> EncodeToSend(const protocol::MessageWire& message) {
  return protocol::Encode(message).transform_error([&message](protocol::EncodeError error) {
    return BrokenInvariant(error, "message_type", static_cast<std::uint8_t>(protocol::TypeOf(message)));
  });
}

protocol::EntityIdWire ToWire(EntityId entity) {
  return static_cast<protocol::EntityIdWire>(static_cast<std::uint32_t>(entity));
}

protocol::SessionIdWire ToWire(SessionId session) {
  return static_cast<protocol::SessionIdWire>(static_cast<std::uint32_t>(session));
}

MessageType TypeOf(std::span<const std::byte> payload) {
  switch (static_cast<protocol::MessageTypeWire>(payload.front())) {
    case protocol::MessageTypeWire::kJoinRequest:
      return MessageType::kJoinRequest;
    case protocol::MessageTypeWire::kJoinAccepted:
      return MessageType::kJoinAccepted;
    case protocol::MessageTypeWire::kJoinRefused:
      return MessageType::kJoinRefused;
    case protocol::MessageTypeWire::kCommands:
      return MessageType::kCommands;
    case protocol::MessageTypeWire::kAuthoritativeState:
      return MessageType::kAuthoritativeState;
    case protocol::MessageTypeWire::kLobby:
      return MessageType::kLobby;
    case protocol::MessageTypeWire::kReady:
      return MessageType::kReady;
    case protocol::MessageTypeWire::kMatchStart:
      return MessageType::kMatchStart;
    case protocol::MessageTypeWire::kMatchEnd:
      return MessageType::kMatchEnd;
    case protocol::MessageTypeWire::kShot:
      return MessageType::kShot;
    case protocol::MessageTypeWire::kHitConfirmation:
      return MessageType::kHitConfirmation;
    case protocol::MessageTypeWire::kDeath:
      return MessageType::kDeath;
    case protocol::MessageTypeWire::kReplayListRequest:
      return MessageType::kReplayListRequest;
    case protocol::MessageTypeWire::kReplayList:
      return MessageType::kReplayList;
    case protocol::MessageTypeWire::kReplayRequest:
      return MessageType::kReplayRequest;
    case protocol::MessageTypeWire::kReplayView:
      return MessageType::kReplayView;
    case protocol::MessageTypeWire::kReenactRequest:
      return MessageType::kReenactRequest;
  }
  std::unreachable();
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
    case JoinRefusal::kReplayServer:
      return protocol::JoinRefusalWire::kReplayServer;
    case JoinRefusal::kUnknownCapture:
      return protocol::JoinRefusalWire::kUnknownCapture;
    case JoinRefusal::kNotAReplayServer:
      return protocol::JoinRefusalWire::kNotAReplayServer;
    case JoinRefusal::kReenactmentsNotAccepted:
      return protocol::JoinRefusalWire::kReenactmentsNotAccepted;
  }
  // As ToWire(BodyPart): a value the protocol lacks, which Encode refuses.
  return protocol::JoinRefusalWire{};
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
      .flags = body.exhausted ? protocol::BodyStateWire::kExhausted : std::uint8_t{0},
      .stance = ToWire(body.stance),
  };
}

protocol::ParametersWire ToWire(const parameters::Parameters& parameters) {
  const parameters::Rifle& rifle = parameters.rifle;
  protocol::RifleWire rifle_wire{
      .rounds_per_minute = rifle.rounds_per_minute,
      .muzzle_velocity = rifle.muzzle_velocity,
      .reload_seconds = rifle.reload_seconds,
      .recoil_recovery_per_second = rifle.recoil_recovery_per_second,
      .ads_recoil_scale = rifle.ads_recoil_scale,
      .ads_field_of_view = rifle.ads_field_of_view,
      .recoil_pattern = {},
      .magazine_capacity = rifle.magazine_capacity,
  };
  rifle_wire.recoil_pattern.reserve(rifle.recoil_pattern.size());
  for (const parameters::RecoilKick& kick : rifle.recoil_pattern) {
    rifle_wire.recoil_pattern.push_back(protocol::RecoilKickWire{.pitch = kick.pitch, .yaw = kick.yaw});
  }
  const parameters::Ammo& ammo = parameters.ammo;
  return protocol::ParametersWire{
      .stamina =
          {
              .deplete_per_second = parameters.stamina.deplete_per_second,
              .regen_per_second = parameters.stamina.regen_per_second,
              .forced_walk_below = parameters.stamina.forced_walk_below,
          },
      .rifle = std::move(rifle_wire),
      .ammo =
          {
              .gravity = ammo.gravity,
              .max_range = ammo.max_range,
              .head_damage = ammo.damage.head,
              .torso_damage = ammo.damage.torso,
              .limb_damage = ammo.damage.limb,
          },
      .starting_health = parameters.starting_health,
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

protocol::MatchStartWire ToWire(const MatchStart& start, std::span<const math::Vec3> spawns, tick::Tick first_tick) {
  protocol::MatchStartWire message;
  message.first_tick = first_tick;
  message.players.reserve(start.players.size());
  for (std::size_t i = 0; i < start.players.size(); ++i) {
    const MatchPlayer& player = start.players[i];
    message.players.push_back(protocol::MatchPlayerWire{.spawn = spawns[i],
                                                        .session = ToWire(player.session),
                                                        .entity = ToWire(player.entity),
                                                        .character = player.character});
  }
  return message;
}

protocol::MatchEndWire ToWire(const MatchEnd& end) {
  return protocol::MatchEndWire{.winner = end.winner.has_value() ? ToWire(*end.winner) : protocol::kDraw};
}

protocol::AuthoritativeStateWire ToWire(const replication::Updates& updates) {
  protocol::AuthoritativeStateWire state{.tick = updates.tick, .bodies = {}};
  state.bodies.reserve(updates.bodies.size());
  for (const replication::EntityBody& body : updates.bodies) {
    state.bodies.push_back(protocol::EntityStateWire{
        .entity = ToWire(FromSimulation(body.entity)), .body = ToWire(body.body), .yaw = body.yaw});
  }
  return state;
}

void Address(protocol::AuthoritativeStateWire& state, const replication::RecipientUpdate& recipient) {
  state.rifle = {.cooldown = recipient.rifle.cooldown,
                 .reload_remaining = recipient.rifle.reload_remaining,
                 .recoil_pitch = recipient.rifle.recoil.pitch,
                 .recoil_yaw = recipient.rifle.recoil.yaw,
                 .rounds = recipient.rifle.rounds,
                 .burst_index = recipient.rifle.burst_index};
  state.health = recipient.health;
  state.acknowledged_sequence = recipient.acknowledged_sequence;
  state.queued_commands = recipient.queued_commands;
}

protocol::ShotWire ToWire(const replication::Shot& shot) {
  return protocol::ShotWire{
      .tick = shot.tick,
      .origin = shot.origin,
      .shooter = ToWire(FromSimulation(shot.shooter)),
      .yaw = shot.yaw,
      .pitch = shot.pitch,
  };
}

protocol::HitConfirmationWire ToWire(const replication::HitConfirmation& hit) {
  return protocol::HitConfirmationWire{
      .target = ToWire(FromSimulation(hit.target)),
      .damage = hit.damage,
      .part = ToWire(hit.part),
  };
}

protocol::DeathWire ToWire(const replication::Death& death) {
  return protocol::DeathWire{
      .victim = ToWire(FromSimulation(death.victim)),
      .killer = ToWire(FromSimulation(death.killer)),
      .yaw = death.yaw,
      .pitch = death.pitch,
      .part = ToWire(death.part),
  };
}

protocol::ReplayListingWire ToWire(const ReplayListing& listing) {
  return protocol::ReplayListingWire{.started_unix_ms = listing.started.time_since_epoch().count(),
                                     .characters = listing.characters,
                                     .name = listing.name,
                                     .ticks = listing.ticks,
                                     .tick_rate_hz = listing.tick_rate_hz};
}

protocol::ReplayListWire ToWire(const std::vector<ReplayListing>& listings) {
  protocol::ReplayListWire list;
  list.replays.reserve(listings.size());
  for (const ReplayListing& listing : listings) {
    list.replays.push_back(ToWire(listing));
  }
  return list;
}

protocol::ReplayViewWire ToWire(const std::vector<PlayerView>& views, tick::Tick tick) {
  protocol::ReplayViewWire message{.tick = tick, .players = {}};
  message.players.reserve(views.size());
  for (const PlayerView& view : views) {
    message.players.push_back(
        protocol::PlayerViewWire{.pitch = view.pitch,
                                 .entity = ToWire(view.entity),
                                 .flags = static_cast<std::uint8_t>(view.ads ? protocol::PlayerViewWire::kAds : 0U)});
  }
  return message;
}

ReplayRequest FromWire(const protocol::ReplayRequestWire& request) {
  return ReplayRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .capture = request.capture,
  };
}

JoinRequest FromWire(const protocol::JoinRequestWire& request) {
  return JoinRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .character = request.character,
  };
}

JoinRequest FromWire(const protocol::ReenactRequestWire& request) {
  return JoinRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .character = request.character,
      .spawn = request.spawn,
  };
}

SequencedCommand FromWire(const protocol::SequencedCommandWire& command, tick::Tick seen_tick) {
  return SequencedCommand{.sequence = command.sequence, .command = FromWire(command.command, seen_tick)};
}

std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message) {
  std::vector<SequencedCommand> commands;
  commands.reserve(message.commands.size());
  for (const protocol::SequencedCommandWire& command : message.commands) {
    commands.push_back(FromWire(command, message.seen_tick));
  }
  return commands;
}

}  // namespace augusta::host
