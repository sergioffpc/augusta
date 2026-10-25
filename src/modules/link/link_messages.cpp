#include "augusta/link_messages.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/link_types.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"
#include "shared_core.h"

namespace augusta::link {

namespace {

// The overloads below join the module's own, as one set.
using link::FromWire;
using link::ToWire;

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

protocol::BodyPartWire ToWire(body_part::BodyPart part) {
  switch (part) {
    case body_part::BodyPart::kHead:
      return protocol::BodyPartWire::kHead;
    case body_part::BodyPart::kTorso:
      return protocol::BodyPartWire::kTorso;
    case body_part::BodyPart::kLimb:
      return protocol::BodyPartWire::kLimb;
  }
  // A part none of the above names is a corrupted one: it travels on as a
  // value the protocol lacks, which Encode refuses (ADR-0033).
  return protocol::BodyPartWire{};
}

body_part::BodyPart FromWire(protocol::BodyPartWire part) {
  switch (part) {
    case protocol::BodyPartWire::kHead:
      return body_part::BodyPart::kHead;
    case protocol::BodyPartWire::kTorso:
      return body_part::BodyPart::kTorso;
    case protocol::BodyPartWire::kLimb:
      return body_part::BodyPart::kLimb;
  }
  // Decode admits only the body parts above.
  std::unreachable();
}

// commands, oldest first, as the one message that carries them; its Seen tick
// is the newest of theirs.
protocol::CommandsWire OneMessage(std::span<const SequencedCommand> commands) {
  protocol::CommandsWire message;
  for (const SequencedCommand& command : commands) {
    message.seen_tick = std::max(message.seen_tick, command.command.seen_tick);
  }
  message.commands.reserve(commands.size());
  for (const SequencedCommand& command : commands) {
    message.commands.push_back(protocol::SequencedCommandWire{.sequence = command.sequence,
                                                              .command = ToWire(command.command, message.seen_tick)});
  }
  return message;
}

}  // namespace

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

std::expected<protocol::BytesWire, failure::Failure> EncodeToSend(const protocol::MessageWire& message) {
  return protocol::Encode(message).transform_error([&message](protocol::EncodeError error) {
    return failure::Failure{
        .code = failure::Code::kInvariantViolated,
        .context = {{.key = "message_type",
                     .value = std::to_string(static_cast<std::uint8_t>(protocol::TypeOf(message)))}},
        .detail = std::string(protocol::DescribeEncodeError(error))};
  });
}

namespace {

// type as the Link names it.
MessageType Named(protocol::MessageTypeWire type) {
  switch (type) {
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
    case protocol::MessageTypeWire::kReenactRequest:
      return MessageType::kReenactRequest;
#if AUGUSTA_DEBUG_TOOLS
    case protocol::MessageTypeWire::kDebugSubscribe:
      return MessageType::kDebugSubscribe;
    case protocol::MessageTypeWire::kShotJudgement:
      return MessageType::kShotJudgement;
#endif
  }
  std::unreachable();
}

}  // namespace

MessageType TypeOf(std::span<const std::byte> payload) { return Named(protocol::TypeOf(payload)); }

MessageType TypeOf(const protocol::MessageWire& message) { return Named(protocol::TypeOf(message)); }

protocol::EntityIdWire ToWire(EntityId entity) {
  return static_cast<protocol::EntityIdWire>(static_cast<std::uint32_t>(entity));
}

EntityId FromWire(protocol::EntityIdWire entity) { return static_cast<EntityId>(static_cast<std::uint32_t>(entity)); }

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
    case protocol::JoinRefusalWire::kReplayServer:
      return JoinRefusal::kReplayServer;
    case protocol::JoinRefusalWire::kUnknownCapture:
      return JoinRefusal::kUnknownCapture;
    case protocol::JoinRefusalWire::kNotAReplayServer:
      return JoinRefusal::kNotAReplayServer;
    case protocol::JoinRefusalWire::kReenactmentsNotAccepted:
      return JoinRefusal::kReenactmentsNotAccepted;
  }
  // Decode admits only the reasons above.
  std::unreachable();
}

protocol::BodyStateWire ToWire(const physics::BodyState& body) {
  return protocol::BodyStateWire{
      .position = body.position,
      .velocity = body.velocity,
      .stamina = body.stamina,
      .flags = static_cast<std::uint8_t>((body.exhausted ? protocol::BodyStateWire::kExhausted : 0U) |
                                         (body.grounded ? protocol::BodyStateWire::kGrounded : 0U)),
      .stance = ToWire(body.stance),
      .stride = body.stride,
      .previous_stance = ToWire(body.previous_stance),
      .stance_progress = body.stance_progress,
  };
}

physics::BodyState FromWire(const protocol::BodyStateWire& body) {
  physics::BodyState result;
  result.position = body.position;
  result.velocity = body.velocity;
  result.stance = FromWire(body.stance);
  result.stamina = body.stamina;
  result.exhausted = (body.flags & protocol::BodyStateWire::kExhausted) != 0;
  result.grounded = (body.flags & protocol::BodyStateWire::kGrounded) != 0;
  result.stride = body.stride;
  result.previous_stance = FromWire(body.previous_stance);
  result.stance_progress = body.stance_progress;
  return result;
}

protocol::ParametersWire ToWire(const parameters::Parameters& parameters) {
  const weapon::Rifle& rifle = parameters.rifle;
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
  for (const weapon::RecoilKick& kick : rifle.recoil_pattern) {
    rifle_wire.recoil_pattern.push_back(protocol::RecoilKickWire{.pitch = kick.pitch, .yaw = kick.yaw});
  }
  const weapon::Ammo& ammo = parameters.ammo;
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
              .gravity = ammo.flight.gravity,
              .max_range = ammo.flight.max_range,
              .head_damage = ammo.damage.head,
              .torso_damage = ammo.damage.torso,
              .limb_damage = ammo.damage.limb,
          },
      .starting_health = parameters.starting_health,
      .player_count = parameters.player_count,
  };
}

parameters::Parameters FromWire(const protocol::ParametersWire& parameters) {
  parameters::Parameters result;
  result.stamina.deplete_per_second = parameters.stamina.deplete_per_second;
  result.stamina.regen_per_second = parameters.stamina.regen_per_second;
  result.stamina.forced_walk_below = parameters.stamina.forced_walk_below;
  const protocol::RifleWire& rifle = parameters.rifle;
  result.rifle.rounds_per_minute = rifle.rounds_per_minute;
  result.rifle.muzzle_velocity = rifle.muzzle_velocity;
  result.rifle.reload_seconds = rifle.reload_seconds;
  result.rifle.recoil_recovery_per_second = rifle.recoil_recovery_per_second;
  result.rifle.ads_recoil_scale = rifle.ads_recoil_scale;
  result.rifle.ads_field_of_view = rifle.ads_field_of_view;
  result.rifle.recoil_pattern.reserve(rifle.recoil_pattern.size());
  for (const protocol::RecoilKickWire& kick : rifle.recoil_pattern) {
    result.rifle.recoil_pattern.push_back(weapon::RecoilKick{.pitch = kick.pitch, .yaw = kick.yaw});
  }
  result.rifle.magazine_capacity = rifle.magazine_capacity;
  result.ammo.flight.gravity = parameters.ammo.gravity;
  result.ammo.flight.max_range = parameters.ammo.max_range;
  result.ammo.damage = {
      .head = parameters.ammo.head_damage,
      .torso = parameters.ammo.torso_damage,
      .limb = parameters.ammo.limb_damage,
  };
  result.starting_health = parameters.starting_health;
  result.player_count = parameters.player_count;
  return result;
}

protocol::JoinAcceptedWire ToWire(const Admission& admission) {
  return protocol::JoinAcceptedWire{
      .session = admission.session,
      .tick_rate_hz = admission.tick_rate_hz,
      .parameters = ToWire(admission.parameters),
      .character = admission.character,
  };
}

Admission FromWire(const protocol::JoinAcceptedWire& accepted) {
  return Admission{
      .session = accepted.session,
      .tick_rate_hz = accepted.tick_rate_hz,
      .parameters = FromWire(accepted.parameters),
      .character = accepted.character,
  };
}

// ADS shares the body's flags with the physics state's exhausted flag (ADR-0038).
protocol::EntityStateWire ToWire(const EntityBody& body) {
  protocol::EntityStateWire result{
      .entity = ToWire(body.entity), .body = ToWire(body.body), .yaw = body.yaw, .pitch = body.pitch};
  result.body.flags |= body.ads ? protocol::BodyStateWire::kAds : std::uint8_t{0};
  return result;
}

EntityBody FromWire(const protocol::EntityStateWire& body) {
  return EntityBody{.entity = FromWire(body.entity),
                    .body = FromWire(body.body),
                    .yaw = body.yaw,
                    .pitch = body.pitch,
                    .ads = (body.body.flags & protocol::BodyStateWire::kAds) != 0};
}

protocol::AuthoritativeStateWire ToWire(const AuthoritativeState& state) {
  protocol::AuthoritativeStateWire result{.tick = state.tick, .bodies = {}};
  result.bodies.reserve(state.bodies.size());
  for (const EntityBody& body : state.bodies) {
    result.bodies.push_back(ToWire(body));
  }
  FillRecipientFields(result, state);
  return result;
}

void FillRecipientFields(protocol::AuthoritativeStateWire& state, const AuthoritativeState& recipient) {
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

AuthoritativeState FromWire(const protocol::AuthoritativeStateWire& state) {
  AuthoritativeState result{
      .tick = state.tick,
      .acknowledged_sequence = state.acknowledged_sequence,
      .bodies = {},
      .rifle = {.cooldown = state.rifle.cooldown,
                .reload_remaining = state.rifle.reload_remaining,
                .recoil = {.pitch = state.rifle.recoil_pitch, .yaw = state.rifle.recoil_yaw},
                .rounds = state.rifle.rounds,
                .burst_index = state.rifle.burst_index},
      .health = state.health,
      .queued_commands = state.queued_commands,
  };
  result.bodies.reserve(state.bodies.size());
  for (const protocol::EntityStateWire& body : state.bodies) {
    result.bodies.push_back(FromWire(body));
  }
  return result;
}

protocol::ShotWire ToWire(const Shot& shot) {
  return protocol::ShotWire{
      .tick = shot.tick,
      .origin = shot.origin,
      .shooter = ToWire(shot.shooter),
      .yaw = shot.yaw,
      .pitch = shot.pitch,
  };
}

Shot FromWire(const protocol::ShotWire& shot) {
  return Shot{
      .shooter = FromWire(shot.shooter),
      .tick = shot.tick,
      .origin = shot.origin,
      .yaw = shot.yaw,
      .pitch = shot.pitch,
  };
}

protocol::HitConfirmationWire ToWire(const HitConfirmation& hit) {
  return protocol::HitConfirmationWire{.target = ToWire(hit.target), .damage = hit.damage, .part = ToWire(hit.part)};
}

HitConfirmation FromWire(const protocol::HitConfirmationWire& hit) {
  return HitConfirmation{.target = FromWire(hit.target), .damage = hit.damage, .part = FromWire(hit.part)};
}

protocol::DeathWire ToWire(const Death& death) {
  return protocol::DeathWire{
      .victim = ToWire(death.victim),
      .killer = ToWire(death.killer),
      .yaw = death.yaw,
      .pitch = death.pitch,
      .part = ToWire(death.part),
  };
}

Death FromWire(const protocol::DeathWire& death) {
  return Death{.victim = FromWire(death.victim),
               .killer = FromWire(death.killer),
               .yaw = death.yaw,
               .pitch = death.pitch,
               .part = FromWire(death.part)};
}

protocol::LobbyWire ToWire(const Lobby& lobby) {
  protocol::LobbyWire result{.version = lobby.version, .roster = {}};
  result.roster.reserve(lobby.roster.size());
  for (const RosterEntry& entry : lobby.roster) {
    result.roster.push_back(protocol::RosterEntryWire{.session = entry.session, .character = entry.character});
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

protocol::MatchStartWire ToWire(const MatchStart& start) {
  protocol::MatchStartWire result;
  result.first_tick = start.first_tick;
  result.match = start.match;
  result.players.reserve(start.players.size());
  for (const MatchPlayer& player : start.players) {
    result.players.push_back(protocol::MatchPlayerWire{.spawn = player.spawn,
                                                       .session = player.session,
                                                       .entity = ToWire(player.entity),
                                                       .character = player.character});
  }
  return result;
}

MatchStart FromWire(const protocol::MatchStartWire& start) {
  MatchStart result;
  result.players.reserve(start.players.size());
  for (const protocol::MatchPlayerWire& player : start.players) {
    result.players.push_back(MatchPlayer{.session = player.session,
                                         .entity = FromWire(player.entity),
                                         .character = player.character,
                                         .spawn = player.spawn});
  }
  result.first_tick = start.first_tick;
  result.match = start.match;
  return result;
}

protocol::MatchEndWire ToWire(const MatchEnd& end) {
  return protocol::MatchEndWire{.winner = end.winner.has_value() ? *end.winner : protocol::kDraw};
}

MatchEnd FromWire(const protocol::MatchEndWire& end) {
  return MatchEnd{.winner = end.winner == protocol::kDraw ? std::nullopt : std::optional(end.winner)};
}

protocol::ReplayListingWire ToWire(const ReplayListing& listing) {
  return protocol::ReplayListingWire{.started_unix_ms = listing.started.time_since_epoch().count(),
                                     .characters = listing.characters,
                                     .name = listing.name,
                                     .ticks = listing.ticks,
                                     .tick_rate_hz = listing.tick_rate_hz};
}

ReplayListing FromWire(const protocol::ReplayListingWire& listing) {
  return ReplayListing{
      .name = listing.name,
      .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(listing.started_unix_ms)),
      .ticks = listing.ticks,
      .tick_rate_hz = listing.tick_rate_hz,
      .characters = listing.characters};
}

protocol::ReplayListWire ToWire(const std::vector<ReplayListing>& listings) {
  protocol::ReplayListWire list;
  list.replays.reserve(listings.size());
  for (const ReplayListing& listing : listings) {
    list.replays.push_back(ToWire(listing));
  }
  return list;
}

std::vector<ReplayListing> FromWire(const protocol::ReplayListWire& list) {
  std::vector<ReplayListing> listings;
  listings.reserve(list.replays.size());
  for (const protocol::ReplayListingWire& listing : list.replays) {
    listings.push_back(FromWire(listing));
  }
  return listings;
}

protocol::ReplayRequestWire ToWire(const ReplayRequest& request) {
  return protocol::ReplayRequestWire{
      .engine_version = request.engine_version,
      .client_pack = ToWire(request.client_pack),
      .capture = request.capture,
      .run = request.run,
  };
}

ReplayRequest FromWire(const protocol::ReplayRequestWire& request) {
  return ReplayRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .capture = request.capture,
      .run = request.run,
  };
}

protocol::MessageWire ToWire(const JoinRequest& request) {
  if (request.spawn.has_value()) {
    return protocol::ReenactRequestWire{
        .engine_version = request.engine_version,
        .client_pack = ToWire(request.client_pack),
        .character = request.character,
        .spawn = *request.spawn,
        .run = request.run,
    };
  }
  return protocol::JoinRequestWire{
      .engine_version = request.engine_version,
      .client_pack = ToWire(request.client_pack),
      .character = request.character,
      .run = request.run,
  };
}

JoinRequest FromWire(const protocol::JoinRequestWire& request) {
  return JoinRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .character = request.character,
      .run = request.run,
  };
}

JoinRequest FromWire(const protocol::ReenactRequestWire& request) {
  return JoinRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .character = request.character,
      .spawn = request.spawn,
      .run = request.run,
  };
}

std::span<const SequencedCommand> Carried(std::span<const SequencedCommand> commands) {
  return commands.last(std::min(commands.size(), protocol::kMaxCommandsPerMessage));
}

protocol::CommandsWire ToWire(std::span<const SequencedCommand> commands) { return OneMessage(Carried(commands)); }

std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message) {
  std::vector<SequencedCommand> commands;
  commands.reserve(message.commands.size());
  for (const protocol::SequencedCommandWire& command : message.commands) {
    commands.push_back(
        SequencedCommand{.sequence = command.sequence, .command = FromWire(command.command, message.seen_tick)});
  }
  return commands;
}

protocol::ReadyWire ToWire(const Ready& ready) { return protocol::ReadyWire{.version = ready.version}; }

protocol::ReplayListRequestWire ToWire(ReplayListRequest /*request*/) { return protocol::ReplayListRequestWire{}; }

#if AUGUSTA_DEBUG_TOOLS
// The flags travel as they are: the protocol's bits are the Link's.
static_assert(DebugSubscribe::kShotJudgements == protocol::DebugSubscribeWire::kShotJudgements);

protocol::DebugSubscribeWire ToWire(DebugSubscribe subscribe) {
  return protocol::DebugSubscribeWire{.flags = subscribe.flags};
}

namespace {

// The protocol numbers what a round struck as the Link does.
static_assert(static_cast<std::uint8_t>(Struck::kNothing) == static_cast<std::uint8_t>(protocol::StruckWire::kNothing));
static_assert(static_cast<std::uint8_t>(Struck::kMap) == static_cast<std::uint8_t>(protocol::StruckWire::kMap));
static_assert(static_cast<std::uint8_t>(Struck::kProp) == static_cast<std::uint8_t>(protocol::StruckWire::kProp));
static_assert(static_cast<std::uint8_t>(Struck::kHitbox) == static_cast<std::uint8_t>(protocol::StruckWire::kHitbox));

}  // namespace

protocol::ShotJudgementWire ToWire(const ShotJudgement& judgement) {
  const float delay =
      std::clamp(judgement.shooters_delay, 0.0F, static_cast<float>(std::numeric_limits<std::uint8_t>::max()));
  const float whole = std::floor(delay);
  protocol::ShotJudgementWire result{
      .shot_tick = judgement.shot_tick,
      .entries = {},
      .point = judgement.point,
      .target = judgement.target.has_value() ? ToWire(*judgement.target) : protocol::EntityIdWire{0},
      .delay_fraction = delay - whole,
      .delay_ticks = static_cast<std::uint8_t>(whole),
      // A value no case names, a corrupted one, travels on as one the protocol
      // lacks, which Encode refuses.
      .struck = static_cast<protocol::StruckWire>(judgement.struck),
      .part = ToWire(judgement.part),
      .flags = judgement.entries_left_out ? protocol::ShotJudgementWire::kEntriesLeftOut : std::uint8_t{0},
  };
  result.entries.reserve(judgement.entries.size());
  for (const ShotJudgementEntry& entry : judgement.entries) {
    protocol::ShotJudgementEntryWire& written = result.entries.emplace_back(
        protocol::ShotJudgementEntryWire{.players = {}, .ticks_after_shot = entry.ticks_after_shot});
    written.players.reserve(entry.players.size());
    for (const JudgedPlayer& player : entry.players) {
      written.players.push_back(protocol::JudgedPlayerWire{.position = player.position,
                                                           .entity = ToWire(player.entity),
                                                           .yaw = player.yaw,
                                                           .stance = ToWire(player.stance)});
    }
  }
  return result;
}

ShotJudgement FromWire(const protocol::ShotJudgementWire& judgement) {
  ShotJudgement result{
      .shot_tick = judgement.shot_tick,
      .shooters_delay = static_cast<float>(judgement.delay_ticks) + judgement.delay_fraction,
      .struck = static_cast<Struck>(judgement.struck),
      .part = FromWire(judgement.part),
      .point = judgement.point,
      .target =
          judgement.target == protocol::EntityIdWire{0} ? std::nullopt : std::optional(FromWire(judgement.target)),
      .entries = {},
      .entries_left_out = (judgement.flags & protocol::ShotJudgementWire::kEntriesLeftOut) != 0,
  };
  result.entries.reserve(judgement.entries.size());
  for (const protocol::ShotJudgementEntryWire& entry : judgement.entries) {
    ShotJudgementEntry& read =
        result.entries.emplace_back(ShotJudgementEntry{.ticks_after_shot = entry.ticks_after_shot, .players = {}});
    read.players.reserve(entry.players.size());
    for (const protocol::JudgedPlayerWire& player : entry.players) {
      read.players.push_back(JudgedPlayer{.entity = FromWire(player.entity),
                                          .position = player.position,
                                          .stance = FromWire(player.stance),
                                          .yaw = player.yaw});
    }
  }
  return result;
}
#endif

std::optional<ServerMessage> ServerMessageFromWire(const protocol::MessageWire& message) {
  if (const auto* accepted = std::get_if<protocol::JoinAcceptedWire>(&message)) {
    return FromWire(*accepted);
  }
  if (const auto* refused = std::get_if<protocol::JoinRefusedWire>(&message)) {
    return FromWire(refused->reason);
  }
  if (const auto* state = std::get_if<protocol::AuthoritativeStateWire>(&message)) {
    return FromWire(*state);
  }
  if (const auto* shot = std::get_if<protocol::ShotWire>(&message)) {
    return FromWire(*shot);
  }
  if (const auto* hit = std::get_if<protocol::HitConfirmationWire>(&message)) {
    return FromWire(*hit);
  }
  if (const auto* death = std::get_if<protocol::DeathWire>(&message)) {
    return FromWire(*death);
  }
  if (const auto* lobby = std::get_if<protocol::LobbyWire>(&message)) {
    return FromWire(*lobby);
  }
  if (const auto* start = std::get_if<protocol::MatchStartWire>(&message)) {
    return FromWire(*start);
  }
  if (const auto* end = std::get_if<protocol::MatchEndWire>(&message)) {
    return FromWire(*end);
  }
  if (const auto* list = std::get_if<protocol::ReplayListWire>(&message)) {
    return FromWire(*list);
  }
#if AUGUSTA_DEBUG_TOOLS
  if (const auto* judgement = std::get_if<protocol::ShotJudgementWire>(&message)) {
    return FromWire(*judgement);
  }
#endif
  return std::nullopt;
}

std::optional<ClientMessage> ClientMessageFromWire(const protocol::MessageWire& message) {
  if (const auto* request = std::get_if<protocol::JoinRequestWire>(&message)) {
    return FromWire(*request);
  }
  if (const auto* reenact = std::get_if<protocol::ReenactRequestWire>(&message)) {
    return FromWire(*reenact);
  }
  if (const auto* commands = std::get_if<protocol::CommandsWire>(&message)) {
    return FromWire(*commands);
  }
  if (const auto* ready = std::get_if<protocol::ReadyWire>(&message)) {
    return Ready{.version = ready->version};
  }
  if (const auto* request = std::get_if<protocol::ReplayRequestWire>(&message)) {
    return FromWire(*request);
  }
  if (std::holds_alternative<protocol::ReplayListRequestWire>(message)) {
    return ReplayListRequest{};
  }
#if AUGUSTA_DEBUG_TOOLS
  if (const auto* subscribe = std::get_if<protocol::DebugSubscribeWire>(&message)) {
    return DebugSubscribe{.flags = subscribe->flags};
  }
#endif
  return std::nullopt;
}

}  // namespace augusta::link
