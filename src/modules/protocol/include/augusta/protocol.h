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
#include "augusta/primitives.h"

/// \file
/// augusta::protocol is the Networking Protocol (ADR-0007, ADR-0038): the
/// messages client and server exchange and their custom binary encoding. It is
/// a pure codec - bytes in, a message or a typed error out, bytes out - with no
/// socket, no clock and no state, so it is tested without a network and shared
/// by both sides (ADR-0006). Which peer may send what, and what a message
/// means for the match, is the receiver's business.
///
/// Its messages hold only plain types of its own and the math types, never
/// another module's structs: a module changing its structs never changes what
/// travels. The protocol depends on nothing but augusta_math, which also holds
/// the grids its numbers travel on (augusta/grid.h), and augusta_primitives,
/// whose Tick and Sequence widths its counters take and whose player, command
/// and recoil bounds (primitives::kMaxPlayers and the rest) bound its lists;
/// it neither owns nor re-exports them (tests/protocol_boundary.cmake,
/// tests/core_boundary.cmake). A type that mirrors one of the engine's
/// carries the suffix Wire (BodyStateWire for physics::BodyState,
/// AuthoritativeStateWire for harness::AuthoritativeState), so the two never
/// read alike where they meet: each peer converts at its edge
/// and nowhere else, the server in server/wire.h and the client in
/// augusta/harness_wire.h, so no module past that edge (Match, replication,
/// harness::Session's API, presentation) names a Wire type.
///
/// Every message is one payload: a one-byte MessageTypeWire followed by that
/// type's fields, fixed-width and little-endian, with a string or a list as a
/// one-byte length and its elements. A position, a velocity, a direction, an
/// angle, a stamina or a Seen time's fraction travels as a whole count of its grid's
/// step, in the fewest bytes its range needs (augusta/grid.h, which
/// physics::World keeps every body on, and weapon::Step a rifle's Recoil
/// offset); the other floats (the Parameters, a rifle's times, a hit's damage)
/// travel as their IEEE-754 bits.
/// The server's Match captures (ADR-0050) are written in the same encoding,
/// as records of their own (CaptureRecordWire), so a capture carries a command
/// exactly as a Commands message does.
/// A client message carries intent, never an outcome: tests/impossible_actions.md
/// (US-15, NFR-05) lists what bounds each of its fields. A new one needs a line
/// there, and if it carries an outcome (a position, a hit, an ammo count), a
/// check at the server's boundary and a test.
/// Every field takes the smallest type that holds what it says: flags are bits
/// of one byte, shared with a small enumeration where one fits. Decode treats
/// its input as untrusted: it never throws, never reads past the end, and never
/// allocates more than the input itself holds.
///
/// The structs order their fields widest first, so none carries padding between
/// fields; the order on the wire is the codec's and need not follow it.
/// They compare equal field by field, so a message that survives Encode and
/// Decode compares equal to itself, whatever fields it gains.
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
  /// Server to client: the match is over, with its winner or a draw, and its players are back in the Lobby.
  kMatchEnd = 9,
  /// Server to client: a player fired a round (ADR-0044).
  kShot = 10,
  /// Server to client: a round the recipient fired hit a player (ADR-0044).
  kHitConfirmation = 11,
  /// Server to client: a player in the match died (US-13).
  kDeath = 12,
  /// Client to server: asks a replay server which Match captures it replays (ADR-0051).
  kReplayListRequest = 13,
  /// Server to client: the Match captures a replay server replays, after which it closes the connection.
  kReplayList = 14,
  /// Client to server: asks a replay server to replay one Match capture to this client (ADR-0051).
  kReplayRequest = 15,
  /// Server to client: every player's pitch and ADS on one tick of a Replay, to its Replay viewer alone.
  kReplayView = 16,
  /// Client to server: asks to join the Lobby as a Captured player, naming its spawn (ADR-0050).
  kReenactRequest = 17,
};

/// Longest engine version string a JoinRequestWire may carry, in bytes.
inline constexpr std::size_t kMaxEngineVersionLength = 32;

/// Longest character name a message may carry, in bytes.
inline constexpr std::size_t kMaxCharacterNameLength = 64;

/// The size of a pack's BLAKE3 hash, in bytes.
inline constexpr std::size_t kPackHashSize = 32;

/// A pack's BLAKE3 hash, the one its trailer signs (ADR-0031): names one cook of it.
using PackHashWire = std::array<std::byte, kPackHashSize>;

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
  /// The fraction of the command's Seen time: how far the player was shown the
  /// other players between two server ticks (ADR-0044), 0 to 255/256.
  float seen_fraction = 0.0F;
  /// Any of kSprint, kAds, kFire and kReload; no other bit.
  std::uint8_t flags = 0;
  StanceWire desired_stance = StanceWire::kStanding;
  /// The tick of the command's Seen time, as how many ticks before its message's
  /// CommandsWire::seen_tick it is.
  std::uint8_t seen_age = 0;

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
  /// At most primitives::kMaxRecoilKicks.
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

/// The session a Match end names as its winner when it is a draw: Session IDs
/// start at 1, so none is ever 0 (ADR-0038).
inline constexpr SessionIdWire kDraw{};

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
  /// The server is a replay server: it takes Replay requests only (ADR-0051).
  kReplayServer = 6,
  /// The capture a Replay request names is none the replay server replays (ADR-0051).
  kUnknownCapture = 7,
  /// A Replay list request or a Replay request reached a live server, which replays nothing (ADR-0051).
  kNotAReplayServer = 8,
  /// A Reenact request reached a server that does not take them (ADR-0050).
  kReenactmentsNotAccepted = 9,
};

/// Client to server: the first message on a new connection.
struct JoinRequestWire {
  /// The client's engine version (augusta::EngineVersion); at most kMaxEngineVersionLength bytes.
  std::string engine_version;
  /// The hash of the client pack the client loaded.
  PackHashWire client_pack{};
  /// The character the player chose, by its name in the scenario's manifest
  /// (e.g. "soldier", ADR-0042); at most kMaxCharacterNameLength bytes.
  std::string character;

  bool operator==(const JoinRequestWire&) const = default;
};

/// Client to server: a Captured player's first message on a new connection, in
/// place of a JoinRequestWire (ADR-0050): its fields, checked the same way,
/// and where the capture spawned the player, which the server places it at.
struct ReenactRequestWire {
  /// As JoinRequestWire::engine_version.
  std::string engine_version;
  /// As JoinRequestWire::client_pack.
  PackHashWire client_pack{};
  /// As JoinRequestWire::character.
  std::string character;
  /// Where the capture's Match start spawned the player, on the position grid.
  math::Vec3 spawn{};

  bool operator==(const ReenactRequestWire&) const = default;
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
  /// The joining player's own character, by its name in the scenario's manifest
  /// (see JoinRequestWire::character); at most kMaxCharacterNameLength bytes.
  std::string character;

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
/// They count one connection's commands and start over on the next, in
/// primitives::Sequence's width, which never wraps.
struct SequencedCommandWire {
  primitives::Sequence sequence = 0;
  CommandWire command{};

  bool operator==(const SequencedCommandWire&) const = default;
};

/// Client to server: recent commands, oldest first. Each message repeats the
/// ones the client has not seen acknowledged (at most primitives::kMaxCommandsPerMessage,
/// the newest), so one lost datagram does not drop input.
struct CommandsWire {
  std::vector<SequencedCommandWire> commands;
  /// The newest tick of its commands' Seen times (ADR-0044): each says how far
  /// before it its own is (CommandWire::seen_age).
  primitives::Tick seen_tick = 0;

  bool operator==(const CommandsWire&) const = default;
};

/// Server to client: the Authoritative State of one server tick.
struct AuthoritativeStateWire {
  /// The server tick this state is from; a client keeps only the newest it has seen.
  /// Ticks count from the server's start and never start over, so they take 64
  /// bits: 32 would wrap after about 828 days at 60 Hz.
  primitives::Tick tick = 0;
  /// Every dynamic body in the match, at most primitives::kMaxPlayers (only players have one so far).
  std::vector<EntityStateWire> bodies;
  /// The recipient's own rifle as of this tick: what it reconciles its
  /// predicted rifle against, as it does its body against its entry in bodies.
  WeaponStateWire rifle{};
  /// The recipient's own health as of this tick, 0 once it has died; no one
  /// else's is ever sent.
  float health = 0.0F;
  /// The highest command sequence of the recipient that the server has processed, 0 if none.
  primitives::Sequence acknowledged_sequence = 0;
  /// How many of the recipient's commands the server still holds queued after
  /// this tick: what the client paces its own ticks by (ADR-0038).
  std::uint8_t queued_commands = 0;

  bool operator==(const AuthoritativeStateWire&) const = default;
};

/// One player in the Lobby.
struct RosterEntryWire {
  SessionIdWire session{};
  /// The player's character (see JoinAcceptedWire::character).
  std::string character;

  bool operator==(const RosterEntryWire&) const = default;
};

/// Server to client: who is in the Lobby, sent to everyone in it whenever that changes.
struct LobbyWire {
  /// Numbers this Roster: it grows on every join and leave, so a client can say which one it loaded for.
  std::uint32_t version = 0;
  /// Every player in the Lobby, the recipient included, at most primitives::kMaxPlayers.
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
  /// The player's character (see JoinAcceptedWire::character).
  std::string character;

  bool operator==(const MatchPlayerWire&) const = default;
};

/// Server to client: the match has started. From here on its players can only leave.
struct MatchStartWire {
  /// Every player in the match, the recipient included, at most primitives::kMaxPlayers.
  std::vector<MatchPlayerWire> players;
  /// The Match's first server tick: what a Match capture's offsets count from (ADR-0050).
  primitives::Tick first_tick = 0;

  bool operator==(const MatchStartWire&) const = default;
};

/// Server to client: the match is over, and everyone still connected is back in the Lobby.
struct MatchEndWire {
  /// The session of the player Game policy declared the winner, or kDraw.
  SessionIdWire winner = kDraw;

  bool operator==(const MatchEndWire&) const = default;
};

/// Server to client: one round a player in the match fired (CONTEXT.md's Shot,
/// ADR-0044), told to every player in it, the shooter included.
struct ShotWire {
  /// The server tick it was fired on.
  primitives::Tick tick = 0;
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

/// Longest Match capture name a Replay message may carry, in bytes: a
/// capture's file name (server::CaptureFileName's) is about half of it.
inline constexpr std::size_t kMaxCaptureNameLength = 64;

/// The most captures a Replay list names: as many as its one-byte count holds.
inline constexpr std::size_t kMaxReplayListings = 255;

/// Client to server: the first message on a connection that asks a replay
/// server which captures it replays, instead of a Join request. It has no fields.
struct ReplayListRequestWire {
  bool operator==(const ReplayListRequestWire&) const = default;
};

/// One Match capture a replay server replays.
struct ReplayListingWire {
  /// When its Match started, in milliseconds since the Unix epoch, UTC.
  std::int64_t started_unix_ms = 0;
  /// Its players' Characters, in its Join order, at most primitives::kMaxPlayers,
  /// each at most kMaxCharacterNameLength bytes.
  std::vector<std::string> characters;
  /// Its file name, which a Replay request names it by; at most kMaxCaptureNameLength bytes.
  std::string name;
  /// How long its Match lasted, in ticks at tick_rate_hz.
  std::uint32_t ticks = 0;
  std::uint8_t tick_rate_hz = 0;

  bool operator==(const ReplayListingWire&) const = default;
};

/// Server to client: every capture the replay server replays, at most
/// kMaxReplayListings, answering a Replay list request; the server then closes
/// the connection.
struct ReplayListWire {
  std::vector<ReplayListingWire> replays;

  bool operator==(const ReplayListWire&) const = default;
};

/// Client to server: the first message on a connection that watches a Replay,
/// instead of a Join request (ADR-0051).
struct ReplayRequestWire {
  /// As JoinRequestWire::engine_version.
  std::string engine_version;
  /// As JoinRequestWire::client_pack.
  PackHashWire client_pack{};
  /// The capture to replay, by its name in the Replay list; at most
  /// kMaxCaptureNameLength bytes. Never a path: the server matches it against
  /// its listing.
  std::string capture;

  bool operator==(const ReplayRequestWire&) const = default;
};

/// Where one player of a Replay looked on a tick, from the Command the World ran.
struct PlayerViewWire {
  /// The bits of flags: the player held ADS.
  static constexpr std::uint8_t kAds = 1U << 0U;

  /// The view's pitch, in radians, on the angle grid.
  float pitch = 0.0F;
  /// The player's body.
  EntityIdWire entity{};
  /// kAds or not; no other bit.
  std::uint8_t flags = 0;

  bool operator==(const PlayerViewWire&) const = default;
};

/// Server to client, unreliably and to a Replay viewer alone: what each player
/// saw on one tick of its Replay, which no Authoritative State carries.
struct ReplayViewWire {
  /// The tick, as the Authoritative State of the same tick names it.
  primitives::Tick tick = 0;
  /// Every player handed a Command on the tick, at most primitives::kMaxPlayers.
  std::vector<PlayerViewWire> players;

  bool operator==(const ReplayViewWire&) const = default;
};

/// Any message of the protocol.
using MessageWire =
    std::variant<JoinRequestWire, JoinAcceptedWire, JoinRefusedWire, CommandsWire, AuthoritativeStateWire, LobbyWire,
                 ReadyWire, MatchStartWire, MatchEndWire, ShotWire, HitConfirmationWire, DeathWire,
                 ReplayListRequestWire, ReplayListWire, ReplayRequestWire, ReplayViewWire, ReenactRequestWire>;

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

/// Why a message or record is not encoded: a field holds what the protocol
/// cannot carry. Always the sender's bug, never a peer's input, so its
/// boundary treats it as a broken invariant (ADR-0033).
enum class EncodeError : std::uint8_t {
  /// A string or list field is longer than the protocol allows.
  kFieldTooLong = 1,
  /// A flags field has a bit set that is not one of its flags.
  kReservedBits = 2,
  /// An enumerated field holds a value the enumeration does not have.
  kInvalidEnum = 3,
};

/// Encodes message as one payload, or reports the first field beyond its
/// limit (an engine version over kMaxEngineVersionLength, more than
/// primitives::kMaxCommandsPerMessage commands, a flag bit or stance a field lacks) and
/// gives no payload at all: checked in every build, so a payload is either
/// whole and decodable or not made.
[[nodiscard]] std::expected<BytesWire, EncodeError> Encode(const MessageWire& message);

/// The type message's payload starts with.
[[nodiscard]] MessageTypeWire TypeOf(const MessageWire& message);

/// Decodes one payload, or reports what is wrong with it.
[[nodiscard]] std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload);

/// A short lowercase description of error, for logs.
[[nodiscard]] std::string_view DescribeDecodeError(DecodeError error);

/// A short lowercase description of error, for logs.
[[nodiscard]] std::string_view DescribeEncodeError(EncodeError error);

// A Match capture (ADR-0050): one Match's client actions and markers, from its
// Match start to its Match end, each at its offset in ticks from the Match's
// first tick, in this protocol's encoding. Not a message: Decode never yields
// one, nor DecodeCaptureRecord a message. A record is one payload as a message
// is, a one-byte CaptureRecordTypeWire followed by its fields, read under the
// same untrusted-input rules. A player is named by its number in the capture,
// from 1, in its Join's order.

/// The bytes a capture file starts with, before its first record, so a reader
/// tells it from a pack (ADR-0031) by them alone.
inline constexpr std::array<std::byte, 8> kCaptureMagic{std::byte{'A'},  std::byte{'U'}, std::byte{'G'},
                                                        std::byte{'C'},  std::byte{'A'}, std::byte{'P'},
                                                        std::byte{'\r'}, std::byte{'\n'}};

/// The capture format this engine writes, in CaptureHeaderWire::format_version.
inline constexpr std::uint8_t kCaptureFormatVersion = 2;

/// The first byte of every record of a capture.
enum class CaptureRecordTypeWire : std::uint8_t {
  /// What the Match ran on: a capture's first record, and its only one of this type.
  kHeader = 1,
  kJoin = 2,
  kCommand = 3,
  kLeave = 4,
  kDeath = 5,
  /// A capture's last record.
  kMatchEnd = 6,
};

/// What a captured Match ran on and when it started.
struct CaptureHeaderWire {
  /// The hash of the server pack the Match ran on (ADR-0031).
  PackHashWire server_pack{};
  /// The hash of the client pack its players joined with.
  PackHashWire client_pack{};
  /// The capturing engine's version (augusta::EngineVersion); at most kMaxEngineVersionLength bytes.
  std::string engine_version;
  /// When the Match started, in milliseconds since the Unix epoch, UTC.
  std::int64_t started_unix_ms = 0;
  /// kCaptureFormatVersion of the engine that wrote it.
  std::uint8_t format_version = 0;
  std::uint8_t tick_rate_hz = 0;

  bool operator==(const CaptureHeaderWire&) const = default;
};

/// A player of the Match start, in its order: always at offset 0.
struct CapturedJoinWire {
  /// Where the Match start spawned it, on the position grid.
  math::Vec3 spawn{};
  std::uint32_t offset = 0;
  SessionIdWire session{};
  /// Its Character, by its name in the scenario's manifest (ADR-0042); at most
  /// kMaxCharacterNameLength bytes.
  std::string character;
  std::uint8_t player = 0;

  bool operator==(const CapturedJoinWire&) const = default;
};

/// A Command a player sent, at the offset of the tick the server's command
/// queue handed it to SimulationWorld.
struct CapturedCommandWire {
  /// As a Commands message carries it, its seen_age 0: seen_offset names its Seen time's tick.
  CommandWire command{};
  std::uint32_t offset = 0;
  /// Its Seen time's tick, as an offset from the Match's first tick: negative
  /// for a State sent before the Match started.
  std::int32_t seen_offset = 0;
  std::uint8_t player = 0;

  bool operator==(const CapturedCommandWire&) const = default;
};

/// A player whose connection ended mid-Match, at the offset of the tick its body was taken out on.
struct CapturedLeaveWire {
  std::uint32_t offset = 0;
  std::uint8_t player = 0;

  bool operator==(const CapturedLeaveWire&) const = default;
};

/// A player who died, and who killed them.
struct CapturedDeathWire {
  std::uint32_t offset = 0;
  std::uint8_t victim = 0;
  std::uint8_t killer = 0;

  bool operator==(const CapturedDeathWire&) const = default;
};

/// The Match's end, at the offset of its last tick.
struct CapturedMatchEndWire {
  std::uint32_t offset = 0;
  /// The winner's number, or 0 for a Draw.
  std::uint8_t winner = 0;

  bool operator==(const CapturedMatchEndWire&) const = default;
};

using CaptureRecordWire = std::variant<CaptureHeaderWire, CapturedJoinWire, CapturedCommandWire, CapturedLeaveWire,
                                       CapturedDeathWire, CapturedMatchEndWire>;

/// Encodes record as one payload, or reports the first field beyond its limit
/// and gives no payload at all, as Encode does a message.
[[nodiscard]] std::expected<BytesWire, EncodeError> EncodeCaptureRecord(const CaptureRecordWire& record);

/// The type record's payload starts with.
[[nodiscard]] CaptureRecordTypeWire TypeOf(const CaptureRecordWire& record);

/// Decodes one capture record's payload, or reports what is wrong with it, as Decode does a message's.
[[nodiscard]] std::expected<CaptureRecordWire, DecodeError> DecodeCaptureRecord(std::span<const std::byte> payload);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_H_
