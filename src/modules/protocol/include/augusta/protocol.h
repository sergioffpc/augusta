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

#include "augusta/math.h"

// augusta::protocol is the Networking Protocol (ADR-0007, ADR-0038): the
// messages client and server exchange and their custom binary encoding. It is
// a pure codec - bytes in, a message or a typed error out, bytes out - with no
// socket, no clock and no state, so it is tested without a network and shared
// by both sides (ADR-0006). Which peer may send what, and what a message
// means for the match, is the receiver's business.
//
// Its messages hold only plain types of its own and the math types, never
// another module's structs: a module changing its structs never changes what
// travels, and the protocol depends on nothing but augusta_math. A type that
// mirrors one of the engine's carries the suffix Wire (BodyStateWire for
// physics::BodyState, AuthoritativeStateWire for harness::AuthoritativeState),
// so the two never read alike where they meet: each peer converts at its edge,
// the server in server::Host and the client in harness::Session.
//
// Every message is one payload: a one-byte MessageType followed by that
// type's fields, fixed-width and little-endian, with a string or a list as a
// one-byte length and its elements. A position, a velocity, a direction, an
// angle or a stamina travels as a whole count of its grid's step, in the fewest
// bytes its range needs (see the Snap functions); the other floats travel as
// their IEEE-754 bits.
// Every field takes the smallest type that holds what it says: flags are bits
// of one byte, shared with a small enumeration where one fits. Decode treats
// its input as untrusted: it never throws, never reads past the end, and never
// allocates more than the input itself holds.
//
// The structs order their fields widest first, so none carries padding between
// fields; the order on the wire is the codec's and need not follow it.
namespace augusta::protocol {

/// The first byte of every payload; which message the rest of it is.
enum class MessageType : std::uint8_t {
  /// Client to server: asks to join the match.
  kJoinRequest = 1,
  /// Server to client: the join succeeded.
  kJoinAccepted = 2,
  /// Server to client: the join failed.
  kJoinRefused = 3,
  /// Client to server: the newest movement commands the server has not acknowledged.
  kCommands = 4,
  /// Server to client: every player's body as of one tick.
  kAuthoritativeState = 5,
};

/// Longest engine version string a JoinRequest may carry, in bytes.
inline constexpr std::size_t kMaxEngineVersionLength = 32;

/// Longest character path a JoinRequest may carry, in bytes.
inline constexpr std::size_t kMaxCharacterPathLength = 64;

/// The players a match holds, and so the most an Authoritative State update lists.
inline constexpr std::size_t kMaxPlayers = 8;

/// The most commands one Commands message carries.
inline constexpr std::size_t kMaxCommandsPerMessage = 8;

/// A body's stance.
enum class StanceWire : std::uint8_t {
  kStanding = 0,
  kCrouching = 1,
  kProne = 2,
};

/// One player's body as the server simulated it.
struct BodyStateWire {
  math::Vec3 position{};
  math::Vec3 velocity{};
  /// Remaining stamina, 0 to 1.
  float stamina = 1.0F;
  StanceWire stance = StanceWire::kStanding;
};

/// What a player asked to do for one tick.
struct CommandWire {
  /// The bits of flags: sprint, aim down sights and fire held this tick, and
  /// reload pressed on it.
  static constexpr std::uint8_t kSprint = 1U << 0U;
  static constexpr std::uint8_t kAds = 1U << 1U;
  static constexpr std::uint8_t kFire = 1U << 2U;
  static constexpr std::uint8_t kReload = 1U << 3U;

  /// The desired movement direction, in world space; not necessarily unit length.
  math::Vec3 direction{};
  /// The view, in radians.
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Any of kSprint, kAds, kFire and kReload; no other bit.
  std::uint8_t flags = 0;
  StanceWire desired_stance = StanceWire::kStanding;
};

/// The stamina rules every player body follows.
struct StaminaWire {
  float deplete_per_second = 0.0F;
  float regen_per_second = 0.0F;
  float forced_walk_below = 0.0F;
};

/// The Parameters (ADR-0039) a client predicts with.
struct ParametersWire {
  StaminaWire stamina{};
  /// How many players a match needs to start (ADR-0043).
  std::uint8_t player_count = 1;
};

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
  /// The character the client asked to play is not one of the scenario's (ADR-0042).
  kUnknownCharacter = 3,
};

/// Client to server: the first message on a new connection.
struct JoinRequest {
  /// The client's engine version (augusta::EngineVersion); at most kMaxEngineVersionLength bytes.
  std::string engine_version;
  /// The character the player chose, by its path relative to `authoring/`
  /// (e.g. "characters/player", ADR-0042); at most kMaxCharacterPathLength bytes.
  std::string character;
};

/// One player's body inside an Authoritative State update or a roster.
struct PlayerStateWire {
  SessionId session{};
  BodyStateWire body{};
};

/// Server to client: the join succeeded.
struct JoinAccepted {
  /// The session the server assigned to this client.
  SessionId session{};
  /// Where the server spawned this client's player.
  math::Vec3 spawn{};
  /// The rate, in Hz, at which the server simulates and this client must predict:
  /// the server's startup setting, fixed for the life of the server process and
  /// so sent here once and never again.
  float tick_rate_hz = 0.0F;
  /// The parameters the client must predict with, so its numbers (the stamina
  /// rules among them) are the server's.
  ParametersWire parameters{};
  /// The players already in the match, at most kMaxPlayers, each where the
  /// server last had it. Not the joining client's own.
  std::vector<PlayerStateWire> roster;
};

/// Server to client: the join failed and the connection will not be used.
struct JoinRefused {
  JoinRefusal reason{};
};

/// One tick's command and the number the client gave it. Numbers start at 1 and
/// grow by one per command, so the server can tell what it has already seen.
struct SequencedCommandWire {
  std::uint32_t sequence = 0;
  CommandWire command{};
};

/// Client to server: recent commands, oldest first. Each message repeats the
/// ones the client has not seen acknowledged (at most kMaxCommandsPerMessage,
/// the newest), so one lost datagram does not drop input.
struct Commands {
  std::vector<SequencedCommandWire> commands;
};

/// Server to client: the Authoritative State of one server tick.
struct AuthoritativeStateWire {
  /// The server tick this state is from; a client keeps only the newest it has seen.
  std::uint32_t tick = 0;
  /// The highest command sequence of the recipient that the server has processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
  /// Every player in the match, at most kMaxPlayers.
  std::vector<PlayerStateWire> players;
};

/// Any message of the protocol.
using Message = std::variant<JoinRequest, JoinAccepted, JoinRefused, Commands, AuthoritativeStateWire>;

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
  /// A string or list field is longer than the protocol allows.
  kFieldTooLong,
};

/// Encodes message as one payload. A field beyond its limit (an engine version
/// over kMaxEngineVersionLength, more than kMaxCommandsPerMessage commands,
/// more than kMaxPlayers players) is a caller bug, not an input.
[[nodiscard]] Bytes Encode(const Message& message);

/// position as Decode gives it back once Encode has sent it: on a grid of
/// 1/1024 m (about a millimeter), within 8192 m of the origin on each axis. A
/// value beyond the range is the bound and a NaN is 0, here and in the Snap
/// functions below. A body lives on these grids (physics::World), so what a peer
/// is told is exactly what the sender has.
[[nodiscard]] math::Vec3 SnapPosition(const math::Vec3& position);

/// velocity as Decode gives it back: on a grid of 1/512 m/s, within 64 m/s on each axis.
[[nodiscard]] math::Vec3 SnapVelocity(const math::Vec3& velocity);

/// A movement direction as Decode gives it back: on a grid of 1/16384, within 2 on each axis.
[[nodiscard]] math::Vec3 SnapDirection(const math::Vec3& direction);

/// An angle (a yaw or a pitch) as Decode gives it back: on a grid of 1/8192 rad, within 4 rad.
[[nodiscard]] float SnapAngle(float radians);

/// A stamina as Decode gives it back: on a grid of 1/32768, from 0 to 2.
[[nodiscard]] float SnapStamina(float stamina);

/// Decodes one payload, or reports what is wrong with it.
[[nodiscard]] std::expected<Message, DecodeError> Decode(std::span<const std::byte> payload);

/// A short lowercase description of error, for logs.
[[nodiscard]] std::string_view DescribeDecodeError(DecodeError error);

/// A short lowercase description of reason, for logs and for the player.
[[nodiscard]] std::string_view DescribeJoinRefusal(JoinRefusal reason);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_H_
