#include "augusta/protocol.h"

#include <bit>
#include <cassert>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace augusta::protocol {

namespace {

constexpr int kBitsPerByte = 8;

void WriteU8(Bytes& out, std::uint8_t value) { out.push_back(static_cast<std::byte>(value)); }

void WriteU32(Bytes& out, std::uint32_t value) {
  for (int shift = 0; shift < std::numeric_limits<std::uint32_t>::digits; shift += kBitsPerByte) {
    WriteU8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

void WriteF32(Bytes& out, float value) { WriteU32(out, std::bit_cast<std::uint32_t>(value)); }

// A one-byte length and the bytes: the write side of Reader::ReadString.
void WriteString(Bytes& out, std::string_view text) {
  WriteU8(out, static_cast<std::uint8_t>(text.size()));
  for (const char letter : text) {
    WriteU8(out, static_cast<std::uint8_t>(letter));
  }
}

void WriteVec3(Bytes& out, const math::Vec3& value) {
  WriteF32(out, value.x);
  WriteF32(out, value.y);
  WriteF32(out, value.z);
}

// A command's flags take the low four bits of its last byte and its stance the
// two above them; the top two are always 0.
constexpr std::uint8_t kCommandFlagsMask = 0x0FU;
constexpr unsigned kCommandStanceShift = 4U;

void WriteCommand(Bytes& out, const CommandWire& command) {
  assert((command.flags & ~kCommandFlagsMask) == 0);
  WriteVec3(out, command.direction);
  WriteF32(out, command.yaw);
  WriteF32(out, command.pitch);
  WriteU8(out, static_cast<std::uint8_t>(command.flags |
                                         (static_cast<std::uint8_t>(command.desired_stance) << kCommandStanceShift)));
}

void WriteBodyState(Bytes& out, const BodyStateWire& body) {
  WriteVec3(out, body.position);
  WriteVec3(out, body.velocity);
  WriteU8(out, static_cast<std::uint8_t>(body.stance));
  WriteF32(out, body.stamina);
}

// The players of a roster or an update: a count, then each one.
void WritePlayers(Bytes& out, const std::vector<PlayerStateWire>& players) {
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

  // value as an enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ToEnum(std::uint8_t value, Enum first, Enum last) {
    if (value < static_cast<std::uint8_t>(first) || value > static_cast<std::uint8_t>(last)) {
      Fail(DecodeError::kInvalidEnum);
      return first;
    }
    return static_cast<Enum>(value);
  }

  math::Vec3 ReadVec3() {
    const float x = ReadF32();
    const float y = ReadF32();
    const float z = ReadF32();
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
  command.direction = reader.ReadVec3();
  command.yaw = reader.ReadF32();
  command.pitch = reader.ReadF32();
  const std::uint8_t packed = reader.ReadU8();
  command.flags = packed & kCommandFlagsMask;
  command.desired_stance = reader.ToEnum(static_cast<std::uint8_t>(packed >> kCommandStanceShift),
                                         StanceWire::kStanding, StanceWire::kProne);
  return command;
}

BodyStateWire ReadBodyState(Reader& reader) {
  BodyStateWire body;
  body.position = reader.ReadVec3();
  body.velocity = reader.ReadVec3();
  body.stance = reader.ReadEnum(StanceWire::kStanding, StanceWire::kProne);
  body.stamina = reader.ReadF32();
  return body;
}

JoinRequest ReadJoinRequest(Reader& reader) {
  // Braced initializers evaluate in order: the version is read before the character.
  return JoinRequest{
      .engine_version = reader.ReadString(kMaxEngineVersionLength),
      .character = reader.ReadString(kMaxCharacterPathLength),
  };
}

PlayerStateWire ReadPlayerState(Reader& reader) {
  const auto session = static_cast<SessionId>(reader.ReadU32());
  return PlayerStateWire{.session = session, .body = ReadBodyState(reader)};
}

// The players of a roster or an update, at most kMaxPlayers.
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

JoinAccepted ReadJoinAccepted(Reader& reader) {
  JoinAccepted accepted;
  accepted.session = static_cast<SessionId>(reader.ReadU32());
  accepted.spawn = reader.ReadVec3();
  accepted.tick_rate_hz = reader.ReadF32();
  accepted.parameters = ReadParameters(reader);
  accepted.roster = ReadPlayers(reader);
  return accepted;
}

JoinRefused ReadJoinRefused(Reader& reader) {
  return JoinRefused{.reason = reader.ReadEnum(JoinRefusal::kVersionMismatch, JoinRefusal::kUnknownCharacter)};
}

Commands ReadCommands(Reader& reader) {
  Commands message;
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

// nullopt when type is not a message of this protocol.
std::optional<Message> ReadBody(MessageType type, Reader& reader) {
  switch (type) {
    case MessageType::kJoinRequest:
      return ReadJoinRequest(reader);
    case MessageType::kJoinAccepted:
      return ReadJoinAccepted(reader);
    case MessageType::kJoinRefused:
      return ReadJoinRefused(reader);
    case MessageType::kCommands:
      return ReadCommands(reader);
    case MessageType::kAuthoritativeState:
      return ReadAuthoritativeState(reader);
  }
  return std::nullopt;
}

void WriteParameters(Bytes& out, const ParametersWire& parameters) {
  WriteU8(out, parameters.player_count);
  WriteF32(out, parameters.stamina.deplete_per_second);
  WriteF32(out, parameters.stamina.regen_per_second);
  WriteF32(out, parameters.stamina.forced_walk_below);
}

// One overload per message: the type tag, then the fields.
struct Encoder {
  Bytes& out;

  void operator()(const JoinRequest& message) const {
    assert(message.engine_version.size() <= kMaxEngineVersionLength);
    assert(message.character.size() <= kMaxCharacterPathLength);
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kJoinRequest));
    WriteString(out, message.engine_version);
    WriteString(out, message.character);
  }

  void operator()(const JoinAccepted& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kJoinAccepted));
    WriteU32(out, static_cast<std::uint32_t>(message.session));
    WriteVec3(out, message.spawn);
    WriteF32(out, message.tick_rate_hz);
    WriteParameters(out, message.parameters);
    WritePlayers(out, message.roster);
  }

  void operator()(const JoinRefused& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kJoinRefused));
    WriteU8(out, static_cast<std::uint8_t>(message.reason));
  }

  void operator()(const Commands& message) const {
    assert(message.commands.size() <= kMaxCommandsPerMessage);
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kCommands));
    WriteU8(out, static_cast<std::uint8_t>(message.commands.size()));
    for (const SequencedCommandWire& sequenced : message.commands) {
      WriteU32(out, sequenced.sequence);
      WriteCommand(out, sequenced.command);
    }
  }

  void operator()(const AuthoritativeStateWire& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kAuthoritativeState));
    WriteU32(out, message.tick);
    WriteU32(out, message.acknowledged_sequence);
    WritePlayers(out, message.players);
  }
};

}  // namespace

Bytes Encode(const Message& message) {
  Bytes out;
  std::visit(Encoder{.out = out}, message);
  return out;
}

std::expected<Message, DecodeError> Decode(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return std::unexpected(DecodeError::kEmpty);
  }
  Reader reader(payload.subspan(1));
  std::optional<Message> message = ReadBody(static_cast<MessageType>(payload.front()), reader);
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

std::string_view DescribeJoinRefusal(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return "client version does not match the server";
    case JoinRefusal::kMatchFull:
      return "match is full";
    case JoinRefusal::kUnknownCharacter:
      return "the server's scenario has no such character";
  }
  return "unknown refusal";
}

}  // namespace augusta::protocol
