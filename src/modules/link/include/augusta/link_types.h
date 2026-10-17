#ifndef AUGUSTA_LINK_TYPES_H_
#define AUGUSTA_LINK_TYPES_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"

/// \file
/// What each of the Networking Protocol's messages says, in the engine's terms
/// (ADR-0038): the game's state the Link turns into messages and back
/// (augusta/link_messages.h). The client's harness takes these as its own
/// (augusta/harness.h names each under augusta::harness); the server's Host
/// builds them from its own Match, replication and Replay types, which name a
/// player and a body by the server's own IDs, and takes in the ones a client
/// sends. Plain values over the shared core's types; no protocol type, no
/// transport.
namespace augusta::link {

/// The server's name for one connected player (GLOSSARY.md, "Session ID"), as
/// a message carries it: assigned when the server admits the join. Distinct
/// from the transport's handle for the connection, and not a credential.
enum class SessionId : std::uint32_t {};

/// The server's name for one dynamic body (GLOSSARY.md, "Entity ID"), as a
/// message carries it: a player's is given in Match start. It names the body,
/// not who moves it, so it is never a session.
enum class EntityId : std::uint32_t {};

/// One dynamic body, under the entity the server names it by.
struct EntityBody {
  EntityId entity{};
  physics::BodyState body{};
  /// Where the body faces: the yaw of its player's view (command::Command), in radians.
  float yaw = 0.0F;
};

/// What the server said about every body as of one of its ticks: its
/// Authoritative State, as one client is sent it.
struct AuthoritativeState {
  /// The server tick this state is from; a client keeps only the newest it has seen.
  tick::Tick tick = 0;
  /// The highest command sequence of this client that the server has processed, 0 if none.
  tick::Sequence acknowledged_sequence = 0;
  /// Every dynamic body in the match.
  std::vector<EntityBody> bodies;
  /// This client's own player's rifle as of that tick: what its predicted rifle
  /// is reconciled against.
  weapon::State rifle{};
  /// This client's own player's health as of that tick, 0 once it has died. No
  /// other player's is told.
  float health = 0.0F;
  /// How many of this client's commands the server still held queued after
  /// that tick: what the client paces its own ticks by (tick::PacedTickDuration).
  std::uint8_t queued_commands = 0;
};

/// One round a player fired, as the server announced it (GLOSSARY.md's Shot,
/// ADR-0044).
struct Shot {
  /// The body of the player who fired it.
  EntityId shooter{};
  /// The server tick it was fired on.
  tick::Tick tick = 0;
  /// Where the round left from.
  math::Vec3 origin{};
  /// Where it left for, as a view's yaw and pitch in radians (command::Command;
  /// command::ViewDirection gives the direction).
  float yaw = 0.0F;
  float pitch = 0.0F;
};

/// A round a client's player fired hit a player, as the server confirmed it to
/// that client (GLOSSARY.md's Hit confirmation, ADR-0044).
struct HitConfirmation {
  /// The body that was hit.
  EntityId target{};
  /// The damage the hit did.
  float damage = 0.0F;
  /// Where on the body it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

/// Where one player of a Replay looked on one tick, from the Command the
/// replay server's World ran (ADR-0051): what its Replay viewer is shown, which
/// no Authoritative State carries.
struct PlayerView {
  EntityId entity{};
  /// The view's pitch, in radians (command::Command).
  float pitch = 0.0F;
  bool ads = false;
};

/// Every player's view on one tick of a Replay, as its replay server sends it.
struct ReplayView {
  /// The tick, as the Authoritative State of the same tick names it.
  tick::Tick tick = 0;
  std::vector<PlayerView> players;
};

/// A player in the match died (US-13), as the server told every client in it:
/// for the rest of the match. Carries what a ragdoll would start from (ADR-0045).
struct Death {
  /// The body of the player who died.
  EntityId victim{};
  /// The body of the player who fired the killing round.
  EntityId killer{};
  /// Where the killing round was fired for, as a view's yaw and pitch in
  /// radians (command::ViewDirection gives the direction).
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Where on the victim it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

/// One player in the Lobby.
struct RosterEntry {
  SessionId session{};
  /// Its character, by its name in the scenario's manifest (ADR-0042).
  std::string character;
};

/// Who is in the Lobby, as the server last said (ADR-0043).
struct Lobby {
  /// Numbers this Roster; what a client's Ready names.
  std::uint32_t version = 0;
  /// Every player in the Lobby.
  std::vector<RosterEntry> roster;
};

/// One player in a match, the body it controls, and where the server spawned it.
struct MatchPlayer {
  SessionId session{};
  /// The body this player's commands move for the whole match.
  EntityId entity{};
  /// Its character (see RosterEntry::character).
  std::string character;
  math::Vec3 spawn{};
};

/// What the server said when a match started: everyone in it.
struct MatchStart {
  std::vector<MatchPlayer> players;
  /// The Match's first server tick: what a Match capture's offsets count from (ADR-0050).
  tick::Tick first_tick = 0;
};

/// What the server said when a match ended (US-14).
struct MatchEnd {
  /// The session of the player Game policy declared the winner; nullopt for a draw.
  std::optional<SessionId> winner;
};

/// Why the server refused a client's join.
enum class JoinRefusal : std::uint8_t {
  /// The client's engine version is not the server's.
  kVersionMismatch,
  /// The Lobby already holds the scenario's Player count.
  kLobbyFull,
  /// The character the client asked to play is not one of the scenario's (ADR-0042).
  kUnknownCharacter,
  /// A match is under way, and no one joins one in progress (ADR-0043).
  kMatchInProgress,
  /// The client's pack is not the one cooked with the server's.
  kPackMismatch,
  /// The server is a replay server, which only replays Match captures (ADR-0051).
  kReplayServer,
  /// The capture the client asked to watch is none the replay server replays (ADR-0051).
  kUnknownCapture,
  /// The client asked a live server for Replays, which only a replay server serves (ADR-0051).
  kNotAReplayServer,
  /// The client asked to join as a Captured player, and the server takes none (ADR-0050).
  kReenactmentsNotAccepted,
};

/// What the server said when it admitted a client.
struct Admission {
  /// The session the server assigned to the client.
  SessionId session{};
  /// The rate, in Hz, at which the server ticks and the client must.
  std::uint8_t tick_rate_hz = 0;
  /// The parameters the client must predict with.
  parameters::Parameters parameters{};
  /// The client's own character (see RosterEntry::character).
  std::string character;
};

/// One capture a replay server replays, as its Replay list names it (ADR-0051).
struct ReplayListing {
  /// Its file name, which a Replay request names it by.
  std::string name;
  /// When its Match started, in UTC.
  std::chrono::sys_time<std::chrono::milliseconds> started;
  /// How long it lasted, in ticks at tick_rate_hz: up to its Match end, or
  /// to its last record if it ends partway.
  std::uint32_t ticks = 0;
  std::uint8_t tick_rate_hz = 0;
  /// Its players' Characters, in its Join order.
  std::vector<std::string> characters;

  bool operator==(const ReplayListing&) const = default;
};

/// What a client asks when it joins.
struct JoinRequest {
  /// Its engine version (augusta::EngineVersion).
  std::string engine_version;
  /// The hash of the client pack it loaded.
  assets::PackHash client_pack{};
  /// The character it asks to play, by its name in the scenario's manifest
  /// (e.g. "soldier", ADR-0042).
  std::string character;
  /// Where it asks to spawn, as a Captured player (ADR-0050), which only a
  /// Reenact request names; nullopt for anyone else.
  std::optional<math::Vec3> spawn = std::nullopt;
};

/// What a client asks when it watches a Replay (ADR-0051).
struct ReplayRequest {
  /// Its engine version (augusta::EngineVersion).
  std::string engine_version;
  /// The hash of the client pack it loaded.
  assets::PackHash client_pack{};
  /// The capture to watch, by its name in the replay server's Replay list.
  std::string capture;
};

/// One tick's command and the number its client gave it. Numbers start at 1 and
/// grow by one per command; they start over with every connection, in
/// tick::Sequence's width (ADR-0038).
struct SequencedCommand {
  tick::Sequence sequence = 0;
  command::Command command{};
};

/// The Networking Protocol's message types (ADR-0038), in the engine's terms:
/// what a payload is, for whoever counts what it sends and receives.
enum class MessageType : std::uint8_t {
  kJoinRequest,
  kJoinAccepted,
  kJoinRefused,
  kCommands,
  kAuthoritativeState,
  kLobby,
  kReady,
  kMatchStart,
  kMatchEnd,
  kShot,
  kHitConfirmation,
  kDeath,
  kReplayListRequest,
  kReplayList,
  kReplayRequest,
  kReplayView,
  kReenactRequest,
};

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_TYPES_H_
