#include "wire.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "capture.h"
#include "command_queue.h"
#include "host_metrics.h"
#include "match.h"
#include "recording.h"
#include "simulation_mapping.h"

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

// command as a Commands message carries it, but for its Seen time's tick,
// which whoever stores it keeps beside it: its age is 0.
protocol::CommandWire ToWire(const command::Command& command) {
  std::uint8_t flags = 0;
  flags |= command.movement.sprint ? protocol::CommandWire::kSprint : std::uint8_t{0};
  flags |= command.ads ? protocol::CommandWire::kAds : std::uint8_t{0};
  flags |= command.fire ? protocol::CommandWire::kFire : std::uint8_t{0};
  flags |= command.reload ? protocol::CommandWire::kReload : std::uint8_t{0};
  return protocol::CommandWire{.direction = command.movement.direction,
                               .yaw = command.yaw,
                               .pitch = command.pitch,
                               .seen_fraction = command.seen_fraction,
                               .flags = flags,
                               .desired_stance = ToWire(command.movement.desired_stance),
                               .seen_age = 0};
}

// The protocol carries a pack's hash as the assets module computes it.
static_assert(protocol::kPackHashSize == assets::kPackHashSize);

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

// A payload the protocol could not carry, of the message or record type
// named under key: a broken invariant of the server's, never a peer's input.
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

std::expected<protocol::BytesWire, failure::Failure> EncodeToRecord(const protocol::RecordWire& record) {
  return protocol::EncodeRecord(record).transform_error([&record](protocol::EncodeError error) {
    return BrokenInvariant(error, "record_type", static_cast<std::uint8_t>(protocol::TypeOf(record)));
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

protocol::RecordingHeaderWire ToWire(const RecordingHeader& header) {
  protocol::RecordingHeaderWire wire{
      .server_pack = {}, .engine_version = header.engine_version, .tick_rate_hz = header.tick_rate_hz};
  std::ranges::copy(header.server_pack, wire.server_pack.begin());
  return wire;
}

RecordingHeader FromWire(const protocol::RecordingHeaderWire& header) {
  return RecordingHeader{.engine_version = header.engine_version,
                         .server_pack = FromWire(header.server_pack),
                         .tick_rate_hz = header.tick_rate_hz};
}

namespace {

// A recorded command in full: its Seen time's tick goes beside it, so its age is 0.
protocol::RecordedCommandWire ToWire(const simulation::PlayerCommand& command) {
  return protocol::RecordedCommandWire{
      .seen_tick = command.command.seen_tick,
      .command = ToWire(command.command),
      .entity = server::ToWire(FromSimulation(command.entity)),
  };
}

simulation::EntityId FromWire(protocol::EntityIdWire entity) {
  return static_cast<simulation::EntityId>(std::to_underlying(entity));
}

simulation::SessionId FromWire(protocol::SessionIdWire session) {
  return static_cast<simulation::SessionId>(std::to_underlying(session));
}

ballistics::BodyPart FromWire(protocol::BodyPartWire part) {
  switch (part) {
    case protocol::BodyPartWire::kHead:
      return ballistics::BodyPart::kHead;
    case protocol::BodyPartWire::kTorso:
      return ballistics::BodyPart::kTorso;
    case protocol::BodyPartWire::kLimb:
      return ballistics::BodyPart::kLimb;
  }
  std::unreachable();
}

protocol::RecordedBodyWire ToWire(const simulation::EntityState& body) {
  return protocol::RecordedBodyWire{
      .state = {.entity = server::ToWire(FromSimulation(body.entity)),
                .body = server::ToWire(body.body),
                .yaw = body.yaw},
      .rifle = {.cooldown = body.rifle.cooldown,
                .reload_remaining = body.rifle.reload_remaining,
                .recoil_pitch = body.rifle.recoil.pitch,
                .recoil_yaw = body.rifle.recoil.yaw,
                .rounds = body.rifle.rounds,
                .burst_index = body.rifle.burst_index},
      .health = body.health,
  };
}

simulation::EntityState FromWire(const protocol::RecordedBodyWire& body) {
  const protocol::BodyStateWire& state = body.state.body;
  return simulation::EntityState{
      .entity = FromWire(body.state.entity),
      .body = {.position = state.position,
               .velocity = state.velocity,
               .stance = FromWire(state.stance),
               .stamina = state.stamina,
               .exhausted = (state.flags & protocol::BodyStateWire::kExhausted) != 0},
      .yaw = body.state.yaw,
      .health = body.health,
      .rifle = {.cooldown = body.rifle.cooldown,
                .reload_remaining = body.rifle.reload_remaining,
                .recoil = {.pitch = body.rifle.recoil_pitch, .yaw = body.rifle.recoil_yaw},
                .rounds = body.rifle.rounds,
                .burst_index = body.rifle.burst_index},
  };
}

protocol::RecordedHitWire ToWire(const simulation::Hit& hit) {
  return protocol::RecordedHitWire{
      .damage = hit.damage,
      .health = hit.health,
      .shooter = server::ToWire(FromSimulation(hit.shooter)),
      .target = server::ToWire(FromSimulation(hit.target)),
      .part = ToWire(hit.part),
      .flags = hit.reached_zero ? protocol::RecordedHitWire::kReachedZero : std::uint8_t{0},
  };
}

simulation::Hit FromWire(const protocol::RecordedHitWire& hit) {
  return simulation::Hit{
      .shooter = FromWire(hit.shooter),
      .target = FromWire(hit.target),
      .damage = hit.damage,
      .health = hit.health,
      .part = FromWire(hit.part),
      .reached_zero = (hit.flags & protocol::RecordedHitWire::kReachedZero) != 0,
  };
}

protocol::DeathWire ToWire(const simulation::Death& death) {
  return protocol::DeathWire{.victim = server::ToWire(FromSimulation(death.victim)),
                             .killer = server::ToWire(FromSimulation(death.killer)),
                             .yaw = death.yaw,
                             .pitch = death.pitch,
                             .part = ToWire(death.part)};
}

simulation::Death FromWire(const protocol::DeathWire& death) {
  return simulation::Death{.victim = FromWire(death.victim),
                           .killer = FromWire(death.killer),
                           .yaw = death.yaw,
                           .pitch = death.pitch,
                           .part = FromWire(death.part)};
}

// Each of from, converted by convert.
template <typename To, typename From, typename Convert>
std::vector<To> Converted(const std::vector<From>& from, Convert convert) {
  std::vector<To> to;
  to.reserve(from.size());
  for (const From& element : from) {
    to.push_back(convert(element));
  }
  return to;
}

}  // namespace

protocol::RecordedTickWire ToWire(const TickRecord& record) {
  const TickInput& input = record.input;
  const TickOutcome& outcome = record.outcome;
  protocol::RecordedTickWire wire{
      .removed = Converted<protocol::EntityIdWire>(
          input.removed, [](simulation::EntityId entity) { return ToWire(FromSimulation(entity)); }),
      .match_start = {},
      .commands = Converted<protocol::RecordedCommandWire>(
          input.commands, [](const simulation::PlayerCommand& command) { return ToWire(command); }),
      .bodies = Converted<protocol::RecordedBodyWire>(outcome.bodies,
                                                      [](const simulation::EntityState& body) { return ToWire(body); }),
      .shots = Converted<protocol::ShotWire>(outcome.shots,
                                             [&](const simulation::Shot& shot) {
                                               return protocol::ShotWire{
                                                   .tick = outcome.tick,
                                                   .origin = shot.origin,
                                                   .shooter = ToWire(FromSimulation(shot.shooter)),
                                                   .yaw = shot.yaw,
                                                   .pitch = shot.pitch};
                                             }),
      .hits =
          Converted<protocol::RecordedHitWire>(outcome.hits, [](const simulation::Hit& hit) { return ToWire(hit); }),
      .deaths =
          Converted<protocol::DeathWire>(outcome.deaths, [](const simulation::Death& death) { return ToWire(death); }),
      .delta_time = input.delta_time,
      .winner = protocol::kDraw,
      .flags = input.match_ended ? protocol::RecordedTickWire::kMatchEnded : std::uint8_t{0},
  };
  wire.match_start.reserve(input.match_start.size());
  for (std::size_t i = 0; i < input.match_start.size(); ++i) {
    const RecordedEntrant& entrant = input.match_start[i];
    wire.match_start.push_back(protocol::MatchPlayerWire{.spawn = outcome.spawns.at(i),
                                                         .session = ToWire(FromSimulation(entrant.identity.session)),
                                                         .entity = ToWire(FromSimulation(entrant.entity)),
                                                         .character = entrant.identity.character});
  }
  if (outcome.match_end.has_value()) {
    wire.flags |= protocol::RecordedTickWire::kPolicyMatchEnd;
    if (outcome.match_end->winner.has_value()) {
      wire.winner = ToWire(FromSimulation(*outcome.match_end->winner));
    }
  }
  return wire;
}

TickRecord FromWire(const protocol::RecordedTickWire& record, tick::Tick tick) {
  TickRecord result{
      .input = {.removed = Converted<simulation::EntityId>(
                    record.removed, [](protocol::EntityIdWire entity) { return FromWire(entity); }),
                .match_start = {},
                .commands = Converted<simulation::PlayerCommand>(
                    record.commands,
                    [](const protocol::RecordedCommandWire& command) {
                      return simulation::PlayerCommand{.entity = FromWire(command.entity),
                                                       .command = FromWire(command.command, command.seen_tick)};
                    }),
                .delta_time = record.delta_time,
                .match_ended = (record.flags & protocol::RecordedTickWire::kMatchEnded) != 0},
      .outcome = {.tick = tick,
                  .spawns = {},
                  .bodies = Converted<simulation::EntityState>(
                      record.bodies, [](const protocol::RecordedBodyWire& body) { return FromWire(body); }),
                  .shots = Converted<simulation::Shot>(record.shots,
                                                       [](const protocol::ShotWire& shot) {
                                                         return simulation::Shot{.shooter = FromWire(shot.shooter),
                                                                                 .origin = shot.origin,
                                                                                 .yaw = shot.yaw,
                                                                                 .pitch = shot.pitch};
                                                       }),
                  .hits = Converted<simulation::Hit>(
                      record.hits, [](const protocol::RecordedHitWire& hit) { return FromWire(hit); }),
                  .deaths = Converted<simulation::Death>(
                      record.deaths, [](const protocol::DeathWire& death) { return FromWire(death); }),
                  .match_end = std::nullopt},
  };
  for (const protocol::MatchPlayerWire& player : record.match_start) {
    result.input.match_start.push_back(
        RecordedEntrant{.entity = FromWire(player.entity),
                        .identity = {.session = FromWire(player.session), .character = player.character}});
    result.outcome.spawns.push_back(player.spawn);
  }
  if ((record.flags & protocol::RecordedTickWire::kPolicyMatchEnd) != 0) {
    result.outcome.match_end = simulation::MatchEnd{
        .winner = record.winner == protocol::kDraw ? std::nullopt : std::optional(FromWire(record.winner))};
  }
  return result;
}

namespace {

protocol::PackHashWire ToWire(const assets::PackHash& hash) {
  protocol::PackHashWire wire{};
  std::ranges::copy(hash, wire.begin());
  return wire;
}

// One overload per event: its record, at offset.
struct CapturedEventToWire {
  std::uint32_t offset;

  protocol::CaptureRecordWire operator()(const CapturedJoin& join) const {
    return protocol::CapturedJoinWire{.spawn = join.spawn,
                                      .offset = offset,
                                      .session = server::ToWire(join.session),
                                      .character = join.character,
                                      .player = join.player};
  }
  protocol::CaptureRecordWire operator()(const CapturedCommand& command) const {
    return protocol::CapturedCommandWire{.command = ToWire(command.command),
                                         .offset = offset,
                                         .seen_offset = command.seen_offset,
                                         .player = command.player};
  }
  protocol::CaptureRecordWire operator()(const CapturedLeave& leave) const {
    return protocol::CapturedLeaveWire{.offset = offset, .player = leave.player};
  }
  protocol::CaptureRecordWire operator()(const CapturedDeath& death) const {
    return protocol::CapturedDeathWire{.offset = offset, .victim = death.victim, .killer = death.killer};
  }
  protocol::CaptureRecordWire operator()(const CapturedMatchEnd& end) const {
    return protocol::CapturedMatchEndWire{.offset = offset, .winner = end.winner.value_or(0)};
  }
};

// One overload per record: its event, nullopt for the header, which is none.
struct CaptureRecordFromWire {
  std::optional<CaptureRecord> operator()(const protocol::CaptureHeaderWire& /*header*/) const { return std::nullopt; }
  std::optional<CaptureRecord> operator()(const protocol::CapturedJoinWire& join) const {
    return CaptureRecord{.offset = join.offset,
                         .event = CapturedJoin{.player = join.player,
                                               .session = static_cast<SessionId>(std::to_underlying(join.session)),
                                               .character = join.character,
                                               .spawn = join.spawn}};
  }
  std::optional<CaptureRecord> operator()(const protocol::CapturedCommandWire& command) const {
    return CaptureRecord{.offset = command.offset,
                         .event = CapturedCommand{.player = command.player,
                                                  .seen_offset = command.seen_offset,
                                                  .command = server::FromWire(command.command, 0)}};
  }
  std::optional<CaptureRecord> operator()(const protocol::CapturedLeaveWire& leave) const {
    return CaptureRecord{.offset = leave.offset, .event = CapturedLeave{.player = leave.player}};
  }
  std::optional<CaptureRecord> operator()(const protocol::CapturedDeathWire& death) const {
    return CaptureRecord{.offset = death.offset,
                         .event = CapturedDeath{.victim = death.victim, .killer = death.killer}};
  }
  std::optional<CaptureRecord> operator()(const protocol::CapturedMatchEndWire& end) const {
    return CaptureRecord{.offset = end.offset,
                         .event = CapturedMatchEnd{
                             .winner = end.winner == 0 ? std::nullopt : std::optional<CapturedPlayer>(end.winner)}};
  }
};

}  // namespace

std::expected<protocol::BytesWire, failure::Failure> EncodeToCapture(const protocol::CaptureRecordWire& record) {
  return protocol::EncodeCaptureRecord(record).transform_error([&record](protocol::EncodeError error) {
    return BrokenInvariant(error, "capture_record_type", static_cast<std::uint8_t>(protocol::TypeOf(record)));
  });
}

protocol::CaptureHeaderWire ToWire(const CaptureHeader& header) {
  return protocol::CaptureHeaderWire{.server_pack = ToWire(header.server_pack),
                                     .client_pack = ToWire(header.client_pack),
                                     .engine_version = header.engine_version,
                                     .started_unix_ms = header.started.time_since_epoch().count(),
                                     .format_version = protocol::kCaptureFormatVersion,
                                     .tick_rate_hz = header.tick_rate_hz};
}

CaptureHeader FromWire(const protocol::CaptureHeaderWire& header) {
  return CaptureHeader{
      .engine_version = header.engine_version,
      .server_pack = FromWire(header.server_pack),
      .client_pack = FromWire(header.client_pack),
      .tick_rate_hz = header.tick_rate_hz,
      .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(header.started_unix_ms)),
  };
}

protocol::CaptureRecordWire ToWire(const CaptureRecord& record) {
  return std::visit(CapturedEventToWire{.offset = record.offset}, record.event);
}

std::optional<CaptureRecord> FromWire(const protocol::CaptureRecordWire& record) {
  return std::visit(CaptureRecordFromWire{}, record);
}

}  // namespace augusta::server
