#include "augusta/protocol.h"

#include <algorithm>
#include <array>
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

#include <boost/uuid/uuid.hpp>
#include <google/protobuf/arena.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message_lite.h>
#include <google/protobuf/repeated_ptr_field.h>
#include <utf8_range.h>

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

// A body's stance takes bits 0-1 of its bitfield and its flags the bits from 2
// up: exhausted bit 2, ADS bit 3 (ADR-0038).
constexpr std::uint8_t kBodyFlagsMask = BodyStateWire::kExhausted | BodyStateWire::kAds;
constexpr unsigned kBodyFlagsShift = 2U;
constexpr std::uint32_t kBodyBitsMask = kStanceMask | (std::uint32_t{kBodyFlagsMask} << kBodyFlagsShift);

#if AUGUSTA_DEBUG_TOOLS
// A Shot judgement's bitfield (ADR-0038): what the round struck in bits 0-1,
// the Body part in bits 2-3, its flags from bit 4.
constexpr std::uint32_t kStruckMask = 0x03U;
constexpr unsigned kJudgementPartShift = 2U;
constexpr std::uint32_t kPartMask = 0x03U;
constexpr unsigned kJudgementFlagsShift = 4U;
constexpr std::uint8_t kJudgementFlagsMask = ShotJudgementWire::kEntriesLeftOut;
constexpr std::uint32_t kJudgementBitsMask =
    kStruckMask | (kPartMask << kJudgementPartShift) | (std::uint32_t{kJudgementFlagsMask} << kJudgementFlagsShift);
#endif

// The first block of each call's arena, on the stack: more than an
// Authoritative State or a Commands message as large as the protocol allows
// builds or parses into, so neither touches the heap but for an encode's
// payload. A larger message (a Replay list) grows the arena there.
constexpr std::size_t kArenaBlockSize = 4096;

// An arena local to one call of the codec (ADR-0007): the messages the
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

  // text, which may be at most max_length bytes long, and must be UTF-8.
  std::string_view Text(std::string_view text, std::size_t max_length) {
    Check(text.size() <= max_length, EncodeError::kFieldTooLong);
    Check(IsUtf8(text), EncodeError::kNotUtf8);
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
  body.set_pitch(Steps(wire.pitch, math::kAngleGrid));
  body.set_stride(wire.body.stride);
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
  rifle.set_hip_offset_right(wire.rifle.hip_offset_right);
  rifle.set_hip_offset_up(wire.rifle.hip_offset_up);
  rifle.set_hip_offset_back(wire.rifle.hip_offset_back);
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

// One overload per message: the envelope's member, then its fields.
struct Encoder {
  pb::Message& message;
  Outbound& out;

  void operator()(const JoinRequestWire& wire) const {
    pb::JoinRequest& request = *message.mutable_join_request();
    request.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
    request.set_client_pack(BytesOf(wire.client_pack));
    request.set_character(out.Text(wire.character, kMaxCharacterNameLength));
    request.set_run(BytesOf(wire.run));
  }

  void operator()(const JoinAcceptedWire& wire) const {
    pb::JoinAccepted& accepted = *message.mutable_join_accepted();
    accepted.set_session(BytesOf(wire.session));
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
        written.set_session(BytesOf(entry.session));
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
    start.set_match(BytesOf(wire.match));
  }

  void operator()(const MatchEndWire& wire) const { message.mutable_match_end()->set_winner(BytesOf(wire.winner)); }

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
    request.set_run(BytesOf(wire.run));
  }

  void operator()(const ReenactRequestWire& wire) const {
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
  void operator()(const DebugSubscribeWire& wire) const { message.mutable_debug_subscribe()->set_flags(wire.flags); }

  void operator()(const ShotJudgementWire& wire) const {
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

  // value, which its varint carries wider than its *Wire field, Field: one
  // byte unless said otherwise.
  template <typename Field = std::uint8_t>
  Field Narrow(std::uint32_t value) {
    if (value > std::numeric_limits<Field>::max()) {
      Fail(DecodeError::kOutOfRange);
      return 0;
    }
    return static_cast<Field>(value);
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

  // text, which may be at most max_length bytes long, and must be UTF-8: the
  // schema holds it as bytes, so protobuf's parse never checks it, and never
  // logs a line a peer's bad text would make at will (NFR-05).
  std::string Text(std::string_view text, std::size_t max_length) {
    if (text.size() > max_length) {
      Fail(DecodeError::kFieldTooLong);
      return {};
    }
    if (!IsUtf8(text)) {
      Fail(DecodeError::kMalformed);
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

  // bytes as a UUID (a Run ID, a Match ID): none is the nil one an absent field reads as,
  // and any other length than a UUID's is a number wider than its field.
  boost::uuids::uuid Uuid(std::string_view bytes) {
    boost::uuids::uuid id{};
    if (bytes.empty()) {
      return id;
    }
    if (bytes.size() != id.size()) {
      Fail(DecodeError::kOutOfRange);
      return id;
    }
    std::ranges::transform(bytes, id.begin(), [](char byte) { return static_cast<std::uint8_t>(byte); });
    return id;
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
  wire.pitch = in.FromSteps(body.pitch(), math::kAngleGrid);
  wire.body.stride = in.Narrow(body.stride());
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
                   .magazine_capacity = in.Narrow(rifle.magazine_capacity()),
                   .hip_offset_right = rifle.hip_offset_right(),
                   .hip_offset_up = rifle.hip_offset_up(),
                   .hip_offset_back = rifle.hip_offset_back()};
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

// Readdress's framing (ADR-0038): protobuf's own tags and varints, around the
// generated messages, so a tick's bodies are serialized once; and the envelope
// tag a payload's type is read from without decoding it.

// The tag before a field: its number, shifted past three bits of wire type.
constexpr unsigned kTagTypeBits = 3U;
constexpr std::uint32_t kTagTypeMask = (1U << kTagTypeBits) - 1U;
constexpr std::uint32_t kVarintType = 0;
constexpr std::uint32_t kFixed64Type = 1;
constexpr std::uint32_t kLengthDelimitedType = 2;
constexpr std::uint32_t kFixed32Type = 5;
constexpr int kFixed64Size = 8;
constexpr int kFixed32Size = 4;
// A tag is a varint of at most 32 bits: five bytes of seven bits each.
constexpr std::size_t kMaxTagBytes = 5;

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

// A Match capture's records (ADR-0050), each its augusta.CaptureRecord member,
// mapped as the messages are: a captured Command's command is a Commands
// message's augusta.Command, its sequence left 0.

// One overload per record: the envelope's member, then its fields.
struct RecordEncoder {
  pb::CaptureRecord& record;
  Outbound& out;

  void operator()(const CaptureHeaderWire& wire) const {
    pb::CaptureHeader& header = *record.mutable_header();
    header.set_server_pack(BytesOf(wire.server_pack));
    header.set_client_pack(BytesOf(wire.client_pack));
    header.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
    header.set_started_unix_ms(wire.started_unix_ms);
    header.set_tick_rate_hz(wire.tick_rate_hz);
    header.set_match(BytesOf(wire.match));
  }

  void operator()(const CapturedJoinWire& wire) const {
    pb::CapturedJoin& join = *record.mutable_join();
    join.set_offset(wire.offset);
    join.set_player(wire.player);
    join.set_session(BytesOf(wire.session));
    join.set_character(out.Text(wire.character, kMaxCharacterNameLength));
    join.set_spawn_x(Steps(wire.spawn.x, math::kPositionGrid));
    join.set_spawn_y(Steps(wire.spawn.y, math::kPositionGrid));
    join.set_spawn_z(Steps(wire.spawn.z, math::kPositionGrid));
  }

  void operator()(const CapturedCommandWire& wire) const {
    pb::CapturedCommand& command = *record.mutable_command();
    command.set_offset(wire.offset);
    command.set_player(wire.player);
    command.set_seen_offset(wire.seen_offset);
    Write(wire.command, *command.mutable_command(), out);
  }

  void operator()(const CapturedLeaveWire& wire) const {
    pb::CapturedLeave& leave = *record.mutable_leave();
    leave.set_offset(wire.offset);
    leave.set_player(wire.player);
  }

  void operator()(const CapturedDeathWire& wire) const {
    pb::CapturedDeath& death = *record.mutable_death();
    death.set_offset(wire.offset);
    death.set_victim(wire.victim);
    death.set_killer(wire.killer);
  }

  void operator()(const CapturedMatchEndWire& wire) const {
    pb::CapturedMatchEnd& end = *record.mutable_match_end();
    end.set_offset(wire.offset);
    end.set_winner(wire.winner);
  }
};

CaptureHeaderWire Read(const pb::CaptureHeader& header, Inbound& in) {
  return CaptureHeaderWire{.server_pack = in.PackHash(header.server_pack()),
                           .client_pack = in.PackHash(header.client_pack()),
                           .engine_version = in.Text(header.engine_version(), kMaxEngineVersionLength),
                           .started_unix_ms = header.started_unix_ms(),
                           .match = in.Uuid(header.match()),
                           .tick_rate_hz = in.Narrow(header.tick_rate_hz())};
}

CapturedJoinWire Read(const pb::CapturedJoin& join, Inbound& in) {
  return CapturedJoinWire{.spawn = in.FromSteps(join.spawn_x(), join.spawn_y(), join.spawn_z(), math::kPositionGrid),
                          .offset = join.offset(),
                          .session = in.Uuid(join.session()),
                          .character = in.Text(join.character(), kMaxCharacterNameLength),
                          .player = in.Narrow(join.player())};
}

CapturedCommandWire Read(const pb::CapturedCommand& command, Inbound& in) {
  return CapturedCommandWire{.command = Read(command.command(), in),
                             .offset = command.offset(),
                             .seen_offset = command.seen_offset(),
                             .player = in.Narrow(command.player())};
}

// record's member, or nullopt when it sets none this build knows.
std::optional<CaptureRecordWire> ReadRecord(const pb::CaptureRecord& record, Inbound& in) {
  switch (record.record_case()) {
    case pb::CaptureRecord::kHeader:
      return Read(record.header(), in);
    case pb::CaptureRecord::kJoin:
      return Read(record.join(), in);
    case pb::CaptureRecord::kCommand:
      return Read(record.command(), in);
    case pb::CaptureRecord::kLeave:
      return CapturedLeaveWire{.offset = record.leave().offset(), .player = in.Narrow(record.leave().player())};
    case pb::CaptureRecord::kDeath:
      return CapturedDeathWire{.offset = record.death().offset(),
                               .victim = in.Narrow(record.death().victim()),
                               .killer = in.Narrow(record.death().killer())};
    case pb::CaptureRecord::kMatchEnd:
      return CapturedMatchEndWire{.offset = record.match_end().offset(),
                                  .winner = in.Narrow(record.match_end().winner())};
    case pb::CaptureRecord::RECORD_NOT_SET:
      return std::nullopt;
  }
  return std::nullopt;
}

// What read maps payload's Envelope (augusta.Message or augusta.CaptureRecord)
// to, or why it is none of this build: kMalformed if protobuf cannot parse it,
// kUnknownType if it sets no member this build knows, else the first value the
// mapping refused.
template <typename Wire, typename Envelope, typename ReadEnvelope>
std::expected<Wire, DecodeError> DecodeEnvelope(std::span<const std::byte> payload, ReadEnvelope read) {
  CallArena arena;
  auto& envelope = arena.Create<Envelope>();
  if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      !envelope.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    return std::unexpected(DecodeError::kMalformed);
  }
  Inbound in;
  std::optional<Wire> wire = read(envelope, in);
  if (!wire.has_value()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  if (in.Error().has_value()) {
    return std::unexpected(*in.Error());
  }
  return std::move(*wire);
}

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
    MessageTypeWire operator()(const ReenactRequestWire& /*wire*/) const { return MessageTypeWire::kReenactRequest; }
#if AUGUSTA_DEBUG_TOOLS
    MessageTypeWire operator()(const DebugSubscribeWire& /*wire*/) const { return MessageTypeWire::kDebugSubscribe; }
    MessageTypeWire operator()(const ShotJudgementWire& /*wire*/) const { return MessageTypeWire::kShotJudgement; }
#endif
  };
  return std::visit(Type{}, message);
}

MessageTypeWire TypeOf(std::span<const std::byte> payload) {
  google::protobuf::io::CodedInputStream input(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                               static_cast<int>(std::min(payload.size(), kMaxTagBytes)));
  return static_cast<MessageTypeWire>(input.ReadTag() >> kTagTypeBits);
}

std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload) {
  return DecodeEnvelope<MessageWire, pb::Message>(payload, ReadBody);
}

std::expected<BytesWire, EncodeError> EncodeCaptureRecord(const CaptureRecordWire& record) {
  CallArena arena;
  auto& encoded = arena.Create<pb::CaptureRecord>();
  Outbound out;
  std::visit(RecordEncoder{.record = encoded, .out = out}, record);
  if (out.Error().has_value()) {
    return std::unexpected(*out.Error());
  }
  return Serialize(encoded);
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
  return DecodeEnvelope<CaptureRecordWire, pb::CaptureRecord>(payload, ReadRecord);
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
    case EncodeError::kNotUtf8:
      return "string field is not utf-8";
  }
  return "unknown encode error";
}

bool IsUtf8(std::string_view text) { return utf8_range_IsValid(text.data(), text.size()) != 0; }

}  // namespace augusta::protocol
