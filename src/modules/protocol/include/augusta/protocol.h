#ifndef AUGUSTA_PROTOCOL_H_
#define AUGUSTA_PROTOCOL_H_

#include <array>
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
// travels, and the protocol depends on nothing but augusta_math, which also
// holds the grids its numbers travel on (augusta/grid.h). A type that mirrors one of the engine's
// carries the suffix Wire (BodyStateWire for physics::BodyState,
// AuthoritativeStateWire for harness::AuthoritativeState), so the two never
// read alike where they meet: each peer converts at its edge
// and nowhere else, the server in server/wire.h and the client in
// augusta/harness_wire.h, so no module past that edge (Match, replication,
// harness::Session's API, presentation) names a Wire type.
//
// Every message is one payload: a one-byte MessageTypeWire followed by that
// type's fields, fixed-width and little-endian, with a string or a list as a
// one-byte length and its elements. A position, a velocity, a direction, an
// angle, a stamina or a view's fraction travels as a whole count of its grid's
// step, in the fewest bytes its range needs (augusta/grid.h, which
// physics::World keeps every body on, and weapon::Step a rifle's Recoil
// offset); the other floats (the Parameters, a rifle's times, a hit's damage)
// travel as their IEEE-754 bits.
// Every field takes the smallest type that holds what it says: flags are bits
// of one byte, shared with a small enumeration where one fits. Decode treats
// its input as untrusted: it never throws, never reads past the end, and never
// allocates more than the input itself holds.
//
// The structs order their fields widest first, so none carries padding between
// fields; the order on the wire is the codec's and need not follow it.
// They compare equal field by field, so a message that survives Encode and
// Decode compares equal to itself, whatever fields it gains.
namespace augusta::protocol {

/// The first byte of every payload; which message the rest of it is.
enum class MessageTypeWire : std::uint8_t {
  /// Client to server: asks to join the Lobby.
  kJoinRequest = 1,
  /// Server to client: the join succeeded and the client is in the Lobby.
  kJoinAccepted = 2,
  /// Server to client: the join failed.
  kJoinRefused = 3,
  /// Client to server: the newest movement commands the server has not acknowledged.
  kCommands = 4,
  /// Server to client: every player's body as of one tick.
  kAuthoritativeState = 5,
  /// Server to client: who is in the Lobby, each with their character (ADR-0043).
  kLobby = 6,
  /// Client to server: it has loaded everything it needs for one version of the Lobby's Roster.
  kReady = 7,
  /// Server to client: the match has started, with who is in it and where each spawns.
  kMatchStart = 8,
  /// Server to client: the match is over and its players are back in the Lobby.
  kMatchEnd = 9,
  /// Server to client: a player fired a round (ADR-0044).
  kShot = 10,
  /// Server to client: a round the recipient fired hit a player (ADR-0044).
  kHitConfirmation = 11,
  /// Server to client: a player in the match died (US-13).
  kDeath = 12,
};

/// Longest engine version string a JoinRequestWire may carry, in bytes.
inline constexpr std::size_t kMaxEngineVersionLength = 32;

/// Longest character path a JoinRequestWire may carry, in bytes.
inline constexpr std::size_t kMaxCharacterPathLength = 64;

/// The size of a pack's BLAKE3 hash, in bytes.
inline constexpr std::size_t kPackHashSize = 32;

/// A pack's BLAKE3 hash, the one its trailer signs (ADR-0031): names one cook of it.
using PackHashWire = std::array<std::byte, kPackHashSize>;

/// The players a Lobby or a match holds, and so the most a Lobby, a Match start
/// or an Authoritative State update lists.
inline constexpr std::size_t kMaxPlayers = 8;

/// The most commands one CommandsWire message carries.
inline constexpr std::size_t kMaxCommandsPerMessage = 8;

/// The most kicks a rifle's recoil pattern holds (RifleWire::recoil_pattern).
inline constexpr std::size_t kMaxRecoilKicks = 64;

/// A body's stance.
enum class StanceWire : std::uint8_t {
  kStanding = 0,
  kCrouching = 1,
  kProne = 2,
};

/// Where on a player's body a round struck (CONTEXT.md's Body part). Starts at
/// 1, so a zeroed byte is never one.
enum class BodyPartWire : std::uint8_t {
  kHead = 1,
  kTorso = 2,
  kLimb = 3,
};

/// One player's body as the server simulated it.
struct BodyStateWire {
  /// The bits of flags: the body ran its stamina out and has not yet recovered.
  static constexpr std::uint8_t kExhausted = 1U << 0U;

  math::Vec3 position{};
  math::Vec3 velocity{};
  /// Remaining stamina, 0 to 1.
  float stamina = 1.0F;
  /// Any of kExhausted; no other bit.
  std::uint8_t flags = 0;
  StanceWire stance = StanceWire::kStanding;

  bool operator==(const BodyStateWire&) const = default;
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
  /// How far the player was shown the other players between two server ticks
  /// when the command was sampled (ADR-0044), 0 to 255/256.
  float view_fraction = 0.0F;
  /// Any of kSprint, kAds, kFire and kReload; no other bit.
  std::uint8_t flags = 0;
  StanceWire desired_stance = StanceWire::kStanding;
  /// The first of those two ticks, as how many ticks before its message's
  /// CommandsWire::view_tick it is.
  std::uint8_t view_age = 0;

  bool operator==(const CommandWire&) const = default;
};

/// The stamina rules every player body follows.
struct StaminaWire {
  float deplete_per_second = 0.0F;
  float regen_per_second = 0.0F;
  float forced_walk_below = 0.0F;

  bool operator==(const StaminaWire&) const = default;
};

/// How far one round of a burst turns the aim, in radians.
struct RecoilKickWire {
  float pitch = 0.0F;
  float yaw = 0.0F;

  bool operator==(const RecoilKickWire&) const = default;
};

/// The rifle every player carries.
struct RifleWire {
  float rounds_per_minute = 0.0F;
  float muzzle_velocity = 0.0F;
  float reload_seconds = 0.0F;
  float recoil_recovery_per_second = 0.0F;
  float ads_recoil_scale = 0.0F;
  float ads_field_of_view = 0.0F;
  /// At most kMaxRecoilKicks.
  std::vector<RecoilKickWire> recoil_pattern;
  std::uint8_t magazine_capacity = 0;

  bool operator==(const RifleWire&) const = default;
};

/// The rifle's ammunition, with its damage by body part.
struct AmmoWire {
  float gravity = 0.0F;
  float max_range = 0.0F;
  float head_damage = 0.0F;
  float torso_damage = 0.0F;
  float limb_damage = 0.0F;

  bool operator==(const AmmoWire&) const = default;
};

/// The Parameters (ADR-0039) a client predicts with.
struct ParametersWire {
  StaminaWire stamina{};
  RifleWire rifle{};
  AmmoWire ammo{};
  float starting_health = 0.0F;
  /// How many players a match needs to start (ADR-0043).
  std::uint8_t player_count = 1;

  bool operator==(const ParametersWire&) const = default;
};

/// The server's name for one connected player, distinct from the transport's
/// handle for the connection. Identifies a player inside messages; it is not a
/// credential, since the server tells senders apart by connection.
enum class SessionIdWire : std::uint32_t {};

/// The server's name for one dynamic body - today a player's, later any that
/// moves (a crate, a door). Distinct from the session of the player who
/// controls it, if any: a body is named by what it is, not by who moves it.
enum class EntityIdWire : std::uint32_t {};

/// Why the server refused a join.
enum class JoinRefusalWire : std::uint8_t {
  /// The client's engine version is not the server's.
  kVersionMismatch = 1,
  /// The Lobby already holds the scenario's Player count.
  kLobbyFull = 2,
  /// The character the client asked to play is not one of the scenario's (ADR-0042).
  kUnknownCharacter = 3,
  /// A match is under way, and no one joins one in progress (ADR-0043).
  kMatchInProgress = 4,
  /// The client's pack is not the one cooked with the server's.
  kPackMismatch = 5,
};

/// Client to server: the first message on a new connection.
struct JoinRequestWire {
  /// The client's engine version (augusta::EngineVersion); at most kMaxEngineVersionLength bytes.
  std::string engine_version;
  /// The hash of the client pack the client loaded.
  PackHashWire client_pack{};
  /// The character the player chose, by its path relative to `authoring/`
  /// (e.g. "characters/player", ADR-0042); at most kMaxCharacterPathLength bytes.
  std::string character;

  bool operator==(const JoinRequestWire&) const = default;
};

/// One dynamic body inside an Authoritative State update.
struct EntityStateWire {
  EntityIdWire entity{};
  BodyStateWire body{};
  /// Where the body faces: the yaw of its player's view, in radians.
  float yaw = 0.0F;

  bool operator==(const EntityStateWire&) const = default;
};

/// Server to client: the join succeeded, and the client waits in the Lobby.
struct JoinAcceptedWire {
  /// The session the server assigned to this client.
  SessionIdWire session{};
  /// The rate, in Hz, at which the server simulates and this client must predict:
  /// the server's startup setting, fixed for the life of the server process and
  /// so sent here once and never again.
  std::uint8_t tick_rate_hz = 0;
  /// The parameters the client must predict with, so its numbers (the stamina
  /// rules among them) are the server's.
  ParametersWire parameters{};
  /// The joining player's own character index: its 1-based position in the
  /// scenario's character list (ADR-0042). Never 0.
  std::uint8_t character = 1;

  bool operator==(const JoinAcceptedWire&) const = default;
};

/// Server to client: the join failed and the connection will not be used.
struct JoinRefusedWire {
  JoinRefusalWire reason{};

  bool operator==(const JoinRefusedWire&) const = default;
};

/// One player's rifle between two ticks.
struct WeaponStateWire {
  /// How long, in seconds, until the next round may fire; 0 or less when one may.
  float cooldown = 0.0F;
  /// How long, in seconds, the reload under way still takes; 0 when there is none.
  float reload_remaining = 0.0F;
  /// How far the rifle points off its player's view (CONTEXT.md's Recoil
  /// offset), in radians.
  float recoil_pitch = 0.0F;
  float recoil_yaw = 0.0F;
  /// How many rounds are left in the magazine.
  std::uint8_t rounds = 0;
  /// How many rounds the Burst under way has fired.
  std::uint8_t burst_index = 0;

  bool operator==(const WeaponStateWire&) const = default;
};

/// One tick's command and the number the client gave it. Numbers start at 1 and
/// grow by one per command, so the server can tell what it has already seen.
/// They count one connection's commands and start over on the next, so 32 bits
/// outlast any session: at 60 Hz they would take over two years to wrap.
struct SequencedCommandWire {
  std::uint32_t sequence = 0;
  CommandWire command{};

  bool operator==(const SequencedCommandWire&) const = default;
};

/// Client to server: recent commands, oldest first. Each message repeats the
/// ones the client has not seen acknowledged (at most kMaxCommandsPerMessage,
/// the newest), so one lost datagram does not drop input.
struct CommandsWire {
  std::vector<SequencedCommandWire> commands;
  /// The newest server tick any of the commands was sampled against
  /// (ADR-0044): each says how far before it its own is (CommandWire::view_age).
  std::uint64_t view_tick = 0;

  bool operator==(const CommandsWire&) const = default;
};

/// Server to client: the Authoritative State of one server tick.
struct AuthoritativeStateWire {
  /// The server tick this state is from; a client keeps only the newest it has seen.
  /// Ticks count from the server's start and never start over, so they take 64
  /// bits: 32 would wrap after about 828 days at 60 Hz.
  std::uint64_t tick = 0;
  /// Every dynamic body in the match, at most kMaxPlayers (only players have one so far).
  std::vector<EntityStateWire> bodies;
  /// The recipient's own rifle as of this tick: what it reconciles its
  /// predicted rifle against, as it does its body against its entry in bodies.
  WeaponStateWire rifle{};
  /// The recipient's own health as of this tick, 0 once it has died; no one
  /// else's is ever sent.
  float health = 0.0F;
  /// The highest command sequence of the recipient that the server has processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
  /// How many of the recipient's commands the server still holds queued after
  /// this tick: what the client paces its own ticks by (ADR-0038).
  std::uint8_t queued_commands = 0;

  bool operator==(const AuthoritativeStateWire&) const = default;
};

/// One player in the Lobby.
struct RosterEntryWire {
  SessionIdWire session{};
  /// The player's character index (see JoinAcceptedWire::character). Never 0.
  std::uint8_t character = 1;

  bool operator==(const RosterEntryWire&) const = default;
};

/// Server to client: who is in the Lobby, sent to everyone in it whenever that changes.
struct LobbyWire {
  /// Numbers this Roster: it grows on every join and leave, so a client can say which one it loaded for.
  std::uint32_t version = 0;
  /// Every player in the Lobby, the recipient included, at most kMaxPlayers.
  std::vector<RosterEntryWire> roster;

  bool operator==(const LobbyWire&) const = default;
};

/// Client to server: the client has loaded what it needs to draw everyone in
/// one version of the Lobby's Roster (ADR-0043). The player presses nothing.
struct ReadyWire {
  /// The LobbyWire::version the client loaded for; only the current one counts.
  std::uint32_t version = 0;

  bool operator==(const ReadyWire&) const = default;
};

/// One player in a match, the body it controls, and where the server spawns it.
struct MatchPlayerWire {
  math::Vec3 spawn{};
  SessionIdWire session{};
  /// The body this player's commands move for the whole match.
  EntityIdWire entity{};
  /// The player's character index (see JoinAcceptedWire::character). Never 0.
  std::uint8_t character = 1;

  bool operator==(const MatchPlayerWire&) const = default;
};

/// Server to client: the match has started. From here on its players can only leave.
struct MatchStartWire {
  /// Every player in the match, the recipient included, at most kMaxPlayers.
  std::vector<MatchPlayerWire> players;

  bool operator==(const MatchStartWire&) const = default;
};

/// Server to client: the match is over, and everyone still connected is back in the Lobby.
struct MatchEndWire {
  bool operator==(const MatchEndWire&) const = default;
};

/// Server to client: one round a player in the match fired (CONTEXT.md's Shot,
/// ADR-0044), told to every player in it, the shooter included.
struct ShotWire {
  /// The server tick it was fired on.
  std::uint64_t tick = 0;
  /// Where the round left from.
  math::Vec3 origin{};
  /// The body of the player who fired it.
  EntityIdWire shooter{};
  /// Where it left for, as a view's yaw and pitch, in radians.
  float yaw = 0.0F;
  float pitch = 0.0F;

  bool operator==(const ShotWire&) const = default;
};

/// Server to client: a round the recipient fired hit a player (CONTEXT.md's Hit
/// confirmation, ADR-0044), told to the shooter alone.
struct HitConfirmationWire {
  /// The body that was hit.
  EntityIdWire target{};
  /// The damage the hit did.
  float damage = 0.0F;
  /// Where on the body it struck.
  BodyPartWire part = BodyPartWire::kTorso;

  bool operator==(const HitConfirmationWire&) const = default;
};

/// Server to client: a player in the match died (US-13), told to every player
/// in it, the victim included, with what a ragdoll starts from (ADR-0045).
struct DeathWire {
  /// The body of the player who died.
  EntityIdWire victim{};
  /// The body of the player who fired the killing round.
  EntityIdWire killer{};
  /// Where the killing round was fired for, as a view's yaw and pitch, in radians.
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Where on the victim it struck.
  BodyPartWire part = BodyPartWire::kTorso;

  bool operator==(const DeathWire&) const = default;
};

/// Any message of the protocol.
using MessageWire =
    std::variant<JoinRequestWire, JoinAcceptedWire, JoinRefusedWire, CommandsWire, AuthoritativeStateWire, LobbyWire,
                 ReadyWire, MatchStartWire, MatchEndWire, ShotWire, HitConfirmationWire, DeathWire>;

/// A payload is this many bytes, the same type networking::Payload names.
using BytesWire = std::vector<std::byte>;

/// Why a payload is not a message.
enum class DecodeError : std::uint8_t {
  /// The payload has no bytes, not even a message type.
  kEmpty,
  /// The first byte is not a MessageTypeWire of this protocol.
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
[[nodiscard]] BytesWire Encode(const MessageWire& message);

/// Decodes one payload, or reports what is wrong with it.
[[nodiscard]] std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload);

/// A short lowercase description of error, for logs.
[[nodiscard]] std::string_view DescribeDecodeError(DecodeError error);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_H_
