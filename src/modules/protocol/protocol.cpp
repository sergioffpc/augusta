#include "augusta/protocol.h"

#include <algorithm>
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
#include <utility>
#include <variant>
#include <vector>

#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/primitives.h"

namespace augusta::protocol {

namespace {

constexpr int kBitsPerByte = 8;

// Builds a payload front to back. The first field it is handed that the
// protocol cannot carry is remembered, and Finish then gives that error and no
// payload, so an encoder writes all of a message's fields and asks once at the
// end, and nothing half-written ever leaves it: the write side of Reader.
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

// A tick in as many bytes as primitives::Tick has.
void WriteTick(Writer& out, primitives::Tick value) { WriteUnsigned(out, value); }

// A command sequence in as many bytes as primitives::Sequence has.
void WriteSequence(Writer& out, primitives::Sequence value) { WriteUnsigned(out, value); }

void WriteF32(Writer& out, float value) { WriteU32(out, std::bit_cast<std::uint32_t>(value)); }

// value as a whole count of grid's step (ADR-0038), in the grid's bytes.
void WriteSteps(Writer& out, float value, const math::Grid& grid) {
  const auto bits = static_cast<std::uint32_t>(math::ToSteps(value, grid));
  for (int i = 0; i < grid.bytes; ++i) {
    WriteU8(out, static_cast<std::uint8_t>(bits >> (kBitsPerByte * i)));
  }
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

// A character, by its name in the scenario's manifest: the write side of Reader::ReadCharacter.
void WriteCharacter(Writer& out, std::string_view character) { WriteString(out, character, kMaxCharacterNameLength); }

void WritePackHash(Writer& out, const PackHashWire& hash) { out.Put(hash); }

// A list of at most max elements: a one-byte count, then each one as write writes it.
template <typename Element, typename Write>
void WriteList(Writer& out, const std::vector<Element>& list, std::size_t max, Write write) {
  if (!out.Check(list.size() <= max, EncodeError::kFieldTooLong)) {
    return;
  }
  WriteU8(out, static_cast<std::uint8_t>(list.size()));
  for (const Element& element : list) {
    write(out, element);
  }
}

void WriteVec3(Writer& out, const math::Vec3& value, const math::Grid& grid) {
  WriteSteps(out, value.x, grid);
  WriteSteps(out, value.y, grid);
  WriteSteps(out, value.z, grid);
}

// A command's flags take the low four bits of one byte and its stance the two
// above them; the top two are always 0.
constexpr std::uint8_t kCommandFlagsMask = 0x0FU;
constexpr unsigned kCommandStanceShift = 4U;

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

// A body's stance takes the low two bits of its stance byte and its flags the
// one above them; the top five are always 0.
constexpr std::uint8_t kBodyStanceMask = 0x03U;
constexpr std::uint8_t kBodyFlagsMask = BodyStateWire::kExhausted;
constexpr unsigned kBodyFlagsShift = 2U;

void WriteBodyState(Writer& out, const BodyStateWire& body) {
  WriteVec3(out, body.position, math::kPositionGrid);
  WriteVec3(out, body.velocity, math::kVelocityGrid);
  const std::uint8_t stance = out.FromEnum(body.stance, StanceWire::kStanding, StanceWire::kProne);
  const std::uint8_t flags = out.FromFlags(body.flags, kBodyFlagsMask);
  WriteU8(out, static_cast<std::uint8_t>(stance | (flags << kBodyFlagsShift)));
  WriteSteps(out, body.stamina, math::kStaminaGrid);
}

void WriteEntityState(Writer& out, const EntityStateWire& body) {
  WriteU32(out, static_cast<std::uint32_t>(body.entity));
  WriteBodyState(out, body.body);
  WriteSteps(out, body.yaw, math::kAngleGrid);
}

// A rifle's two times travel as their bits, not on a grid: its owner replays
// its commands from them, with the function the server stepped them with, and
// must start from exactly what the server had. Its Recoil offset is kept on the
// angle grid by that function, so its counts are exactly what the server had too.
void WriteWeaponState(Writer& out, const WeaponStateWire& rifle) {
  WriteU8(out, rifle.rounds);
  WriteF32(out, rifle.cooldown);
  WriteF32(out, rifle.reload_remaining);
  WriteU8(out, rifle.burst_index);
  WriteSteps(out, rifle.recoil_pitch, math::kAngleGrid);
  WriteSteps(out, rifle.recoil_yaw, math::kAngleGrid);
}

// Walks a payload front to back. The first problem it meets is remembered and
// every read after it returns a zero value, so a decoder can read all of a
// message's fields and ask once at the end whether they were all there.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::uint8_t ReadU8() {
    if (bytes_.empty()) {
      Fail(DecodeError::kTruncated);
      return 0;
    }
    const auto value = static_cast<std::uint8_t>(bytes_.front());
    bytes_ = bytes_.subspan(1);
    return value;
  }

  std::uint32_t ReadU32() { return ReadUnsigned<std::uint32_t>(); }

  primitives::Tick ReadTick() { return ReadUnsigned<primitives::Tick>(); }

  primitives::Sequence ReadSequence() { return ReadUnsigned<primitives::Sequence>(); }

  float ReadF32() { return std::bit_cast<float>(ReadU32()); }

  // An enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ReadEnum(Enum first, Enum last) {
    return ToEnum(ReadU8(), first, last);
  }

  // A character, by its name in the scenario's manifest (ADR-0042).
  std::string ReadCharacter() { return ReadString(kMaxCharacterNameLength); }

  // value as an enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ToEnum(std::uint8_t value, Enum first, Enum last) {
    if (value < static_cast<std::uint8_t>(first) || value > static_cast<std::uint8_t>(last)) {
      Fail(DecodeError::kInvalidEnum);
      return first;
    }
    return static_cast<Enum>(value);
  }

  // value as flags of which only the bits of mask may be set.
  std::uint8_t ToFlags(std::uint8_t value, std::uint8_t mask) {
    if ((value & ~mask) != 0) {
      Fail(DecodeError::kInvalidEnum);
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

  // The number of elements of a list, at most max_count. It is checked before
  // the caller allocates for them.
  std::size_t ReadCount(std::size_t max_count) {
    const std::size_t count = ReadU8();
    if (count > max_count) {
      Fail(DecodeError::kFieldTooLong);
      return 0;
    }
    return count;
  }

  PackHashWire ReadPackHash() {
    PackHashWire hash{};
    if (bytes_.size() < hash.size()) {
      Fail(DecodeError::kTruncated);
      return hash;
    }
    std::ranges::copy(bytes_.first(hash.size()), hash.begin());
    bytes_ = bytes_.subspan(hash.size());
    return hash;
  }

  std::string ReadString(std::size_t max_length) {
    const std::size_t length = ReadCount(max_length);
    if (bytes_.size() < length) {
      Fail(DecodeError::kTruncated);
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
      Fail(DecodeError::kTruncated);
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

// The read side of WriteList: at most max elements, each as read reads it.
template <typename Read>
auto ReadList(Reader& reader, std::size_t max, Read read) {
  const std::size_t count = reader.ReadCount(max);
  std::vector<decltype(read(reader))> list;
  list.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    list.push_back(read(reader));
  }
  return list;
}

CommandWire ReadCommand(Reader& reader) {
  CommandWire command;
  command.direction = reader.ReadVec3(math::kDirectionGrid);
  command.yaw = reader.ReadSteps(math::kAngleGrid);
  command.pitch = reader.ReadSteps(math::kAngleGrid);
  const std::uint8_t packed = reader.ReadU8();
  command.flags = packed & kCommandFlagsMask;
  command.desired_stance = reader.ToEnum(static_cast<std::uint8_t>(packed >> kCommandStanceShift),
                                         StanceWire::kStanding, StanceWire::kProne);
  command.seen_age = reader.ReadU8();
  command.seen_fraction = reader.ReadSteps(math::kFractionGrid);
  return command;
}

BodyStateWire ReadBodyState(Reader& reader) {
  BodyStateWire body;
  body.position = reader.ReadVec3(math::kPositionGrid);
  body.velocity = reader.ReadVec3(math::kVelocityGrid);
  const std::uint8_t packed = reader.ReadU8();
  body.stance =
      reader.ToEnum(static_cast<std::uint8_t>(packed & kBodyStanceMask), StanceWire::kStanding, StanceWire::kProne);
  body.flags = reader.ToFlags(static_cast<std::uint8_t>(packed >> kBodyFlagsShift), kBodyFlagsMask);
  body.stamina = reader.ReadSteps(math::kStaminaGrid);
  return body;
}

JoinRequestWire ReadJoinRequest(Reader& reader) {
  // Braced initializers evaluate in order: the version, the pack, then the character.
  return JoinRequestWire{
      .engine_version = reader.ReadString(kMaxEngineVersionLength),
      .client_pack = reader.ReadPackHash(),
      .character = reader.ReadCharacter(),
  };
}

EntityStateWire ReadEntityState(Reader& reader) {
  EntityStateWire state;
  state.entity = static_cast<EntityIdWire>(reader.ReadU32());
  state.body = ReadBodyState(reader);
  state.yaw = reader.ReadSteps(math::kAngleGrid);
  return state;
}

RifleWire ReadRifle(Reader& reader) {
  RifleWire rifle;
  rifle.magazine_capacity = reader.ReadU8();
  rifle.rounds_per_minute = reader.ReadF32();
  rifle.muzzle_velocity = reader.ReadF32();
  rifle.reload_seconds = reader.ReadF32();
  rifle.recoil_recovery_per_second = reader.ReadF32();
  rifle.ads_recoil_scale = reader.ReadF32();
  rifle.ads_field_of_view = reader.ReadF32();
  const std::size_t kicks = reader.ReadCount(primitives::kMaxRecoilKicks);
  // Stops at the first missing byte, so a short payload never grows the list.
  for (std::size_t i = 0; i < kicks && !reader.Error(); ++i) {
    RecoilKickWire kick;
    kick.pitch = reader.ReadF32();
    kick.yaw = reader.ReadF32();
    rifle.recoil_pattern.push_back(kick);
  }
  return rifle;
}

AmmoWire ReadAmmo(Reader& reader) {
  AmmoWire ammo;
  ammo.gravity = reader.ReadF32();
  ammo.max_range = reader.ReadF32();
  ammo.head_damage = reader.ReadF32();
  ammo.torso_damage = reader.ReadF32();
  ammo.limb_damage = reader.ReadF32();
  return ammo;
}

ParametersWire ReadParameters(Reader& reader) {
  ParametersWire parameters;
  parameters.player_count = reader.ReadU8();
  parameters.stamina.deplete_per_second = reader.ReadF32();
  parameters.stamina.regen_per_second = reader.ReadF32();
  parameters.stamina.forced_walk_below = reader.ReadF32();
  parameters.rifle = ReadRifle(reader);
  parameters.ammo = ReadAmmo(reader);
  parameters.starting_health = reader.ReadF32();
  return parameters;
}

JoinAcceptedWire ReadJoinAccepted(Reader& reader) {
  JoinAcceptedWire accepted;
  accepted.session = static_cast<SessionIdWire>(reader.ReadU32());
  accepted.tick_rate_hz = reader.ReadU8();
  accepted.parameters = ReadParameters(reader);
  accepted.character = reader.ReadCharacter();
  return accepted;
}

JoinRefusedWire ReadJoinRefused(Reader& reader) {
  return JoinRefusedWire{.reason =
                             reader.ReadEnum(JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kNotAReplayServer)};
}

CommandsWire ReadCommands(Reader& reader) {
  CommandsWire message;
  message.commands = ReadList(reader, primitives::kMaxCommandsPerMessage, [](Reader& sequenced) {
    const primitives::Sequence sequence = sequenced.ReadSequence();
    return SequencedCommandWire{.sequence = sequence, .command = ReadCommand(sequenced)};
  });
  message.seen_tick = reader.ReadTick();
  return message;
}

WeaponStateWire ReadWeaponState(Reader& reader) {
  WeaponStateWire rifle;
  rifle.rounds = reader.ReadU8();
  rifle.cooldown = reader.ReadF32();
  rifle.reload_remaining = reader.ReadF32();
  rifle.burst_index = reader.ReadU8();
  rifle.recoil_pitch = reader.ReadSteps(math::kAngleGrid);
  rifle.recoil_yaw = reader.ReadSteps(math::kAngleGrid);
  return rifle;
}

AuthoritativeStateWire ReadAuthoritativeState(Reader& reader) {
  AuthoritativeStateWire state;
  state.tick = reader.ReadTick();
  state.acknowledged_sequence = reader.ReadSequence();
  state.bodies = ReadList(reader, primitives::kMaxPlayers, ReadEntityState);
  state.queued_commands = reader.ReadU8();
  state.rifle = ReadWeaponState(reader);
  state.health = reader.ReadF32();
  return state;
}

LobbyWire ReadLobby(Reader& reader) {
  LobbyWire lobby;
  lobby.version = reader.ReadU32();
  lobby.roster = ReadList(reader, primitives::kMaxPlayers, [](Reader& entry) {
    const auto session = static_cast<SessionIdWire>(entry.ReadU32());
    return RosterEntryWire{.session = session, .character = entry.ReadCharacter()};
  });
  return lobby;
}

MatchPlayerWire ReadMatchPlayer(Reader& reader) {
  MatchPlayerWire player;
  player.session = static_cast<SessionIdWire>(reader.ReadU32());
  player.entity = static_cast<EntityIdWire>(reader.ReadU32());
  player.character = reader.ReadCharacter();
  player.spawn = reader.ReadVec3(math::kPositionGrid);
  return player;
}

MatchStartWire ReadMatchStart(Reader& reader) {
  MatchStartWire start;
  start.players = ReadList(reader, primitives::kMaxPlayers, ReadMatchPlayer);
  start.first_tick = reader.ReadTick();
  return start;
}

ShotWire ReadShot(Reader& reader) {
  ShotWire shot;
  shot.shooter = static_cast<EntityIdWire>(reader.ReadU32());
  shot.tick = reader.ReadTick();
  shot.origin = reader.ReadVec3(math::kPositionGrid);
  shot.yaw = reader.ReadSteps(math::kAngleGrid);
  shot.pitch = reader.ReadSteps(math::kAngleGrid);
  return shot;
}

HitConfirmationWire ReadHitConfirmation(Reader& reader) {
  HitConfirmationWire hit;
  hit.target = static_cast<EntityIdWire>(reader.ReadU32());
  hit.part = reader.ReadEnum(BodyPartWire::kHead, BodyPartWire::kLimb);
  hit.damage = reader.ReadF32();
  return hit;
}

DeathWire ReadDeath(Reader& reader) {
  DeathWire death;
  death.victim = static_cast<EntityIdWire>(reader.ReadU32());
  death.killer = static_cast<EntityIdWire>(reader.ReadU32());
  death.part = reader.ReadEnum(BodyPartWire::kHead, BodyPartWire::kLimb);
  death.yaw = reader.ReadSteps(math::kAngleGrid);
  death.pitch = reader.ReadSteps(math::kAngleGrid);
  return death;
}

ReplayListingWire ReadReplayListing(Reader& reader) {
  ReplayListingWire listing;
  listing.name = reader.ReadString(kMaxCaptureNameLength);
  listing.started_unix_ms = std::bit_cast<std::int64_t>(reader.ReadTick());
  listing.ticks = reader.ReadU32();
  listing.tick_rate_hz = reader.ReadU8();
  listing.characters =
      ReadList(reader, primitives::kMaxPlayers, [](Reader& character) { return character.ReadCharacter(); });
  return listing;
}

ReplayRequestWire ReadReplayRequest(Reader& reader) {
  // Braced initializers evaluate in order: the version, the pack, then the capture.
  return ReplayRequestWire{
      .engine_version = reader.ReadString(kMaxEngineVersionLength),
      .client_pack = reader.ReadPackHash(),
      .capture = reader.ReadString(kMaxCaptureNameLength),
  };
}

PlayerViewWire ReadPlayerView(Reader& reader) {
  PlayerViewWire view;
  view.entity = static_cast<EntityIdWire>(reader.ReadU32());
  view.pitch = reader.ReadSteps(math::kAngleGrid);
  view.flags = reader.ToFlags(reader.ReadU8(), PlayerViewWire::kAds);
  return view;
}

ReplayViewWire ReadReplayView(Reader& reader) {
  ReplayViewWire view;
  view.tick = reader.ReadTick();
  view.players = ReadList(reader, primitives::kMaxPlayers, ReadPlayerView);
  return view;
}

// nullopt when type is not a message of this protocol.
std::optional<MessageWire> ReadBody(MessageTypeWire type, Reader& reader) {
  switch (type) {
    case MessageTypeWire::kJoinRequest:
      return ReadJoinRequest(reader);
    case MessageTypeWire::kJoinAccepted:
      return ReadJoinAccepted(reader);
    case MessageTypeWire::kJoinRefused:
      return ReadJoinRefused(reader);
    case MessageTypeWire::kCommands:
      return ReadCommands(reader);
    case MessageTypeWire::kAuthoritativeState:
      return ReadAuthoritativeState(reader);
    case MessageTypeWire::kLobby:
      return ReadLobby(reader);
    case MessageTypeWire::kReady:
      return ReadyWire{.version = reader.ReadU32()};
    case MessageTypeWire::kMatchStart:
      return ReadMatchStart(reader);
    case MessageTypeWire::kMatchEnd:
      return MatchEndWire{.winner = static_cast<SessionIdWire>(reader.ReadU32())};
    case MessageTypeWire::kShot:
      return ReadShot(reader);
    case MessageTypeWire::kHitConfirmation:
      return ReadHitConfirmation(reader);
    case MessageTypeWire::kDeath:
      return ReadDeath(reader);
    case MessageTypeWire::kReplayListRequest:
      return ReplayListRequestWire{};
    case MessageTypeWire::kReplayList:
      return ReplayListWire{.replays = ReadList(reader, kMaxReplayListings, ReadReplayListing)};
    case MessageTypeWire::kReplayRequest:
      return ReadReplayRequest(reader);
    case MessageTypeWire::kReplayView:
      return ReadReplayView(reader);
  }
  return std::nullopt;
}

void WriteRifle(Writer& out, const RifleWire& rifle) {
  WriteU8(out, rifle.magazine_capacity);
  WriteF32(out, rifle.rounds_per_minute);
  WriteF32(out, rifle.muzzle_velocity);
  WriteF32(out, rifle.reload_seconds);
  WriteF32(out, rifle.recoil_recovery_per_second);
  WriteF32(out, rifle.ads_recoil_scale);
  WriteF32(out, rifle.ads_field_of_view);
  WriteList(out, rifle.recoil_pattern, primitives::kMaxRecoilKicks, [](Writer& kick_out, const RecoilKickWire& kick) {
    WriteF32(kick_out, kick.pitch);
    WriteF32(kick_out, kick.yaw);
  });
}

void WriteAmmo(Writer& out, const AmmoWire& ammo) {
  WriteF32(out, ammo.gravity);
  WriteF32(out, ammo.max_range);
  WriteF32(out, ammo.head_damage);
  WriteF32(out, ammo.torso_damage);
  WriteF32(out, ammo.limb_damage);
}

void WriteParameters(Writer& out, const ParametersWire& parameters) {
  WriteU8(out, parameters.player_count);
  WriteF32(out, parameters.stamina.deplete_per_second);
  WriteF32(out, parameters.stamina.regen_per_second);
  WriteF32(out, parameters.stamina.forced_walk_below);
  WriteRifle(out, parameters.rifle);
  WriteAmmo(out, parameters.ammo);
  WriteF32(out, parameters.starting_health);
}

void WriteMatchPlayer(Writer& out, const MatchPlayerWire& player) {
  WriteU32(out, static_cast<std::uint32_t>(player.session));
  WriteU32(out, static_cast<std::uint32_t>(player.entity));
  WriteCharacter(out, player.character);
  WriteVec3(out, player.spawn, math::kPositionGrid);
}

void WriteShot(Writer& out, const ShotWire& shot) {
  WriteU32(out, static_cast<std::uint32_t>(shot.shooter));
  WriteTick(out, shot.tick);
  WriteVec3(out, shot.origin, math::kPositionGrid);
  WriteSteps(out, shot.yaw, math::kAngleGrid);
  WriteSteps(out, shot.pitch, math::kAngleGrid);
}

void WriteDeath(Writer& out, const DeathWire& death) {
  WriteU32(out, static_cast<std::uint32_t>(death.victim));
  WriteU32(out, static_cast<std::uint32_t>(death.killer));
  WriteU8(out, out.FromEnum(death.part, BodyPartWire::kHead, BodyPartWire::kLimb));
  WriteSteps(out, death.yaw, math::kAngleGrid);
  WriteSteps(out, death.pitch, math::kAngleGrid);
}

void WriteReplayListing(Writer& out, const ReplayListingWire& listing) {
  WriteString(out, listing.name, kMaxCaptureNameLength);
  WriteTick(out, std::bit_cast<primitives::Tick>(listing.started_unix_ms));
  WriteU32(out, listing.ticks);
  WriteU8(out, listing.tick_rate_hz);
  WriteList(out, listing.characters, primitives::kMaxPlayers,
            [](Writer& character_out, const std::string& character) { WriteCharacter(character_out, character); });
}

void WritePlayerView(Writer& out, const PlayerViewWire& view) {
  WriteU32(out, static_cast<std::uint32_t>(view.entity));
  WriteSteps(out, view.pitch, math::kAngleGrid);
  WriteU8(out, out.FromFlags(view.flags, PlayerViewWire::kAds));
}

// One overload per message: the type tag, then the fields.
struct Encoder {
  Writer& out;

  void operator()(const JoinRequestWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kJoinRequest));
    WriteString(out, message.engine_version, kMaxEngineVersionLength);
    WritePackHash(out, message.client_pack);
    WriteCharacter(out, message.character);
  }

  void operator()(const JoinAcceptedWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kJoinAccepted));
    WriteU32(out, static_cast<std::uint32_t>(message.session));
    WriteU8(out, message.tick_rate_hz);
    WriteParameters(out, message.parameters);
    WriteCharacter(out, message.character);
  }

  void operator()(const JoinRefusedWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kJoinRefused));
    WriteU8(out, out.FromEnum(message.reason, JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kNotAReplayServer));
  }

  void operator()(const CommandsWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kCommands));
    WriteList(out, message.commands, primitives::kMaxCommandsPerMessage,
              [](Writer& command_out, const SequencedCommandWire& sequenced) {
                WriteSequence(command_out, sequenced.sequence);
                WriteCommand(command_out, sequenced.command);
              });
    WriteTick(out, message.seen_tick);
  }

  void operator()(const AuthoritativeStateWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kAuthoritativeState));
    WriteTick(out, message.tick);
    WriteSequence(out, message.acknowledged_sequence);
    WriteList(out, message.bodies, primitives::kMaxPlayers, WriteEntityState);
    WriteU8(out, message.queued_commands);
    WriteWeaponState(out, message.rifle);
    // As its bits, as the Parameters' starting health it counts down from.
    WriteF32(out, message.health);
  }

  void operator()(const LobbyWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kLobby));
    WriteU32(out, message.version);
    WriteList(out, message.roster, primitives::kMaxPlayers, [](Writer& entry_out, const RosterEntryWire& entry) {
      WriteU32(entry_out, static_cast<std::uint32_t>(entry.session));
      WriteCharacter(entry_out, entry.character);
    });
  }

  void operator()(const ReadyWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kReady));
    WriteU32(out, message.version);
  }

  void operator()(const MatchStartWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kMatchStart));
    WriteList(out, message.players, primitives::kMaxPlayers, WriteMatchPlayer);
    WriteTick(out, message.first_tick);
  }

  void operator()(const MatchEndWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kMatchEnd));
    WriteU32(out, static_cast<std::uint32_t>(message.winner));
  }

  void operator()(const ShotWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kShot));
    WriteShot(out, message);
  }

  void operator()(const HitConfirmationWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kHitConfirmation));
    WriteU32(out, static_cast<std::uint32_t>(message.target));
    WriteU8(out, out.FromEnum(message.part, BodyPartWire::kHead, BodyPartWire::kLimb));
    WriteF32(out, message.damage);
  }

  void operator()(const DeathWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kDeath));
    WriteDeath(out, message);
  }

  void operator()(const ReplayListRequestWire& /*message*/) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kReplayListRequest));
  }

  void operator()(const ReplayListWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kReplayList));
    WriteList(out, message.replays, kMaxReplayListings, WriteReplayListing);
  }

  void operator()(const ReplayRequestWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kReplayRequest));
    WriteString(out, message.engine_version, kMaxEngineVersionLength);
    WritePackHash(out, message.client_pack);
    WriteString(out, message.capture, kMaxCaptureNameLength);
  }

  void operator()(const ReplayViewWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kReplayView));
    WriteTick(out, message.tick);
    WriteList(out, message.players, primitives::kMaxPlayers, WritePlayerView);
  }
};

// A recorded hit's body part takes the low two bits of one byte and its flags
// the one above them; the top five are always 0.
constexpr std::uint8_t kHitPartMask = 0x03U;
constexpr std::uint8_t kHitFlagsMask = RecordedHitWire::kReachedZero;
constexpr unsigned kHitFlagsShift = 2U;

constexpr std::uint8_t kTickFlagsMask = RecordedTickWire::kMatchEnded | RecordedTickWire::kPolicyMatchEnd;

void WriteEntityId(Writer& out, EntityIdWire entity) { WriteU32(out, static_cast<std::uint32_t>(entity)); }

EntityIdWire ReadEntityId(Reader& reader) { return static_cast<EntityIdWire>(reader.ReadU32()); }

void WriteRecordedCommand(Writer& out, const RecordedCommandWire& recorded) {
  WriteEntityId(out, recorded.entity);
  WriteTick(out, recorded.seen_tick);
  WriteCommand(out, recorded.command);
}

RecordedCommandWire ReadRecordedCommand(Reader& reader) {
  RecordedCommandWire recorded;
  recorded.entity = ReadEntityId(reader);
  recorded.seen_tick = reader.ReadTick();
  recorded.command = ReadCommand(reader);
  return recorded;
}

void WriteRecordedBody(Writer& out, const RecordedBodyWire& body) {
  WriteEntityState(out, body.state);
  WriteWeaponState(out, body.rifle);
  WriteF32(out, body.health);
}

RecordedBodyWire ReadRecordedBody(Reader& reader) {
  RecordedBodyWire body;
  body.state = ReadEntityState(reader);
  body.rifle = ReadWeaponState(reader);
  body.health = reader.ReadF32();
  return body;
}

void WriteRecordedHit(Writer& out, const RecordedHitWire& hit) {
  WriteEntityId(out, hit.shooter);
  WriteEntityId(out, hit.target);
  const std::uint8_t part = out.FromEnum(hit.part, BodyPartWire::kHead, BodyPartWire::kLimb);
  const std::uint8_t flags = out.FromFlags(hit.flags, kHitFlagsMask);
  WriteU8(out, static_cast<std::uint8_t>(part | (flags << kHitFlagsShift)));
  WriteF32(out, hit.damage);
  WriteF32(out, hit.health);
}

RecordedHitWire ReadRecordedHit(Reader& reader) {
  RecordedHitWire hit;
  hit.shooter = ReadEntityId(reader);
  hit.target = ReadEntityId(reader);
  const std::uint8_t packed = reader.ReadU8();
  hit.part = reader.ToEnum(static_cast<std::uint8_t>(packed & kHitPartMask), BodyPartWire::kHead, BodyPartWire::kLimb);
  hit.flags = reader.ToFlags(static_cast<std::uint8_t>(packed >> kHitFlagsShift), kHitFlagsMask);
  hit.damage = reader.ReadF32();
  hit.health = reader.ReadF32();
  return hit;
}

RecordingHeaderWire ReadRecordingHeader(Reader& reader) {
  RecordingHeaderWire header;
  header.engine_version = reader.ReadString(kMaxEngineVersionLength);
  header.server_pack = reader.ReadPackHash();
  header.tick_rate_hz = reader.ReadU8();
  return header;
}

RecordedTickWire ReadRecordedTick(Reader& reader) {
  RecordedTickWire tick;
  tick.removed = ReadList(reader, primitives::kMaxPlayers, ReadEntityId);
  tick.match_start = ReadList(reader, primitives::kMaxPlayers, ReadMatchPlayer);
  tick.commands = ReadList(reader, primitives::kMaxPlayers, ReadRecordedCommand);
  tick.bodies = ReadList(reader, primitives::kMaxPlayers, ReadRecordedBody);
  tick.shots = ReadList(reader, primitives::kMaxPlayers, ReadShot);
  tick.hits = ReadList(reader, kMaxRecordedHits, ReadRecordedHit);
  tick.deaths = ReadList(reader, primitives::kMaxPlayers, ReadDeath);
  tick.delta_time = reader.ReadF32();
  tick.winner = static_cast<SessionIdWire>(reader.ReadU32());
  tick.flags = reader.ToFlags(reader.ReadU8(), kTickFlagsMask);
  return tick;
}

// nullopt when type is not a record of a recording.
std::optional<RecordWire> ReadRecordBody(RecordTypeWire type, Reader& reader) {
  switch (type) {
    case RecordTypeWire::kHeader:
      return ReadRecordingHeader(reader);
    case RecordTypeWire::kTick:
      return ReadRecordedTick(reader);
  }
  return std::nullopt;
}

// One overload per record: the type tag, then the fields.
struct RecordEncoder {
  Writer& out;

  void operator()(const RecordingHeaderWire& header) const {
    WriteU8(out, static_cast<std::uint8_t>(RecordTypeWire::kHeader));
    WriteString(out, header.engine_version, kMaxEngineVersionLength);
    WritePackHash(out, header.server_pack);
    WriteU8(out, header.tick_rate_hz);
  }

  void operator()(const RecordedTickWire& tick) const {
    WriteU8(out, static_cast<std::uint8_t>(RecordTypeWire::kTick));
    WriteList(out, tick.removed, primitives::kMaxPlayers, WriteEntityId);
    WriteList(out, tick.match_start, primitives::kMaxPlayers, WriteMatchPlayer);
    WriteList(out, tick.commands, primitives::kMaxPlayers, WriteRecordedCommand);
    WriteList(out, tick.bodies, primitives::kMaxPlayers, WriteRecordedBody);
    WriteList(out, tick.shots, primitives::kMaxPlayers, WriteShot);
    WriteList(out, tick.hits, kMaxRecordedHits, WriteRecordedHit);
    WriteList(out, tick.deaths, primitives::kMaxPlayers, WriteDeath);
    WriteF32(out, tick.delta_time);
    WriteU32(out, static_cast<std::uint32_t>(tick.winner));
    WriteU8(out, out.FromFlags(tick.flags, kTickFlagsMask));
  }
};

void WriteSigned32(Writer& out, std::int32_t value) { WriteU32(out, std::bit_cast<std::uint32_t>(value)); }

CaptureHeaderWire ReadCaptureHeader(Reader& reader) {
  CaptureHeaderWire header;
  header.format_version = reader.ReadU8();
  header.engine_version = reader.ReadString(kMaxEngineVersionLength);
  header.server_pack = reader.ReadPackHash();
  header.client_pack = reader.ReadPackHash();
  header.tick_rate_hz = reader.ReadU8();
  header.started_unix_ms = std::bit_cast<std::int64_t>(reader.ReadTick());
  return header;
}

CapturedJoinWire ReadCapturedJoin(Reader& reader) {
  CapturedJoinWire join;
  join.offset = reader.ReadU32();
  join.player = reader.ReadU8();
  join.session = static_cast<SessionIdWire>(reader.ReadU32());
  join.character = reader.ReadCharacter();
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
    WritePackHash(out, header.server_pack);
    WritePackHash(out, header.client_pack);
    WriteU8(out, header.tick_rate_hz);
    WriteTick(out, std::bit_cast<primitives::Tick>(header.started_unix_ms));
  }

  void operator()(const CapturedJoinWire& join) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kJoin));
    WriteU32(out, join.offset);
    WriteU8(out, join.player);
    WriteU32(out, static_cast<std::uint32_t>(join.session));
    WriteCharacter(out, join.character);
    WriteVec3(out, join.spawn, math::kPositionGrid);
  }

  void operator()(const CapturedCommandWire& command) const {
    WriteU8(out, static_cast<std::uint8_t>(CaptureRecordTypeWire::kCommand));
    WriteU32(out, command.offset);
    WriteU8(out, command.player);
    WriteSigned32(out, command.seen_offset);
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

// A payload whose body was read into body by reader, after its type byte: the
// body, unless its type is unknown (nullopt), the read met a problem, or bytes
// remain.
template <typename Payload>
std::expected<Payload, DecodeError> Finish(std::optional<Payload> body, const Reader& reader) {
  if (!body.has_value()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  if (reader.Error().has_value()) {
    return std::unexpected(*reader.Error());
  }
  if (!reader.AtEnd()) {
    return std::unexpected(DecodeError::kTrailingBytes);
  }
  return std::move(*body);
}

}  // namespace

std::expected<BytesWire, EncodeError> EncodeRecord(const RecordWire& record) {
  Writer out;
  std::visit(RecordEncoder{.out = out}, record);
  return std::move(out).Finish();
}

RecordTypeWire TypeOf(const RecordWire& record) {
  struct Type {
    RecordTypeWire operator()(const RecordingHeaderWire& /*wire*/) const { return RecordTypeWire::kHeader; }
    RecordTypeWire operator()(const RecordedTickWire& /*wire*/) const { return RecordTypeWire::kTick; }
  };
  return std::visit(Type{}, record);
}

std::expected<RecordWire, DecodeError> DecodeRecord(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return std::unexpected(DecodeError::kEmpty);
  }
  Reader reader(payload.subspan(1));
  return Finish(ReadRecordBody(static_cast<RecordTypeWire>(payload.front()), reader), reader);
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
    return std::unexpected(DecodeError::kEmpty);
  }
  Reader reader(payload.subspan(1));
  return Finish(ReadCaptureRecordBody(static_cast<CaptureRecordTypeWire>(payload.front()), reader), reader);
}

std::expected<BytesWire, EncodeError> Encode(const MessageWire& message) {
  Writer out;
  std::visit(Encoder{.out = out}, message);
  return std::move(out).Finish();
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
  };
  return std::visit(Type{}, message);
}

std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return std::unexpected(DecodeError::kEmpty);
  }
  Reader reader(payload.subspan(1));
  return Finish(ReadBody(static_cast<MessageTypeWire>(payload.front()), reader), reader);
}

std::string_view DescribeDecodeError(DecodeError error) {
  switch (error) {
    case DecodeError::kEmpty:
      return "empty payload";
    case DecodeError::kUnknownType:
      return "unknown message type";
    case DecodeError::kTruncated:
      return "payload ends before the message does";
    case DecodeError::kTrailingBytes:
      return "bytes remain after the message";
    case DecodeError::kInvalidEnum:
      return "field holds a value its enumeration lacks";
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
  }
  return "unknown encode error";
}

}  // namespace augusta::protocol
