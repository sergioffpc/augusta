#include "encoder.h"

#include <cstdint>
#include <string>
#include <string_view>

#include <boost/uuid/uuid.hpp>

#include "augusta.pb.h"
#include "augusta/grid.h"
#include "augusta/limits.h"
#include "augusta/protocol.h"
#include "bitfields.h"

namespace augusta::protocol {

namespace {

// The same as Steps, on a grid with no negative count, which travels as a uint32.
std::uint32_t UnsignedSteps(float value, const math::Grid& grid) {
  return static_cast<std::uint32_t>(math::ToSteps(value, grid));
}

void Write(const EntityStateWire& wire, pb::Body& body, Outbound& out) {
  body.set_entity(static_cast<std::uint32_t>(wire.entity));
  body.set_position_x(Steps(wire.body.position.x, math::kPositionGrid));
  body.set_position_y(Steps(wire.body.position.y, math::kPositionGrid));
  body.set_position_z(Steps(wire.body.position.z, math::kPositionGrid));
  body.set_velocity_x(Steps(wire.body.velocity.x, math::kVelocityGrid));
  body.set_velocity_y(Steps(wire.body.velocity.y, math::kVelocityGrid));
  body.set_velocity_z(Steps(wire.body.velocity.z, math::kVelocityGrid));
  const std::uint32_t stance = out.FromEnum(wire.body.stance, StanceWire::kStanding, StanceWire::kProne);
  const std::uint32_t flags = out.FromFlags(wire.body.flags, kBodyFlagsMask);
  const std::uint32_t previous_stance =
      out.FromEnum(wire.body.previous_stance, StanceWire::kStanding, StanceWire::kProne);
  body.set_bits(stance | (flags << kBodyFlagsShift) | (previous_stance << kPreviousStanceShift));
  body.set_stamina(UnsignedSteps(wire.body.stamina, math::kStaminaGrid));
  body.set_yaw(Steps(wire.yaw, math::kAngleGrid));
  body.set_pitch(Steps(wire.pitch, math::kAngleGrid));
  body.set_stride(wire.body.stride);
  body.set_stance_to_go(BodyStateWire::kStanceProgressOver - wire.body.stance_progress);
}

// An Authoritative State's shared fields: what every recipient of a tick is told alike.
void WriteShared(const AuthoritativeStateWire& wire, pb::AuthoritativeState& state, Outbound& out) {
  state.set_tick(wire.tick);
  if (!out.Fits(wire.bodies.size(), limits::kMaxPlayers)) {
    return;
  }
  for (const EntityStateWire& body : wire.bodies) {
    Write(body, *state.add_bodies(), out);
  }
}

void Write(const ParametersWire& wire, pb::Parameters& parameters, Outbound& out) {
  pb::Stamina& stamina = *parameters.mutable_stamina();
  stamina.set_deplete_per_second(wire.stamina.deplete_per_second);
  stamina.set_regen_per_second(wire.stamina.regen_per_second);
  stamina.set_forced_walk_below(wire.stamina.forced_walk_below);
  pb::Rifle& rifle = *parameters.mutable_rifle();
  rifle.set_rounds_per_minute(wire.rifle.rounds_per_minute);
  rifle.set_muzzle_velocity(wire.rifle.muzzle_velocity);
  rifle.set_reload_seconds(wire.rifle.reload_seconds);
  rifle.set_recoil_recovery_per_second(wire.rifle.recoil_recovery_per_second);
  rifle.set_ads_recoil_scale(wire.rifle.ads_recoil_scale);
  rifle.set_ads_field_of_view(wire.rifle.ads_field_of_view);
  if (out.Fits(wire.rifle.recoil_pattern.size(), limits::kMaxRecoilKicks)) {
    for (const RecoilKickWire& kick : wire.rifle.recoil_pattern) {
      pb::RecoilKick& written = *rifle.add_recoil_pattern();
      written.set_pitch(kick.pitch);
      written.set_yaw(kick.yaw);
    }
  }
  rifle.set_magazine_capacity(wire.rifle.magazine_capacity);
  pb::Ammo& ammo = *parameters.mutable_ammo();
  ammo.set_gravity(wire.ammo.gravity);
  ammo.set_max_range(wire.ammo.max_range);
  ammo.set_head_damage(wire.ammo.head_damage);
  ammo.set_torso_damage(wire.ammo.torso_damage);
  ammo.set_limb_damage(wire.ammo.limb_damage);
  parameters.set_starting_health(wire.starting_health);
  parameters.set_player_count(wire.player_count);
}

void Write(const MatchPlayerWire& wire, pb::MatchPlayer& player, Outbound& out) {
  player.set_session(BytesOf(wire.session));
  player.set_entity(static_cast<std::uint32_t>(wire.entity));
  player.set_character(out.Text(wire.character, kMaxCharacterNameLength));
  player.set_spawn_x(Steps(wire.spawn.x, math::kPositionGrid));
  player.set_spawn_y(Steps(wire.spawn.y, math::kPositionGrid));
  player.set_spawn_z(Steps(wire.spawn.z, math::kPositionGrid));
}

void Write(const ReplayListingWire& wire, pb::ReplayListing& listing, Outbound& out) {
  listing.set_name(out.Text(wire.name, kMaxCaptureNameLength));
  listing.set_started_unix_ms(wire.started_unix_ms);
  listing.set_ticks(wire.ticks);
  listing.set_tick_rate_hz(wire.tick_rate_hz);
  if (out.Fits(wire.characters.size(), limits::kMaxPlayers)) {
    for (const std::string& character : wire.characters) {
      listing.add_characters(out.Text(character, kMaxCharacterNameLength));
    }
  }
}

}  // namespace

std::int32_t Steps(float value, const math::Grid& grid) { return math::ToSteps(value, grid); }

std::string_view BytesOf(const PackHashWire& hash) { return {reinterpret_cast<const char*>(hash.data()), hash.size()}; }

std::string_view BytesOf(const boost::uuids::uuid& id) {
  return {reinterpret_cast<const char*>(id.begin()), id.size()};
}

void Write(const CommandWire& wire, pb::Command& command, Outbound& out) {
  command.set_direction_x(Steps(wire.direction.x, math::kDirectionGrid));
  command.set_direction_y(Steps(wire.direction.y, math::kDirectionGrid));
  command.set_direction_z(Steps(wire.direction.z, math::kDirectionGrid));
  command.set_yaw(Steps(wire.yaw, math::kAngleGrid));
  command.set_pitch(Steps(wire.pitch, math::kAngleGrid));
  const std::uint32_t flags = out.FromFlags(wire.flags, kCommandFlagsMask);
  const std::uint32_t stance = out.FromEnum(wire.desired_stance, StanceWire::kStanding, StanceWire::kProne);
  command.set_bits(flags | (stance << kCommandStanceShift));
  command.set_seen_age(wire.seen_age);
  command.set_seen_fraction(UnsignedSteps(wire.seen_fraction, math::kFractionGrid));
}

void WriteOwn(const AuthoritativeStateWire& wire, pb::AuthoritativeState& state) {
  state.set_acknowledged_sequence(wire.acknowledged_sequence);
  state.set_queued_commands(wire.queued_commands);
  state.set_rifle_rounds(wire.rifle.rounds);
  state.set_rifle_cooldown(wire.rifle.cooldown);
  state.set_rifle_reload_remaining(wire.rifle.reload_remaining);
  state.set_rifle_burst_index(wire.rifle.burst_index);
  state.set_rifle_recoil_pitch(Steps(wire.rifle.recoil_pitch, math::kAngleGrid));
  state.set_rifle_recoil_yaw(Steps(wire.rifle.recoil_yaw, math::kAngleGrid));
  // As a float, as the Parameters' starting health it counts down from.
  state.set_health(wire.health);
}

void Encoder::operator()(const JoinRequestWire& wire) const {
  pb::JoinRequest& request = *message.mutable_join_request();
  request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
  request.set_client_pack(BytesOf(wire.client_pack));
  request.set_character(out.Text(wire.character, kMaxCharacterNameLength));
  request.set_run(BytesOf(wire.run));
}

void Encoder::operator()(const JoinAcceptedWire& wire) const {
  pb::JoinAccepted& accepted = *message.mutable_join_accepted();
  accepted.set_session(BytesOf(wire.session));
  accepted.set_tick_rate_hz(wire.tick_rate_hz);
  Write(wire.parameters, *accepted.mutable_parameters(), out);
  accepted.set_character(out.Text(wire.character, kMaxCharacterNameLength));
}

void Encoder::operator()(const JoinRefusedWire& wire) const {
  message.mutable_join_refused()->set_reason(static_cast<pb::JoinRefusal>(
      out.FromEnum(wire.reason, JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kReenactmentsNotAccepted)));
}

void Encoder::operator()(const CommandsWire& wire) const {
  pb::Commands& commands = *message.mutable_commands();
  if (out.Fits(wire.commands.size(), kMaxCommandsPerMessage)) {
    for (const SequencedCommandWire& sequenced : wire.commands) {
      pb::Command& command = *commands.add_commands();
      command.set_sequence(sequenced.sequence);
      Write(sequenced.command, command, out);
    }
  }
  commands.set_seen_tick(wire.seen_tick);
}

void Encoder::operator()(const AuthoritativeStateWire& wire) const {
  pb::AuthoritativeState& state = *message.mutable_authoritative_state();
  WriteShared(wire, state, out);
  WriteOwn(wire, state);
}

void Encoder::operator()(const LobbyWire& wire) const {
  pb::Lobby& lobby = *message.mutable_lobby();
  lobby.set_version(wire.version);
  if (out.Fits(wire.roster.size(), limits::kMaxPlayers)) {
    for (const RosterEntryWire& entry : wire.roster) {
      pb::RosterEntry& written = *lobby.add_roster();
      written.set_session(BytesOf(entry.session));
      written.set_character(out.Text(entry.character, kMaxCharacterNameLength));
    }
  }
}

void Encoder::operator()(const ReadyWire& wire) const { message.mutable_ready()->set_version(wire.version); }

void Encoder::operator()(const MatchStartWire& wire) const {
  pb::MatchStart& start = *message.mutable_match_start();
  if (out.Fits(wire.players.size(), limits::kMaxPlayers)) {
    for (const MatchPlayerWire& player : wire.players) {
      Write(player, *start.add_players(), out);
    }
  }
  start.set_first_tick(wire.first_tick);
  start.set_match(BytesOf(wire.match));
}

void Encoder::operator()(const MatchEndWire& wire) const {
  message.mutable_match_end()->set_winner(BytesOf(wire.winner));
}

void Encoder::operator()(const ShotWire& wire) const {
  pb::Shot& shot = *message.mutable_shot();
  shot.set_tick(wire.tick);
  shot.set_shooter(static_cast<std::uint32_t>(wire.shooter));
  shot.set_origin_x(Steps(wire.origin.x, math::kPositionGrid));
  shot.set_origin_y(Steps(wire.origin.y, math::kPositionGrid));
  shot.set_origin_z(Steps(wire.origin.z, math::kPositionGrid));
  shot.set_yaw(Steps(wire.yaw, math::kAngleGrid));
  shot.set_pitch(Steps(wire.pitch, math::kAngleGrid));
}

void Encoder::operator()(const HitConfirmationWire& wire) const {
  pb::HitConfirmation& hit = *message.mutable_hit_confirmation();
  hit.set_target(static_cast<std::uint32_t>(wire.target));
  hit.set_part(static_cast<pb::BodyPart>(out.FromEnum(wire.part, BodyPartWire::kHead, BodyPartWire::kLimb)));
  hit.set_damage(wire.damage);
}

void Encoder::operator()(const DeathWire& wire) const {
  pb::Death& death = *message.mutable_death();
  death.set_victim(static_cast<std::uint32_t>(wire.victim));
  death.set_killer(static_cast<std::uint32_t>(wire.killer));
  death.set_part(static_cast<pb::BodyPart>(out.FromEnum(wire.part, BodyPartWire::kHead, BodyPartWire::kLimb)));
  death.set_yaw(Steps(wire.yaw, math::kAngleGrid));
  death.set_pitch(Steps(wire.pitch, math::kAngleGrid));
}

void Encoder::operator()(const ReplayListRequestWire& /*wire*/) const {
  static_cast<void>(message.mutable_replay_list_request());
}

void Encoder::operator()(const ReplayListWire& wire) const {
  pb::ReplayList& list = *message.mutable_replay_list();
  if (out.Fits(wire.replays.size(), kMaxReplayListings)) {
    for (const ReplayListingWire& listing : wire.replays) {
      Write(listing, *list.add_replays(), out);
    }
  }
}

void Encoder::operator()(const ReplayRequestWire& wire) const {
  pb::ReplayRequest& request = *message.mutable_replay_request();
  request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
  request.set_client_pack(BytesOf(wire.client_pack));
  request.set_capture(out.Text(wire.capture, kMaxCaptureNameLength));
  request.set_run(BytesOf(wire.run));
}

void Encoder::operator()(const ReenactRequestWire& wire) const {
  pb::ReenactRequest& request = *message.mutable_reenact_request();
  request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
  request.set_client_pack(BytesOf(wire.client_pack));
  request.set_character(out.Text(wire.character, kMaxCharacterNameLength));
  request.set_spawn_x(Steps(wire.spawn.x, math::kPositionGrid));
  request.set_spawn_y(Steps(wire.spawn.y, math::kPositionGrid));
  request.set_spawn_z(Steps(wire.spawn.z, math::kPositionGrid));
  request.set_run(BytesOf(wire.run));
}

#if AUGUSTA_DEBUG_TOOLS
void Encoder::operator()(const DebugSubscribeWire& wire) const {
  message.mutable_debug_subscribe()->set_flags(wire.flags);
}

void Encoder::operator()(const ShotJudgementWire& wire) const {
  pb::ShotJudgement& judgement = *message.mutable_shot_judgement();
  judgement.set_shot_tick(wire.shot_tick);
  judgement.set_delay_ticks(wire.delay_ticks);
  judgement.set_delay_fraction(UnsignedSteps(wire.delay_fraction, math::kFractionGrid));
  const std::uint32_t struck = out.FromEnum(wire.struck, StruckWire::kNothing, StruckWire::kHitbox);
  const std::uint32_t part = out.FromEnum(wire.part, BodyPartWire::kHead, BodyPartWire::kLimb);
  const std::uint32_t flags = out.FromFlags(wire.flags, kJudgementFlagsMask);
  judgement.set_bits(struck | (part << kJudgementPartShift) | (flags << kJudgementFlagsShift));
  judgement.set_point_x(Steps(wire.point.x, math::kPositionGrid));
  judgement.set_point_y(Steps(wire.point.y, math::kPositionGrid));
  judgement.set_point_z(Steps(wire.point.z, math::kPositionGrid));
  judgement.set_target(static_cast<std::uint32_t>(wire.target));
  if (!out.Fits(wire.entries.size(), limits::kMaxShotJudgementEntries)) {
    return;
  }
  for (const ShotJudgementEntryWire& entry : wire.entries) {
    pb::ShotJudgementEntry& written = *judgement.add_entries();
    written.set_ticks_after_shot(entry.ticks_after_shot);
    if (!out.Fits(entry.players.size(), limits::kMaxPlayers)) {
      return;
    }
    for (const JudgedPlayerWire& player : entry.players) {
      pb::JudgedPlayer& judged = *written.add_players();
      judged.set_entity(static_cast<std::uint32_t>(player.entity));
      judged.set_position_x(Steps(player.position.x, math::kPositionGrid));
      judged.set_position_y(Steps(player.position.y, math::kPositionGrid));
      judged.set_position_z(Steps(player.position.z, math::kPositionGrid));
      judged.set_bits(out.FromEnum(player.stance, StanceWire::kStanding, StanceWire::kProne));
      judged.set_yaw(Steps(player.yaw, math::kAngleGrid));
    }
  }
}
#endif

}  // namespace augusta::protocol
