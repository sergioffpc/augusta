#include "decoder.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <google/protobuf/repeated_ptr_field.h>

#include "augusta.pb.h"
#include "augusta/grid.h"
#include "augusta/limits.h"
#include "augusta/protocol.h"
#include "bitfields.h"

namespace augusta::protocol {

namespace {

// list, at most max long, each element as read maps it.
template <typename Element, typename ReadOne>
auto ReadList(const google::protobuf::RepeatedPtrField<Element>& list, std::size_t max, Inbound& in, ReadOne read) {
  std::vector<std::invoke_result_t<ReadOne, const Element&>> wires;
  if (!in.Fits(list.size(), max)) {
    return wires;
  }
  wires.reserve(static_cast<std::size_t>(list.size()));
  for (const Element& element : list) {
    wires.push_back(read(element));
  }
  return wires;
}

}  // namespace

CommandWire Read(const pb::Command& command, Inbound& in) {
  CommandWire wire;
  wire.direction =
      in.FromSteps(command.direction_x(), command.direction_y(), command.direction_z(), math::kDirectionGrid);
  wire.yaw = in.FromSteps(command.yaw(), math::kAngleGrid);
  wire.pitch = in.FromSteps(command.pitch(), math::kAngleGrid);
  const std::uint32_t bits = in.ToBits(command.bits(), kCommandBitsMask);
  wire.flags = static_cast<std::uint8_t>(bits & kCommandFlagsMask);
  wire.desired_stance = in.ToEnum(bits >> kCommandStanceShift, StanceWire::kStanding, StanceWire::kProne);
  wire.seen_age = in.Narrow(command.seen_age());
  wire.seen_fraction = in.FromSteps(command.seen_fraction(), math::kFractionGrid);
  return wire;
}

namespace {

EntityStateWire Read(const pb::Body& body, Inbound& in) {
  EntityStateWire wire{.entity = static_cast<EntityIdWire>(body.entity())};
  wire.body.position = in.FromSteps(body.position_x(), body.position_y(), body.position_z(), math::kPositionGrid);
  wire.body.velocity = in.FromSteps(body.velocity_x(), body.velocity_y(), body.velocity_z(), math::kVelocityGrid);
  const std::uint32_t bits = in.ToBits(body.bits(), kBodyBitsMask);
  wire.body.stance = in.ToEnum(bits & kStanceMask, StanceWire::kStanding, StanceWire::kProne);
  wire.body.flags = static_cast<std::uint8_t>((bits >> kBodyFlagsShift) & kBodyFlagsMask);
  wire.body.previous_stance =
      in.ToEnum((bits >> kPreviousStanceShift) & kStanceMask, StanceWire::kStanding, StanceWire::kProne);
  wire.body.stamina = in.FromSteps(body.stamina(), math::kStaminaGrid);
  wire.yaw = in.FromSteps(body.yaw(), math::kAngleGrid);
  wire.pitch = in.FromSteps(body.pitch(), math::kAngleGrid);
  wire.body.stride = in.Narrow(body.stride());
  const std::uint8_t to_go = in.Narrow(body.stance_to_go());
  wire.body.stance_progress = static_cast<std::uint8_t>(BodyStateWire::kStanceProgressOver - to_go);
  return wire;
}

JoinRequestWire Read(const pb::JoinRequest& request, Inbound& in) {
  return JoinRequestWire{.engine_version = in.Text(request.engine_version(), kMaxEngineVersionLength),
                         .client_pack = in.PackHash(request.client_pack()),
                         .character = in.Text(request.character(), kMaxCharacterNameLength),
                         .run = in.Uuid(request.run())};
}

ReenactRequestWire Read(const pb::ReenactRequest& request, Inbound& in) {
  return ReenactRequestWire{
      .engine_version = in.Text(request.engine_version(), kMaxEngineVersionLength),
      .client_pack = in.PackHash(request.client_pack()),
      .character = in.Text(request.character(), kMaxCharacterNameLength),
      .spawn = in.FromSteps(request.spawn_x(), request.spawn_y(), request.spawn_z(), math::kPositionGrid),
      .run = in.Uuid(request.run())};
}

RifleWire Read(const pb::Rifle& rifle, Inbound& in) {
  return RifleWire{.rounds_per_minute = rifle.rounds_per_minute(),
                   .muzzle_velocity = rifle.muzzle_velocity(),
                   .reload_seconds = rifle.reload_seconds(),
                   .recoil_recovery_per_second = rifle.recoil_recovery_per_second(),
                   .ads_recoil_scale = rifle.ads_recoil_scale(),
                   .ads_field_of_view = rifle.ads_field_of_view(),
                   .recoil_pattern = ReadList(rifle.recoil_pattern(), limits::kMaxRecoilKicks, in,
                                              [](const pb::RecoilKick& kick) {
                                                return RecoilKickWire{.pitch = kick.pitch(), .yaw = kick.yaw()};
                                              }),
                   .magazine_capacity = in.Narrow(rifle.magazine_capacity())};
}

ParametersWire Read(const pb::Parameters& parameters, Inbound& in) {
  const pb::Stamina& stamina = parameters.stamina();
  const pb::Ammo& ammo = parameters.ammo();
  return ParametersWire{.stamina = {.deplete_per_second = stamina.deplete_per_second(),
                                    .regen_per_second = stamina.regen_per_second(),
                                    .forced_walk_below = stamina.forced_walk_below()},
                        .rifle = Read(parameters.rifle(), in),
                        .ammo = {.gravity = ammo.gravity(),
                                 .max_range = ammo.max_range(),
                                 .head_damage = ammo.head_damage(),
                                 .torso_damage = ammo.torso_damage(),
                                 .limb_damage = ammo.limb_damage()},
                        .starting_health = parameters.starting_health(),
                        .player_count = in.Narrow(parameters.player_count())};
}

JoinAcceptedWire Read(const pb::JoinAccepted& accepted, Inbound& in) {
  return JoinAcceptedWire{.session = in.Uuid(accepted.session()),
                          .tick_rate_hz = in.Narrow(accepted.tick_rate_hz()),
                          .parameters = Read(accepted.parameters(), in),
                          .character = in.Text(accepted.character(), kMaxCharacterNameLength)};
}

JoinRefusedWire Read(const pb::JoinRefused& refused, Inbound& in) {
  return JoinRefusedWire{.reason = in.ToEnum(refused.reason(), JoinRefusalWire::kVersionMismatch,
                                             JoinRefusalWire::kReenactmentsNotAccepted)};
}

CommandsWire Read(const pb::Commands& commands, Inbound& in) {
  return CommandsWire{.commands = ReadList(commands.commands(), kMaxCommandsPerMessage, in,
                                           [&in](const pb::Command& command) {
                                             return SequencedCommandWire{.sequence = command.sequence(),
                                                                         .command = Read(command, in)};
                                           }),
                      .seen_tick = commands.seen_tick()};
}

AuthoritativeStateWire Read(const pb::AuthoritativeState& state, Inbound& in) {
  return AuthoritativeStateWire{.tick = state.tick(),
                                .bodies = ReadList(state.bodies(), limits::kMaxPlayers, in,
                                                   [&in](const pb::Body& body) { return Read(body, in); }),
                                .rifle = {.cooldown = state.rifle_cooldown(),
                                          .reload_remaining = state.rifle_reload_remaining(),
                                          .recoil_pitch = in.FromSteps(state.rifle_recoil_pitch(), math::kAngleGrid),
                                          .recoil_yaw = in.FromSteps(state.rifle_recoil_yaw(), math::kAngleGrid),
                                          .rounds = in.Narrow(state.rifle_rounds()),
                                          .burst_index = in.Narrow(state.rifle_burst_index())},
                                .health = state.health(),
                                .acknowledged_sequence = state.acknowledged_sequence(),
                                .queued_commands = in.Narrow(state.queued_commands())};
}

LobbyWire Read(const pb::Lobby& lobby, Inbound& in) {
  return LobbyWire{.version = lobby.version(),
                   .roster = ReadList(lobby.roster(), limits::kMaxPlayers, in, [&in](const pb::RosterEntry& entry) {
                     return RosterEntryWire{.session = in.Uuid(entry.session()),
                                            .character = in.Text(entry.character(), kMaxCharacterNameLength)};
                   })};
}

MatchStartWire Read(const pb::MatchStart& start, Inbound& in) {
  return MatchStartWire{.players = ReadList(start.players(), limits::kMaxPlayers, in,
                                            [&in](const pb::MatchPlayer& player) {
                                              return MatchPlayerWire{
                                                  .spawn = in.FromSteps(player.spawn_x(), player.spawn_y(),
                                                                        player.spawn_z(), math::kPositionGrid),
                                                  .session = in.Uuid(player.session()),
                                                  .entity = static_cast<EntityIdWire>(player.entity()),
                                                  .character = in.Text(player.character(), kMaxCharacterNameLength)};
                                            }),
                        .first_tick = start.first_tick(),
                        .match = in.Uuid(start.match())};
}

ShotWire Read(const pb::Shot& shot, Inbound& in) {
  return ShotWire{.tick = shot.tick(),
                  .origin = in.FromSteps(shot.origin_x(), shot.origin_y(), shot.origin_z(), math::kPositionGrid),
                  .shooter = static_cast<EntityIdWire>(shot.shooter()),
                  .yaw = in.FromSteps(shot.yaw(), math::kAngleGrid),
                  .pitch = in.FromSteps(shot.pitch(), math::kAngleGrid)};
}

HitConfirmationWire Read(const pb::HitConfirmation& hit, Inbound& in) {
  return HitConfirmationWire{.target = static_cast<EntityIdWire>(hit.target()),
                             .damage = hit.damage(),
                             .part = in.ToEnum(hit.part(), BodyPartWire::kHead, BodyPartWire::kLimb)};
}

DeathWire Read(const pb::Death& death, Inbound& in) {
  return DeathWire{.victim = static_cast<EntityIdWire>(death.victim()),
                   .killer = static_cast<EntityIdWire>(death.killer()),
                   .yaw = in.FromSteps(death.yaw(), math::kAngleGrid),
                   .pitch = in.FromSteps(death.pitch(), math::kAngleGrid),
                   .part = in.ToEnum(death.part(), BodyPartWire::kHead, BodyPartWire::kLimb)};
}

ReplayListingWire Read(const pb::ReplayListing& listing, Inbound& in) {
  return ReplayListingWire{.started_unix_ms = listing.started_unix_ms(),
                           .characters = ReadList(listing.characters(), limits::kMaxPlayers, in,
                                                  [&in](const std::string& character) {
                                                    return in.Text(character, kMaxCharacterNameLength);
                                                  }),
                           .name = in.Text(listing.name(), kMaxCaptureNameLength),
                           .ticks = listing.ticks(),
                           .tick_rate_hz = in.Narrow(listing.tick_rate_hz())};
}

ReplayRequestWire Read(const pb::ReplayRequest& request, Inbound& in) {
  return ReplayRequestWire{.engine_version = in.Text(request.engine_version(), kMaxEngineVersionLength),
                           .client_pack = in.PackHash(request.client_pack()),
                           .capture = in.Text(request.capture(), kMaxCaptureNameLength),
                           .run = in.Uuid(request.run())};
}

#if AUGUSTA_DEBUG_TOOLS
JudgedPlayerWire Read(const pb::JudgedPlayer& player, Inbound& in) {
  return JudgedPlayerWire{
      .position = in.FromSteps(player.position_x(), player.position_y(), player.position_z(), math::kPositionGrid),
      .entity = static_cast<EntityIdWire>(player.entity()),
      .yaw = in.FromSteps(player.yaw(), math::kAngleGrid),
      .stance = in.ToEnum(in.ToBits(player.bits(), kStanceMask), StanceWire::kStanding, StanceWire::kProne)};
}

ShotJudgementWire Read(const pb::ShotJudgement& judgement, Inbound& in) {
  const std::uint32_t bits = in.ToBits(judgement.bits(), kJudgementBitsMask);
  return ShotJudgementWire{
      .shot_tick = judgement.shot_tick(),
      .entries = ReadList(judgement.entries(), limits::kMaxShotJudgementEntries, in,
                          [&in](const pb::ShotJudgementEntry& entry) {
                            return ShotJudgementEntryWire{
                                .players = ReadList(entry.players(), limits::kMaxPlayers, in,
                                                    [&in](const pb::JudgedPlayer& player) { return Read(player, in); }),
                                .ticks_after_shot = in.Narrow<std::uint16_t>(entry.ticks_after_shot())};
                          }),
      .point = in.FromSteps(judgement.point_x(), judgement.point_y(), judgement.point_z(), math::kPositionGrid),
      .target = static_cast<EntityIdWire>(judgement.target()),
      .delay_fraction = in.FromSteps(judgement.delay_fraction(), math::kFractionGrid),
      .delay_ticks = in.Narrow(judgement.delay_ticks()),
      .struck = static_cast<StruckWire>(bits & kStruckMask),
      .part = in.ToEnum((bits >> kJudgementPartShift) & kPartMask, BodyPartWire::kHead, BodyPartWire::kLimb),
      .flags = static_cast<std::uint8_t>(bits >> kJudgementFlagsShift)};
}
#endif

}  // namespace

std::optional<MessageWire> ReadBody(const pb::Message& message, Inbound& in) {
  switch (message.body_case()) {
    case pb::Message::kJoinRequest:
      return Read(message.join_request(), in);
    case pb::Message::kJoinAccepted:
      return Read(message.join_accepted(), in);
    case pb::Message::kJoinRefused:
      return Read(message.join_refused(), in);
    case pb::Message::kCommands:
      return Read(message.commands(), in);
    case pb::Message::kAuthoritativeState:
      return Read(message.authoritative_state(), in);
    case pb::Message::kLobby:
      return Read(message.lobby(), in);
    case pb::Message::kReady:
      return ReadyWire{.version = message.ready().version()};
    case pb::Message::kMatchStart:
      return Read(message.match_start(), in);
    case pb::Message::kMatchEnd:
      return MatchEndWire{.winner = in.Uuid(message.match_end().winner())};
    case pb::Message::kShot:
      return Read(message.shot(), in);
    case pb::Message::kHitConfirmation:
      return Read(message.hit_confirmation(), in);
    case pb::Message::kDeath:
      return Read(message.death(), in);
    case pb::Message::kReplayListRequest:
      return ReplayListRequestWire{};
    case pb::Message::kReplayList:
      return ReplayListWire{.replays = ReadList(message.replay_list().replays(), kMaxReplayListings, in,
                                                [&in](const pb::ReplayListing& listing) { return Read(listing, in); })};
    case pb::Message::kReplayRequest:
      return Read(message.replay_request(), in);
    case pb::Message::kReenactRequest:
      return Read(message.reenact_request(), in);
    case pb::Message::kDebugSubscribe:
#if AUGUSTA_DEBUG_TOOLS
      return DebugSubscribeWire{.flags = in.Narrow(message.debug_subscribe().flags())};
#else
      // A Development build's message, which a Shipping one lacks (ADR-0074).
      return std::nullopt;
#endif
    case pb::Message::kShotJudgement:
#if AUGUSTA_DEBUG_TOOLS
      return Read(message.shot_judgement(), in);
#else
      return std::nullopt;
#endif
    case pb::Message::BODY_NOT_SET:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace augusta::protocol
