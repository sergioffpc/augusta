#include "augusta/protocol.h"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <google/protobuf/arena.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message_lite.h>
#include <google/protobuf/repeated_ptr_field.h>

#include "augusta.pb.h"
#include "augusta/grid.h"
#include "augusta/limits.h"
#include "augusta/math.h"

namespace augusta::protocol {

namespace {

// The classes protobuf generates from proto/augusta.proto, which its package
// puts in ::augusta.
namespace pb = ::augusta;

// A command's flags take bits 0-3 of its bitfield and its stance bits 4-5
// (ADR-0038).
constexpr std::uint8_t kCommandFlagsMask = 0x0FU;
constexpr unsigned kCommandStanceShift = 4U;
constexpr std::uint32_t kStanceMask = 0x03U;
constexpr std::uint32_t kCommandBitsMask = kCommandFlagsMask | (kStanceMask << kCommandStanceShift);

// A body's stance takes bits 0-1 of its bitfield and its exhausted flag bit 2.
constexpr std::uint8_t kBodyFlagsMask = BodyStateWire::kExhausted;
constexpr unsigned kBodyFlagsShift = 2U;
constexpr std::uint32_t kBodyBitsMask = kStanceMask | (std::uint32_t{kBodyFlagsMask} << kBodyFlagsShift);

// The first block of each call's arena, on the stack: more than an
// Authoritative State or a Commands message as large as the protocol allows
// builds or parses into, so neither touches the heap but for an encode's
// payload. A larger message (a Replay list) grows the arena there.
constexpr std::size_t kArenaBlockSize = 4096;

// An arena local to one Encode, Decode or Readdress (ADR-0007): the messages the
// call builds or parses live on it and go with it, and none reaches the API.
class CallArena {
 public:
  template <typename Message>
  Message& Create() {
    return *google::protobuf::Arena::Create<Message>(&arena_);
  }

 private:
  alignas(std::max_align_t) std::array<char, kArenaBlockSize> block_;
  google::protobuf::Arena arena_{block_.data(), block_.size()};
};

// message's bytes: the one heap allocation of an encode.
BytesWire Serialize(const google::protobuf::MessageLite& message) {
  BytesWire bytes(message.ByteSizeLong());
  message.SerializeWithCachedSizesToArray(reinterpret_cast<std::uint8_t*>(bytes.data()));
  return bytes;
}

// What Encode maps a *Wire to its generated message with. The first field it is
// handed that the protocol cannot carry is remembered, so a mapping sets all of
// a message's fields and the caller asks once at the end, and nothing of a
// message with such a field is ever serialized.
class Outbound {
 public:
  // Whether holds, remembering error if it does not.
  bool Check(bool holds, EncodeError error) {
    if (!holds && !error_.has_value()) {
      error_ = error;
    }
    return holds;
  }

  // value, an enumerator that must lie between first and last, as its number.
  template <typename Enum>
  std::uint32_t FromEnum(Enum value, Enum first, Enum last) {
    const auto number = static_cast<std::uint32_t>(value);
    Check(number >= static_cast<std::uint32_t>(first) && number <= static_cast<std::uint32_t>(last),
          EncodeError::kInvalidEnum);
    return number;
  }

  // flags, of which only the bits of mask may be set.
  std::uint32_t FromFlags(std::uint8_t flags, std::uint8_t mask) {
    Check((flags & ~mask) == 0, EncodeError::kReservedBits);
    return flags;
  }

  // text, which may be at most max_length bytes long.
  std::string_view Text(std::string_view text, std::size_t max_length) {
    Check(text.size() <= max_length, EncodeError::kFieldTooLong);
    return text;
  }

  // Whether a list of size elements is at most max long.
  bool Fits(std::size_t size, std::size_t max) { return Check(size <= max, EncodeError::kFieldTooLong); }

  [[nodiscard]] std::optional<EncodeError> Error() const { return error_; }

 private:
  std::optional<EncodeError> error_;
};

// value as a whole count of grid's step (ADR-0038), held within its range.
std::int32_t Steps(float value, const math::Grid& grid) { return math::ToSteps(value, grid); }

// The same, on a grid with no negative count, which travels as a uint32.
std::uint32_t UnsignedSteps(float value, const math::Grid& grid) {
  return static_cast<std::uint32_t>(math::ToSteps(value, grid));
}

std::string_view BytesOf(const PackHashWire& hash) { return {reinterpret_cast<const char*>(hash.data()), hash.size()}; }

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
  body.set_bits(stance | (flags << kBodyFlagsShift));
  body.set_stamina(UnsignedSteps(wire.body.stamina, math::kStaminaGrid));
  body.set_yaw(Steps(wire.yaw, math::kAngleGrid));
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

// Its recipient's own fields, none of which has a limit, so none fails. A
// rifle's two times travel as floats, not on a grid: its owner replays its
// commands from them, with the function the server stepped them with, and must
// start from exactly what the server had. Its Recoil offset is kept on the
// angle grid by that function, so its counts are exactly what the server had
// too.
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
  player.set_session(static_cast<std::uint32_t>(wire.session));
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

// One overload per message: the envelope's member, then its fields.
struct Encoder {
  pb::Message& message;
  Outbound& out;

  void operator()(const JoinRequestWire& wire) const {
    pb::JoinRequest& request = *message.mutable_join_request();
    request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
    request.set_client_pack(BytesOf(wire.client_pack));
    request.set_character(out.Text(wire.character, kMaxCharacterNameLength));
  }

  void operator()(const JoinAcceptedWire& wire) const {
    pb::JoinAccepted& accepted = *message.mutable_join_accepted();
    accepted.set_session(static_cast<std::uint32_t>(wire.session));
    accepted.set_tick_rate_hz(wire.tick_rate_hz);
    Write(wire.parameters, *accepted.mutable_parameters(), out);
    accepted.set_character(out.Text(wire.character, kMaxCharacterNameLength));
  }

  void operator()(const JoinRefusedWire& wire) const {
    message.mutable_join_refused()->set_reason(static_cast<pb::JoinRefusal>(
        out.FromEnum(wire.reason, JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kReenactmentsNotAccepted)));
  }

  void operator()(const CommandsWire& wire) const {
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

  void operator()(const AuthoritativeStateWire& wire) const {
    pb::AuthoritativeState& state = *message.mutable_authoritative_state();
    WriteShared(wire, state, out);
    WriteOwn(wire, state);
  }

  void operator()(const LobbyWire& wire) const {
    pb::Lobby& lobby = *message.mutable_lobby();
    lobby.set_version(wire.version);
    if (out.Fits(wire.roster.size(), limits::kMaxPlayers)) {
      for (const RosterEntryWire& entry : wire.roster) {
        pb::RosterEntry& written = *lobby.add_roster();
        written.set_session(static_cast<std::uint32_t>(entry.session));
        written.set_character(out.Text(entry.character, kMaxCharacterNameLength));
      }
    }
  }

  void operator()(const ReadyWire& wire) const { message.mutable_ready()->set_version(wire.version); }

  void operator()(const MatchStartWire& wire) const {
    pb::MatchStart& start = *message.mutable_match_start();
    if (out.Fits(wire.players.size(), limits::kMaxPlayers)) {
      for (const MatchPlayerWire& player : wire.players) {
        Write(player, *start.add_players(), out);
      }
    }
    start.set_first_tick(wire.first_tick);
  }

  void operator()(const MatchEndWire& wire) const {
    message.mutable_match_end()->set_winner(static_cast<std::uint32_t>(wire.winner));
  }

  void operator()(const ShotWire& wire) const {
    pb::Shot& shot = *message.mutable_shot();
    shot.set_tick(wire.tick);
    shot.set_shooter(static_cast<std::uint32_t>(wire.shooter));
    shot.set_origin_x(Steps(wire.origin.x, math::kPositionGrid));
    shot.set_origin_y(Steps(wire.origin.y, math::kPositionGrid));
    shot.set_origin_z(Steps(wire.origin.z, math::kPositionGrid));
    shot.set_yaw(Steps(wire.yaw, math::kAngleGrid));
    shot.set_pitch(Steps(wire.pitch, math::kAngleGrid));
  }

  void operator()(const HitConfirmationWire& wire) const {
    pb::HitConfirmation& hit = *message.mutable_hit_confirmation();
    hit.set_target(static_cast<std::uint32_t>(wire.target));
    hit.set_part(static_cast<pb::BodyPart>(out.FromEnum(wire.part, BodyPartWire::kHead, BodyPartWire::kLimb)));
    hit.set_damage(wire.damage);
  }

  void operator()(const DeathWire& wire) const {
    pb::Death& death = *message.mutable_death();
    death.set_victim(static_cast<std::uint32_t>(wire.victim));
    death.set_killer(static_cast<std::uint32_t>(wire.killer));
    death.set_part(static_cast<pb::BodyPart>(out.FromEnum(wire.part, BodyPartWire::kHead, BodyPartWire::kLimb)));
    death.set_yaw(Steps(wire.yaw, math::kAngleGrid));
    death.set_pitch(Steps(wire.pitch, math::kAngleGrid));
  }

  void operator()(const ReplayListRequestWire& /*wire*/) const {
    static_cast<void>(message.mutable_replay_list_request());
  }

  void operator()(const ReplayListWire& wire) const {
    pb::ReplayList& list = *message.mutable_replay_list();
    if (out.Fits(wire.replays.size(), kMaxReplayListings)) {
      for (const ReplayListingWire& listing : wire.replays) {
        Write(listing, *list.add_replays(), out);
      }
    }
  }

  void operator()(const ReplayRequestWire& wire) const {
    pb::ReplayRequest& request = *message.mutable_replay_request();
    request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
    request.set_client_pack(BytesOf(wire.client_pack));
    request.set_capture(out.Text(wire.capture, kMaxCaptureNameLength));
  }

  void operator()(const ReplayViewWire& wire) const {
    pb::ReplayView& view = *message.mutable_replay_view();
    view.set_tick(wire.tick);
    if (out.Fits(wire.players.size(), limits::kMaxPlayers)) {
      for (const PlayerViewWire& player : wire.players) {
        pb::PlayerView& written = *view.add_players();
        written.set_entity(static_cast<std::uint32_t>(player.entity));
        written.set_pitch(Steps(player.pitch, math::kAngleGrid));
        written.set_bits(out.FromFlags(player.flags, PlayerViewWire::kAds));
      }
    }
  }

  void operator()(const ReenactRequestWire& wire) const {
    pb::ReenactRequest& request = *message.mutable_reenact_request();
    request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
    request.set_client_pack(BytesOf(wire.client_pack));
    request.set_character(out.Text(wire.character, kMaxCharacterNameLength));
    request.set_spawn_x(Steps(wire.spawn.x, math::kPositionGrid));
    request.set_spawn_y(Steps(wire.spawn.y, math::kPositionGrid));
    request.set_spawn_z(Steps(wire.spawn.z, math::kPositionGrid));
  }
};

// What Decode maps a parsed message to its *Wire with. The first value it meets
// that no valid message of this build holds is remembered, and every check
// after gives a zero value, so a mapping reads all of a message's fields and
// the caller asks once at the end (ADR-0038, Untrusted input).
class Inbound {
 public:
  // count of grid's step as the value it stands for. A count outside the
  // grid's range is none a sender, clamping, ever writes.
  float FromSteps(std::int64_t count, const math::Grid& grid) {
    if (count < grid.min || count > grid.max) {
      Fail(DecodeError::kOutOfRange);
      return 0.0F;
    }
    return math::FromSteps(static_cast<std::int32_t>(count), grid);
  }

  math::Vec3 FromSteps(std::int64_t x, std::int64_t y, std::int64_t z, const math::Grid& grid) {
    return {FromSteps(x, grid), FromSteps(y, grid), FromSteps(z, grid)};
  }

  // value, which its varint carries wider than its *Wire field's one byte.
  std::uint8_t Narrow(std::uint32_t value) {
    if (value > std::numeric_limits<std::uint8_t>::max()) {
      Fail(DecodeError::kOutOfRange);
      return 0;
    }
    return static_cast<std::uint8_t>(value);
  }

  // value as an enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ToEnum(std::int64_t value, Enum first, Enum last) {
    if (value < static_cast<std::int64_t>(first) || value > static_cast<std::int64_t>(last)) {
      Fail(DecodeError::kInvalidEnum);
      return first;
    }
    return static_cast<Enum>(value);
  }

  // bits, of which only those of mask may be set.
  std::uint32_t ToBits(std::uint32_t bits, std::uint32_t mask) {
    if ((bits & ~mask) != 0) {
      Fail(DecodeError::kReservedBits);
      return 0;
    }
    return bits;
  }

  // text, which may be at most max_length bytes long.
  std::string Text(std::string_view text, std::size_t max_length) {
    if (text.size() > max_length) {
      Fail(DecodeError::kFieldTooLong);
      return {};
    }
    return std::string(text);
  }

  // bytes as a pack hash: none is the all-zero one an absent field reads as,
  // and any other length than a hash's is a number wider than its field.
  PackHashWire PackHash(std::string_view bytes) {
    PackHashWire hash{};
    if (bytes.empty()) {
      return hash;
    }
    if (bytes.size() != hash.size()) {
      Fail(DecodeError::kOutOfRange);
      return hash;
    }
    std::ranges::transform(bytes, hash.begin(), [](char byte) { return static_cast<std::byte>(byte); });
    return hash;
  }

  // Whether a list of size elements is at most max long.
  bool Fits(int size, std::size_t max) {
    if (std::cmp_greater(size, max)) {
      Fail(DecodeError::kFieldTooLong);
      return false;
    }
    return true;
  }

  [[nodiscard]] std::optional<DecodeError> Error() const { return error_; }

 private:
  void Fail(DecodeError error) {
    if (!error_.has_value()) {
      error_ = error;
    }
  }

  std::optional<DecodeError> error_;
};

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

EntityStateWire Read(const pb::Body& body, Inbound& in) {
  EntityStateWire wire{.entity = static_cast<EntityIdWire>(body.entity())};
  wire.body.position = in.FromSteps(body.position_x(), body.position_y(), body.position_z(), math::kPositionGrid);
  wire.body.velocity = in.FromSteps(body.velocity_x(), body.velocity_y(), body.velocity_z(), math::kVelocityGrid);
  const std::uint32_t bits = in.ToBits(body.bits(), kBodyBitsMask);
  wire.body.stance = in.ToEnum(bits & kStanceMask, StanceWire::kStanding, StanceWire::kProne);
  wire.body.flags = static_cast<std::uint8_t>(bits >> kBodyFlagsShift);
  wire.body.stamina = in.FromSteps(body.stamina(), math::kStaminaGrid);
  wire.yaw = in.FromSteps(body.yaw(), math::kAngleGrid);
  return wire;
}

JoinRequestWire Read(const pb::JoinRequest& request, Inbound& in) {
  return JoinRequestWire{.engine_version = in.Text(request.engine_version(), kMaxEngineVersionLength),
                         .client_pack = in.PackHash(request.client_pack()),
                         .character = in.Text(request.character(), kMaxCharacterNameLength)};
}

ReenactRequestWire Read(const pb::ReenactRequest& request, Inbound& in) {
  return ReenactRequestWire{
      .engine_version = in.Text(request.engine_version(), kMaxEngineVersionLength),
      .client_pack = in.PackHash(request.client_pack()),
      .character = in.Text(request.character(), kMaxCharacterNameLength),
      .spawn = in.FromSteps(request.spawn_x(), request.spawn_y(), request.spawn_z(), math::kPositionGrid)};
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
  return JoinAcceptedWire{.session = static_cast<SessionIdWire>(accepted.session()),
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
                     return RosterEntryWire{.session = static_cast<SessionIdWire>(entry.session()),
                                            .character = in.Text(entry.character(), kMaxCharacterNameLength)};
                   })};
}

MatchStartWire Read(const pb::MatchStart& start, Inbound& in) {
  return MatchStartWire{.players = ReadList(start.players(), limits::kMaxPlayers, in,
                                            [&in](const pb::MatchPlayer& player) {
                                              return MatchPlayerWire{
                                                  .spawn = in.FromSteps(player.spawn_x(), player.spawn_y(),
                                                                        player.spawn_z(), math::kPositionGrid),
                                                  .session = static_cast<SessionIdWire>(player.session()),
                                                  .entity = static_cast<EntityIdWire>(player.entity()),
                                                  .character = in.Text(player.character(), kMaxCharacterNameLength)};
                                            }),
                        .first_tick = start.first_tick()};
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
                           .capture = in.Text(request.capture(), kMaxCaptureNameLength)};
}

ReplayViewWire Read(const pb::ReplayView& view, Inbound& in) {
  return ReplayViewWire{
      .tick = view.tick(),
      .players = ReadList(view.players(), limits::kMaxPlayers, in, [&in](const pb::PlayerView& player) {
        return PlayerViewWire{.pitch = in.FromSteps(player.pitch(), math::kAngleGrid),
                              .entity = static_cast<EntityIdWire>(player.entity()),
                              .flags = static_cast<std::uint8_t>(in.ToBits(player.bits(), PlayerViewWire::kAds))};
      })};
}

// message's body, or nullopt when it sets no member this build knows.
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
      return MatchEndWire{.winner = static_cast<SessionIdWire>(message.match_end().winner())};
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
    case pb::Message::kReplayView:
      return Read(message.replay_view(), in);
    case pb::Message::kReenactRequest:
      return Read(message.reenact_request(), in);
    case pb::Message::BODY_NOT_SET:
      return std::nullopt;
  }
  return std::nullopt;
}

// Readdress's framing (ADR-0038): protobuf's own tags and varints, around the
// generated messages, so a tick's bodies are serialized once.

// The tag before a field: its number, shifted past three bits of wire type.
constexpr unsigned kTagTypeBits = 3U;
constexpr std::uint32_t kTagTypeMask = (1U << kTagTypeBits) - 1U;
constexpr std::uint32_t kVarintType = 0;
constexpr std::uint32_t kFixed64Type = 1;
constexpr std::uint32_t kLengthDelimitedType = 2;
constexpr std::uint32_t kFixed32Type = 5;
constexpr int kFixed64Size = 8;
constexpr int kFixed32Size = 4;

constexpr std::uint32_t kAuthoritativeStateTag =
    (static_cast<std::uint32_t>(pb::Message::kAuthoritativeStateFieldNumber) << kTagTypeBits) | kLengthDelimitedType;

// The last of an Authoritative State's shared fields, which number below every
// field that is its recipient's own (proto/augusta.proto).
constexpr std::uint32_t kLastSharedField = pb::AuthoritativeState::kBodiesFieldNumber;

// Steps input past the value of the field tag begins: false if it runs past
// the end, or is of a wire type an Authoritative State's fields never have.
bool SkipValue(google::protobuf::io::CodedInputStream& input, std::uint32_t tag) {
  std::uint64_t varint = 0;
  std::uint32_t length = 0;
  switch (tag & kTagTypeMask) {
    case kVarintType:
      return input.ReadVarint64(&varint);
    case kFixed64Type:
      return input.Skip(kFixed64Size);
    case kLengthDelimitedType:
      return input.ReadVarint32(&length) && length <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
             input.Skip(static_cast<int>(length));
    case kFixed32Type:
      return input.Skip(kFixed32Size);
    default:
      return false;
  }
}

// The bytes of payload's shared fields, if it is an Authoritative State as
// Encode makes one: the envelope's member, as long as the rest of the payload,
// with its fields in their numbers' order, so its shared fields come first and
// end where its recipient's own begin.
std::optional<std::span<const std::byte>> SharedFieldsOf(std::span<const std::byte> payload) {
  if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  google::protobuf::io::CodedInputStream input(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                               static_cast<int>(payload.size()));
  std::uint32_t length = 0;
  if (input.ReadTag() != kAuthoritativeStateTag || !input.ReadVarint32(&length)) {
    return std::nullopt;
  }
  const auto start = static_cast<std::size_t>(input.CurrentPosition());
  if (payload.size() - start != length) {
    return std::nullopt;
  }
  std::optional<std::size_t> own_start;
  while (static_cast<std::size_t>(input.CurrentPosition()) < payload.size()) {
    const auto at = static_cast<std::size_t>(input.CurrentPosition());
    const std::uint32_t tag = input.ReadTag();
    const std::uint32_t field = tag >> kTagTypeBits;
    if (field == 0 || (own_start.has_value() && field <= kLastSharedField) || !SkipValue(input, tag)) {
      return std::nullopt;
    }
    if (!own_start.has_value() && field > kLastSharedField) {
      own_start = at;
    }
  }
  return payload.subspan(start, own_start.value_or(payload.size()) - start);
}

// The capture records' codec: one CaptureRecordTypeWire byte, then the
// record's fields, fixed-width and little-endian, with a string as a one-byte
// length and its bytes.

constexpr int kBitsPerByte = 8;

// Builds a record front to back. The first field it is handed that the format
// cannot carry is remembered, and Finish then gives that error and no record,
// so an encoder writes all of a record's fields and asks once at the end: the
// write side of Reader.
class Writer {
 public:
  void Put(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

  void Put(std::span<const std::byte> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }

  // Whether holds, remembering error if it does not.
  bool Check(bool holds, EncodeError error) {
    if (!holds && !error_.has_value()) {
      error_ = error;
    }
    return holds;
  }

  // value, an enumerator that must lie between first and last, as its byte.
  template <typename Enum>
  std::uint8_t FromEnum(Enum value, Enum first, Enum last) {
    const auto byte = static_cast<std::uint8_t>(value);
    Check(byte >= static_cast<std::uint8_t>(first) && byte <= static_cast<std::uint8_t>(last),
          EncodeError::kInvalidEnum);
    return byte;
  }

  // flags, of which only the bits of mask may be set.
  std::uint8_t FromFlags(std::uint8_t flags, std::uint8_t mask) {
    Check((flags & ~mask) == 0, EncodeError::kReservedBits);
    return flags;
  }

  std::expected<BytesWire, EncodeError> Finish() && {
    if (error_.has_value()) {
      return std::unexpected(*error_);
    }
    return std::move(bytes_);
  }

 private:
  BytesWire bytes_;
  std::optional<EncodeError> error_;
};

void WriteU8(Writer& out, std::uint8_t value) { out.Put(value); }

// value's bytes, least significant first.
template <std::unsigned_integral Unsigned>
void WriteUnsigned(Writer& out, Unsigned value) {
  for (int shift = 0; shift < std::numeric_limits<Unsigned>::digits; shift += kBitsPerByte) {
    WriteU8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

void WriteU32(Writer& out, std::uint32_t value) { WriteUnsigned(out, value); }

void WriteU64(Writer& out, std::uint64_t value) { WriteUnsigned(out, value); }

// value as a whole count of grid's step (ADR-0038), in the grid's bytes.
void WriteSteps(Writer& out, float value, const math::Grid& grid) {
  const auto bits = static_cast<std::uint32_t>(math::ToSteps(value, grid));
  for (int i = 0; i < grid.bytes; ++i) {
    WriteU8(out, static_cast<std::uint8_t>(bits >> (kBitsPerByte * i)));
  }
}

void WriteVec3(Writer& out, const math::Vec3& value, const math::Grid& grid) {
  WriteSteps(out, value.x, grid);
  WriteSteps(out, value.y, grid);
  WriteSteps(out, value.z, grid);
}

// A one-byte length and the bytes, at most max_length of them: the write side
// of Reader::ReadString.
void WriteString(Writer& out, std::string_view text, std::size_t max_length) {
  if (!out.Check(text.size() <= max_length, EncodeError::kFieldTooLong)) {
    return;
  }
  WriteU8(out, static_cast<std::uint8_t>(text.size()));
  for (const char letter : text) {
    WriteU8(out, static_cast<std::uint8_t>(letter));
  }
}

// A command, its flags and stance in one byte as its bitfield holds them.
void WriteCommand(Writer& out, const CommandWire& command) {
  WriteVec3(out, command.direction, math::kDirectionGrid);
  WriteSteps(out, command.yaw, math::kAngleGrid);
  WriteSteps(out, command.pitch, math::kAngleGrid);
  const std::uint8_t flags = out.FromFlags(command.flags, kCommandFlagsMask);
  const std::uint8_t stance = out.FromEnum(command.desired_stance, StanceWire::kStanding, StanceWire::kProne);
  WriteU8(out, static_cast<std::uint8_t>(flags | (stance << kCommandStanceShift)));
  WriteU8(out, command.seen_age);
  WriteSteps(out, command.seen_fraction, math::kFractionGrid);
}

// Walks a record front to back. The first problem it meets is remembered and
// every read after it returns a zero value, so a decoder can read all of a
// record's fields and ask once at the end whether they were all there.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::uint8_t ReadU8() {
    if (bytes_.empty()) {
      Fail(DecodeError::kMalformed);
      return 0;
    }
    const auto value = static_cast<std::uint8_t>(bytes_.front());
    bytes_ = bytes_.subspan(1);
    return value;
  }

  std::uint32_t ReadU32() { return ReadUnsigned<std::uint32_t>(); }

  std::uint64_t ReadU64() { return ReadUnsigned<std::uint64_t>(); }

  // value as an enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ToEnum(std::uint8_t value, Enum first, Enum last) {
    if (value < static_cast<std::uint8_t>(first) || value > static_cast<std::uint8_t>(last)) {
      Fail(DecodeError::kInvalidEnum);
      return first;
    }
    return static_cast<Enum>(value);
  }

  // value as bits of which only those of mask may be set.
  std::uint8_t ToBits(std::uint8_t value, std::uint32_t mask) {
    if ((value & ~mask) != 0) {
      Fail(DecodeError::kReservedBits);
      return 0;
    }
    return value;
  }

  // A count of grid's step, as the value it stands for. Every count a grid's
  // bytes can hold is in its range, so there is nothing to refuse.
  float ReadSteps(const math::Grid& grid) {
    std::uint32_t bits = 0;
    for (int i = 0; i < grid.bytes; ++i) {
      bits |= static_cast<std::uint32_t>(ReadU8()) << (kBitsPerByte * i);
    }
    const int width = kBitsPerByte * grid.bytes;
    if (grid.min < 0 && width < std::numeric_limits<std::uint32_t>::digits) {
      const std::uint32_t sign = 1U << (width - 1);
      bits = (bits ^ sign) - sign;
    }
    return math::FromSteps(static_cast<std::int32_t>(bits), grid);
  }

  math::Vec3 ReadVec3(const math::Grid& grid) {
    const float x = ReadSteps(grid);
    const float y = ReadSteps(grid);
    const float z = ReadSteps(grid);
    return {x, y, z};
  }

  PackHashWire ReadPackHash() {
    PackHashWire hash{};
    if (bytes_.size() < hash.size()) {
      Fail(DecodeError::kMalformed);
      return hash;
    }
    std::ranges::copy(bytes_.first(hash.size()), hash.begin());
    bytes_ = bytes_.subspan(hash.size());
    return hash;
  }

  // A string of at most max_length bytes, checked before anything is
  // allocated for it.
  std::string ReadString(std::size_t max_length) {
    const std::size_t length = ReadU8();
    if (length > max_length) {
      Fail(DecodeError::kFieldTooLong);
      return {};
    }
    if (bytes_.size() < length) {
      Fail(DecodeError::kMalformed);
      return {};
    }
    std::string value(length, '\0');
    for (std::size_t i = 0; i < length; ++i) {
      value[i] = static_cast<char>(bytes_[i]);
    }
    bytes_ = bytes_.subspan(length);
    return value;
  }

  [[nodiscard]] std::optional<DecodeError> Error() const { return error_; }
  [[nodiscard]] bool AtEnd() const { return bytes_.empty(); }

 private:
  void Fail(DecodeError error) {
    if (!error_.has_value()) {
      error_ = error;
    }
  }

  // The read side of WriteUnsigned.
  template <std::unsigned_integral Unsigned>
  Unsigned ReadUnsigned() {
    constexpr std::size_t kSize = sizeof(Unsigned);
    if (bytes_.size() < kSize) {
      Fail(DecodeError::kMalformed);
      return 0;
    }
    Unsigned value = 0;
    for (std::size_t i = 0; i < kSize; ++i) {
      value |= static_cast<Unsigned>(bytes_[i]) << (kBitsPerByte * i);
    }
    bytes_ = bytes_.subspan(kSize);
    return value;
  }

  std::span<const std::byte> bytes_;
  std::optional<DecodeError> error_;
};

// The read side of WriteCommand.
CommandWire ReadCommand(Reader& reader) {
  CommandWire command;
  command.direction = reader.ReadVec3(math::kDirectionGrid);
  command.yaw = reader.ReadSteps(math::kAngleGrid);
  command.pitch = reader.ReadSteps(math::kAngleGrid);
  const std::uint8_t packed = reader.ToBits(reader.ReadU8(), kCommandBitsMask);
  command.flags = packed & kCommandFlagsMask;
  command.desired_stance = reader.ToEnum(static_cast<std::uint8_t>(packed >> kCommandStanceShift),
                                         StanceWire::kStanding, StanceWire::kProne);
  command.seen_age = reader.ReadU8();
  command.seen_fraction = reader.ReadSteps(math::kFractionGrid);
  return command;
}

CaptureHeaderWire ReadCaptureHeader(Reader& reader) {
  CaptureHeaderWire header;
  header.format_version = reader.ReadU8();
  header.engine_version = reader.ReadString(kMaxEngineVersionLength);
  header.server_pack = reader.ReadPackHash();
  header.client_pack = reader.ReadPackHash();
  header.tick_rate_hz = reader.ReadU8();
  header.started_unix_ms = std::bit_cast<std::int64_t>(reader.ReadU64());
  return header;
}

CapturedJoinWire ReadCapturedJoin(Reader& reader) {
  CapturedJoinWire join;
  join.offset = reader.ReadU32();
  join.player = reader.ReadU8();
  join.session = static_cast<SessionIdWire>(reader.ReadU32());
  join.character = reader.ReadString(kMaxCharacterNameLength);
  join.spawn = reader.ReadVec3(math::kPositionGrid);
  return join;
}

// The command goes last, so a reader that only counts commands (augusta-inspect)
// stops before it.
CapturedCommandWire ReadCapturedCommand(Reader& reader) {
  CapturedCommandWire command;
  command.offset = reader.ReadU32();
  command.player = reader.ReadU8();
  command.seen_offset = std::bit_cast<std::int32_t>(reader.ReadU32());
  command.command = ReadCommand(reader);
  return command;
}

CapturedLeaveWire ReadCapturedLeave(Reader& reader) {
  CapturedLeaveWire leave;
  leave.offset = reader.ReadU32();
  leave.player = reader.ReadU8();
  return leave;
}

CapturedDeathWire ReadCapturedDeath(Reader& reader) {
  CapturedDeathWire death;
  death.offset = reader.ReadU32();
  death.victim = reader.ReadU8();
  death.killer = reader.ReadU8();
  return death;
}

CapturedMatchEndWire ReadCapturedMatchEnd(Reader& reader) {
  CapturedMatchEndWire end;
  end.offset = reader.ReadU32();
  end.winner = reader.ReadU8();
  return end;
}

// nullopt when type is not a record of a capture.
std::optional<CaptureRecordWire> ReadCaptureRecordBody(CaptureRecordTypeWire type, Reader& reader) {
  switch (type) {
    case CaptureRecordTypeWire::kHeader:
      return ReadCaptureHeader(reader);
    case CaptureRecordTypeWire::kJoin:
      return ReadCapturedJoin(reader);
    case CaptureRecordTypeWire::kCommand:
      return ReadCapturedCommand(reader);
    case CaptureRecordTypeWire::kLeave:
      return ReadCapturedLeave(reader);
    case CaptureRecordTypeWire::kDeath:
      return ReadCapturedDeath(reader);
    case CaptureRecordTypeWire::kMatchEnd:
      return ReadCapturedMatchEnd(reader);
  }
  return std::nullopt;
}

// One overload per record: the type tag, then the fields, an event's offset first.
struct CaptureRecordEncoder {
  Writer& out;

  void operator()(const CaptureHeaderWire& header) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kHeader));
    WriteU8(out, header.format_version);
    WriteString(out, header.engine_version, kMaxEngineVersionLength);
    out.Put(header.server_pack);
    out.Put(header.client_pack);
    WriteU8(out, header.tick_rate_hz);
    WriteU64(out, std::bit_cast<std::uint64_t>(header.started_unix_ms));
  }

  void operator()(const CapturedJoinWire& join) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kJoin));
    WriteU32(out, join.offset);
    WriteU8(out, join.player);
    WriteU32(out, static_cast<std::uint32_t>(join.session));
    WriteString(out, join.character, kMaxCharacterNameLength);
    WriteVec3(out, join.spawn, math::kPositionGrid);
  }

  void operator()(const CapturedCommandWire& command) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kCommand));
    WriteU32(out, command.offset);
    WriteU8(out, command.player);
    WriteU32(out, std::bit_cast<std::uint32_t>(command.seen_offset));
    WriteCommand(out, command.command);
  }

  void operator()(const CapturedLeaveWire& leave) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kLeave));
    WriteU32(out, leave.offset);
    WriteU8(out, leave.player);
  }

  void operator()(const CapturedDeathWire& death) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kDeath));
    WriteU32(out, death.offset);
    WriteU8(out, death.victim);
    WriteU8(out, death.killer);
  }

  void operator()(const CapturedMatchEndWire& end) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kMatchEnd));
    WriteU32(out, end.offset);
    WriteU8(out, end.winner);
  }
};

}  // namespace

std::expected<BytesWire, EncodeError> Encode(const MessageWire& message) {
  CallArena arena;
  auto& encoded = arena.Create<pb::Message>();
  Outbound out;
  std::visit(Encoder{.message = encoded, .out = out}, message);
  if (out.Error().has_value()) {
    return std::unexpected(*out.Error());
  }
  return Serialize(encoded);
}

std::expected<void, EncodeError> Readdress(BytesWire& payload, const AuthoritativeStateWire& recipient) {
  const std::optional<std::span<const std::byte>> shared = SharedFieldsOf(payload);
  if (!shared.has_value()) {
    return std::unexpected(EncodeError::kNotAnAuthoritativeState);
  }
  CallArena arena;
  auto& own = arena.Create<pb::AuthoritativeState>();
  WriteOwn(recipient, own);
  const std::size_t own_size = own.ByteSizeLong();
  const auto length = static_cast<std::uint32_t>(shared->size() + own_size);
  using google::protobuf::io::CodedOutputStream;
  BytesWire readdressed(CodedOutputStream::VarintSize32(kAuthoritativeStateTag) +
                        CodedOutputStream::VarintSize32(length) + length);
  auto* at = reinterpret_cast<std::uint8_t*>(readdressed.data());
  at = CodedOutputStream::WriteVarint32ToArray(kAuthoritativeStateTag, at);
  at = CodedOutputStream::WriteVarint32ToArray(length, at);
  at = std::ranges::transform(*shared, at, [](std::byte byte) { return static_cast<std::uint8_t>(byte); }).out;
  own.SerializeWithCachedSizesToArray(at);
  payload = std::move(readdressed);
  return {};
}

MessageTypeWire TypeOf(const MessageWire& message) {
  struct Type {
    MessageTypeWire operator()(const JoinRequestWire& /*wire*/) const { return MessageTypeWire::kJoinRequest; }
    MessageTypeWire operator()(const JoinAcceptedWire& /*wire*/) const { return MessageTypeWire::kJoinAccepted; }
    MessageTypeWire operator()(const JoinRefusedWire& /*wire*/) const { return MessageTypeWire::kJoinRefused; }
    MessageTypeWire operator()(const CommandsWire& /*wire*/) const { return MessageTypeWire::kCommands; }
    MessageTypeWire operator()(const AuthoritativeStateWire& /*wire*/) const {
      return MessageTypeWire::kAuthoritativeState;
    }
    MessageTypeWire operator()(const LobbyWire& /*wire*/) const { return MessageTypeWire::kLobby; }
    MessageTypeWire operator()(const ReadyWire& /*wire*/) const { return MessageTypeWire::kReady; }
    MessageTypeWire operator()(const MatchStartWire& /*wire*/) const { return MessageTypeWire::kMatchStart; }
    MessageTypeWire operator()(const MatchEndWire& /*wire*/) const { return MessageTypeWire::kMatchEnd; }
    MessageTypeWire operator()(const ShotWire& /*wire*/) const { return MessageTypeWire::kShot; }
    MessageTypeWire operator()(const HitConfirmationWire& /*wire*/) const { return MessageTypeWire::kHitConfirmation; }
    MessageTypeWire operator()(const DeathWire& /*wire*/) const { return MessageTypeWire::kDeath; }
    MessageTypeWire operator()(const ReplayListRequestWire& /*wire*/) const {
      return MessageTypeWire::kReplayListRequest;
    }
    MessageTypeWire operator()(const ReplayListWire& /*wire*/) const { return MessageTypeWire::kReplayList; }
    MessageTypeWire operator()(const ReplayRequestWire& /*wire*/) const { return MessageTypeWire::kReplayRequest; }
    MessageTypeWire operator()(const ReplayViewWire& /*wire*/) const { return MessageTypeWire::kReplayView; }
    MessageTypeWire operator()(const ReenactRequestWire& /*wire*/) const { return MessageTypeWire::kReenactRequest; }
  };
  return std::visit(Type{}, message);
}

std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  CallArena arena;
  auto& message = arena.Create<pb::Message>();
  if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      !message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    return std::unexpected(DecodeError::kMalformed);
  }
  Inbound in;
  std::optional<MessageWire> body = ReadBody(message, in);
  if (!body.has_value()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  if (in.Error().has_value()) {
    return std::unexpected(*in.Error());
  }
  return std::move(*body);
}

std::expected<BytesWire, EncodeError> EncodeCaptureRecord(const CaptureRecordWire& record) {
  Writer out;
  std::visit(CaptureRecordEncoder{.out = out}, record);
  return std::move(out).Finish();
}

CaptureRecordTypeWire TypeOf(const CaptureRecordWire& record) {
  struct Type {
    CaptureRecordTypeWire operator()(const CaptureHeaderWire& /*wire*/) const { return CaptureRecordTypeWire::kHeader; }
    CaptureRecordTypeWire operator()(const CapturedJoinWire& /*wire*/) const { return CaptureRecordTypeWire::kJoin; }
    CaptureRecordTypeWire operator()(const CapturedCommandWire& /*wire*/) const {
      return CaptureRecordTypeWire::kCommand;
    }
    CaptureRecordTypeWire operator()(const CapturedLeaveWire& /*wire*/) const { return CaptureRecordTypeWire::kLeave; }
    CaptureRecordTypeWire operator()(const CapturedDeathWire& /*wire*/) const { return CaptureRecordTypeWire::kDeath; }
    CaptureRecordTypeWire operator()(const CapturedMatchEndWire& /*wire*/) const {
      return CaptureRecordTypeWire::kMatchEnd;
    }
  };
  return std::visit(Type{}, record);
}

std::expected<CaptureRecordWire, DecodeError> DecodeCaptureRecord(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  Reader reader(payload.subspan(1));
  std::optional<CaptureRecordWire> record =
      ReadCaptureRecordBody(static_cast<CaptureRecordTypeWire>(payload.front()), reader);
  if (!record.has_value()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  if (reader.Error().has_value()) {
    return std::unexpected(*reader.Error());
  }
  if (!reader.AtEnd()) {
    return std::unexpected(DecodeError::kMalformed);
  }
  return std::move(*record);
}

std::string_view DescribeDecodeError(DecodeError error) {
  switch (error) {
    case DecodeError::kMalformed:
      return "payload does not parse";
    case DecodeError::kUnknownType:
      return "no message type this build knows";
    case DecodeError::kInvalidEnum:
      return "field holds a value its enumeration lacks";
    case DecodeError::kReservedBits:
      return "flags field has a bit that is none of its flags";
    case DecodeError::kOutOfRange:
      return "number outside its grid or wider than its field";
    case DecodeError::kFieldTooLong:
      return "string or list field longer than allowed";
  }
  return "unknown decode error";
}

std::string_view DescribeEncodeError(EncodeError error) {
  switch (error) {
    case EncodeError::kFieldTooLong:
      return "string or list field longer than the protocol carries";
    case EncodeError::kReservedBits:
      return "flags field has a bit that is none of its flags";
    case EncodeError::kInvalidEnum:
      return "field holds a value its enumeration lacks";
    case EncodeError::kNotAnAuthoritativeState:
      return "payload to readdress is not an authoritative state's";
  }
  return "unknown encode error";
}

}  // namespace augusta::protocol
