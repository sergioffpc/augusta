#include "augusta/protocol.h"

#include <algorithm>
#include <bit>
#include <cassert>
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

namespace augusta::protocol {

namespace {

constexpr int kBitsPerByte = 8;

void WriteU8(BytesWire& out, std::uint8_t value) { out.push_back(static_cast<std::byte>(value)); }

void WriteU32(BytesWire& out, std::uint32_t value) {
  for (int shift = 0; shift < std::numeric_limits<std::uint32_t>::digits; shift += kBitsPerByte) {
    WriteU8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

void WriteF32(BytesWire& out, float value) { WriteU32(out, std::bit_cast<std::uint32_t>(value)); }

// value as a whole count of grid's step (ADR-0038), in the grid's bytes.
void WriteSteps(BytesWire& out, float value, const math::Grid& grid) {
  const auto bits = static_cast<std::uint32_t>(math::ToSteps(value, grid));
  for (int i = 0; i < grid.bytes; ++i) {
    WriteU8(out, static_cast<std::uint8_t>(bits >> (kBitsPerByte * i)));
  }
}

// A one-byte length and the bytes: the write side of Reader::ReadString.
void WriteString(BytesWire& out, std::string_view text) {
  WriteU8(out, static_cast<std::uint8_t>(text.size()));
  for (const char letter : text) {
    WriteU8(out, static_cast<std::uint8_t>(letter));
  }
}

void WriteVec3(BytesWire& out, const math::Vec3& value, const math::Grid& grid) {
  WriteSteps(out, value.x, grid);
  WriteSteps(out, value.y, grid);
  WriteSteps(out, value.z, grid);
}

// A command's flags take the low four bits of its last byte and its stance the
// two above them; the top two are always 0.
constexpr std::uint8_t kCommandFlagsMask = 0x0FU;
constexpr unsigned kCommandStanceShift = 4U;

void WriteCommand(BytesWire& out, const CommandWire& command) {
  assert((command.flags & ~kCommandFlagsMask) == 0);
  WriteVec3(out, command.direction, math::kDirectionGrid);
  WriteSteps(out, command.yaw, math::kAngleGrid);
  WriteSteps(out, command.pitch, math::kAngleGrid);
  WriteU8(out, static_cast<std::uint8_t>(command.flags |
                                         (static_cast<std::uint8_t>(command.desired_stance) << kCommandStanceShift)));
}

void WriteBodyState(BytesWire& out, const BodyStateWire& body) {
  WriteVec3(out, body.position, math::kPositionGrid);
  WriteVec3(out, body.velocity, math::kVelocityGrid);
  WriteU8(out, static_cast<std::uint8_t>(body.stance));
  WriteSteps(out, body.stamina, math::kStaminaGrid);
}

// The players of an update: a count, then each one.
void WritePlayers(BytesWire& out, const std::vector<PlayerStateWire>& players) {
  assert(players.size() <= kMaxPlayers);
  WriteU8(out, static_cast<std::uint8_t>(players.size()));
  for (const PlayerStateWire& player : players) {
    WriteU32(out, static_cast<std::uint32_t>(player.session));
    WriteBodyState(out, player.body);
  }
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

  std::uint32_t ReadU32() {
    constexpr std::size_t kSize = sizeof(std::uint32_t);
    if (bytes_.size() < kSize) {
      Fail(DecodeError::kTruncated);
      return 0;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < kSize; ++i) {
      value |= static_cast<std::uint32_t>(bytes_[i]) << (kBitsPerByte * i);
    }
    bytes_ = bytes_.subspan(kSize);
    return value;
  }

  float ReadF32() { return std::bit_cast<float>(ReadU32()); }

  // An enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ReadEnum(Enum first, Enum last) {
    return ToEnum(ReadU8(), first, last);
  }

  // A character index: 1-based, so a zeroed byte is never one (ADR-0042).
  std::uint8_t ReadCharacter() {
    const std::uint8_t character = ReadU8();
    if (character == 0) {
      Fail(DecodeError::kInvalidEnum);
    }
    return character;
  }

  // value as an enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ToEnum(std::uint8_t value, Enum first, Enum last) {
    if (value < static_cast<std::uint8_t>(first) || value > static_cast<std::uint8_t>(last)) {
      Fail(DecodeError::kInvalidEnum);
      return first;
    }
    return static_cast<Enum>(value);
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

  std::span<const std::byte> bytes_;
  std::optional<DecodeError> error_;
};

CommandWire ReadCommand(Reader& reader) {
  CommandWire command;
  command.direction = reader.ReadVec3(math::kDirectionGrid);
  command.yaw = reader.ReadSteps(math::kAngleGrid);
  command.pitch = reader.ReadSteps(math::kAngleGrid);
  const std::uint8_t packed = reader.ReadU8();
  command.flags = packed & kCommandFlagsMask;
  command.desired_stance = reader.ToEnum(static_cast<std::uint8_t>(packed >> kCommandStanceShift),
                                         StanceWire::kStanding, StanceWire::kProne);
  return command;
}

BodyStateWire ReadBodyState(Reader& reader) {
  BodyStateWire body;
  body.position = reader.ReadVec3(math::kPositionGrid);
  body.velocity = reader.ReadVec3(math::kVelocityGrid);
  body.stance = reader.ReadEnum(StanceWire::kStanding, StanceWire::kProne);
  body.stamina = reader.ReadSteps(math::kStaminaGrid);
  return body;
}

JoinRequestWire ReadJoinRequest(Reader& reader) {
  // Braced initializers evaluate in order: the version, the pack, then the character.
  return JoinRequestWire{
      .engine_version = reader.ReadString(kMaxEngineVersionLength),
      .client_pack = reader.ReadPackHash(),
      .character = reader.ReadString(kMaxCharacterPathLength),
  };
}

PlayerStateWire ReadPlayerState(Reader& reader) {
  const auto session = static_cast<SessionIdWire>(reader.ReadU32());
  return PlayerStateWire{.session = session, .body = ReadBodyState(reader)};
}

// The players of an update, at most kMaxPlayers.
std::vector<PlayerStateWire> ReadPlayers(Reader& reader) {
  const std::size_t count = reader.ReadCount(kMaxPlayers);
  std::vector<PlayerStateWire> players;
  players.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    players.push_back(ReadPlayerState(reader));
  }
  return players;
}

ParametersWire ReadParameters(Reader& reader) {
  ParametersWire parameters;
  parameters.player_count = reader.ReadU8();
  parameters.stamina.deplete_per_second = reader.ReadF32();
  parameters.stamina.regen_per_second = reader.ReadF32();
  parameters.stamina.forced_walk_below = reader.ReadF32();
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
  return JoinRefusedWire{.reason = reader.ReadEnum(JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kPackMismatch)};
}

CommandsWire ReadCommands(Reader& reader) {
  CommandsWire message;
  const std::size_t count = reader.ReadCount(kMaxCommandsPerMessage);
  message.commands.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint32_t sequence = reader.ReadU32();
    message.commands.push_back(SequencedCommandWire{.sequence = sequence, .command = ReadCommand(reader)});
  }
  return message;
}

AuthoritativeStateWire ReadAuthoritativeState(Reader& reader) {
  AuthoritativeStateWire state;
  state.tick = reader.ReadU32();
  state.acknowledged_sequence = reader.ReadU32();
  state.players = ReadPlayers(reader);
  return state;
}

LobbyWire ReadLobby(Reader& reader) {
  LobbyWire lobby;
  lobby.version = reader.ReadU32();
  const std::size_t count = reader.ReadCount(kMaxPlayers);
  lobby.roster.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto session = static_cast<SessionIdWire>(reader.ReadU32());
    lobby.roster.push_back(RosterEntryWire{.session = session, .character = reader.ReadCharacter()});
  }
  return lobby;
}

MatchStartWire ReadMatchStart(Reader& reader) {
  MatchStartWire start;
  const std::size_t count = reader.ReadCount(kMaxPlayers);
  start.players.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    MatchPlayerWire player;
    player.session = static_cast<SessionIdWire>(reader.ReadU32());
    player.character = reader.ReadCharacter();
    player.spawn = reader.ReadVec3(math::kPositionGrid);
    start.players.push_back(player);
  }
  return start;
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
      return MatchEndWire{};
  }
  return std::nullopt;
}

void WriteParameters(BytesWire& out, const ParametersWire& parameters) {
  WriteU8(out, parameters.player_count);
  WriteF32(out, parameters.stamina.deplete_per_second);
  WriteF32(out, parameters.stamina.regen_per_second);
  WriteF32(out, parameters.stamina.forced_walk_below);
}

// One overload per message: the type tag, then the fields.
struct Encoder {
  BytesWire& out;

  void operator()(const JoinRequestWire& message) const {
    assert(message.engine_version.size() <= kMaxEngineVersionLength);
    assert(message.character.size() <= kMaxCharacterPathLength);
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kJoinRequest));
    WriteString(out, message.engine_version);
    out.insert(out.end(), message.client_pack.begin(), message.client_pack.end());
    WriteString(out, message.character);
  }

  void operator()(const JoinAcceptedWire& message) const {
    assert(message.character != 0);
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kJoinAccepted));
    WriteU32(out, static_cast<std::uint32_t>(message.session));
    WriteU8(out, message.tick_rate_hz);
    WriteParameters(out, message.parameters);
    WriteU8(out, message.character);
  }

  void operator()(const JoinRefusedWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kJoinRefused));
    WriteU8(out, static_cast<std::uint8_t>(message.reason));
  }

  void operator()(const CommandsWire& message) const {
    assert(message.commands.size() <= kMaxCommandsPerMessage);
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kCommands));
    WriteU8(out, static_cast<std::uint8_t>(message.commands.size()));
    for (const SequencedCommandWire& sequenced : message.commands) {
      WriteU32(out, sequenced.sequence);
      WriteCommand(out, sequenced.command);
    }
  }

  void operator()(const AuthoritativeStateWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kAuthoritativeState));
    WriteU32(out, message.tick);
    WriteU32(out, message.acknowledged_sequence);
    WritePlayers(out, message.players);
  }

  void operator()(const LobbyWire& message) const {
    assert(message.roster.size() <= kMaxPlayers);
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kLobby));
    WriteU32(out, message.version);
    WriteU8(out, static_cast<std::uint8_t>(message.roster.size()));
    for (const RosterEntryWire& entry : message.roster) {
      assert(entry.character != 0);
      WriteU32(out, static_cast<std::uint32_t>(entry.session));
      WriteU8(out, entry.character);
    }
  }

  void operator()(const ReadyWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kReady));
    WriteU32(out, message.version);
  }

  void operator()(const MatchStartWire& message) const {
    assert(message.players.size() <= kMaxPlayers);
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kMatchStart));
    WriteU8(out, static_cast<std::uint8_t>(message.players.size()));
    for (const MatchPlayerWire& player : message.players) {
      assert(player.character != 0);
      WriteU32(out, static_cast<std::uint32_t>(player.session));
      WriteU8(out, player.character);
      WriteVec3(out, player.spawn, math::kPositionGrid);
    }
  }

  void operator()(const MatchEndWire& /*message*/) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageTypeWire::kMatchEnd));
  }
};

}  // namespace

BytesWire Encode(const MessageWire& message) {
  BytesWire out;
  std::visit(Encoder{.out = out}, message);
  return out;
}

std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return std::unexpected(DecodeError::kEmpty);
  }
  Reader reader(payload.subspan(1));
  std::optional<MessageWire> message = ReadBody(static_cast<MessageTypeWire>(payload.front()), reader);
  if (!message.has_value()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  if (reader.Error().has_value()) {
    return std::unexpected(*reader.Error());
  }
  if (!reader.AtEnd()) {
    return std::unexpected(DecodeError::kTrailingBytes);
  }
  return std::move(*message);
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

}  // namespace augusta::protocol
