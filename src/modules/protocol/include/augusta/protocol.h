#ifndef AUGUSTA_PROTOCOL_H_
#define AUGUSTA_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// augusta::protocol is the Networking Protocol (ADR-0007, ADR-0038): the
// messages client and server exchange and their custom binary encoding. It is
// a pure codec - bytes in, a message or a typed error out, bytes out - with no
// socket, no clock and no state, so it is tested without a network and shared
// by both sides (ADR-0006). Which peer may send what, and what a message
// means for the match, is the receiver's business.
//
// Every message is one payload: a one-byte MessageType followed by that
// type's fields, fixed-width and little-endian, with a string as a one-byte
// length and its bytes. Decode treats its input as untrusted: it never throws,
// never reads past the end, and never allocates more than the input itself
// holds.
namespace augusta::protocol {

/// The first byte of every payload; which message the rest of it is.
enum class MessageType : std::uint8_t {
  /// Client to server: asks to join the match.
  kJoinRequest = 1,
  /// Server to client: the join succeeded.
  kJoinAccepted = 2,
  /// Server to client: the join failed.
  kJoinRefused = 3,
};

/// Longest engine version string a JoinRequest may carry, in bytes.
inline constexpr std::size_t kMaxEngineVersionLength = 32;

/// The server's name for one connected player, distinct from the transport's
/// handle for the connection. Identifies a player inside messages; it is not a
/// credential, since the server tells senders apart by connection.
enum class SessionId : std::uint32_t {};

/// Why the server refused a join.
enum class JoinRefusal : std::uint8_t {
  /// The client's engine version is not the server's.
  kVersionMismatch = 1,
  /// The match already holds as many players as it supports.
  kMatchFull = 2,
};

/// Client to server: the first message on a new connection.
struct JoinRequest {
  /// The client's engine version (augusta::EngineVersion); at most kMaxEngineVersionLength bytes.
  std::string engine_version;
};

/// Server to client: the join succeeded.
struct JoinAccepted {
  /// The session the server assigned to this client.
  SessionId session{};
};

/// Server to client: the join failed and the connection will not be used.
struct JoinRefused {
  JoinRefusal reason{};
};

/// Any message of the protocol.
using Message = std::variant<JoinRequest, JoinAccepted, JoinRefused>;

/// A payload is this many bytes, the same type networking::Payload names.
using Bytes = std::vector<std::byte>;

/// Why a payload is not a message.
enum class DecodeError : std::uint8_t {
  /// The payload has no bytes, not even a message type.
  kEmpty,
  /// The first byte is not a MessageType of this protocol.
  kUnknownType,
  /// The payload ends before the message's fields do.
  kTruncated,
  /// The message is complete and bytes remain.
  kTrailingBytes,
  /// An enumerated field holds a value the enumeration does not have.
  kInvalidEnum,
  /// A string field is longer than the protocol allows.
  kFieldTooLong,
};

/// Encodes message as one payload. A JoinRequest whose engine version exceeds
/// kMaxEngineVersionLength is a caller bug, not an input.
[[nodiscard]] Bytes Encode(const Message& message);

/// Decodes one payload, or reports what is wrong with it.
[[nodiscard]] std::expected<Message, DecodeError> Decode(std::span<const std::byte> payload);

/// A short lowercase description of error, for logs.
[[nodiscard]] std::string_view DescribeDecodeError(DecodeError error);

/// A short lowercase description of reason, for logs and for the player.
[[nodiscard]] std::string_view DescribeJoinRefusal(JoinRefusal reason);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_H_
