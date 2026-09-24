#ifndef AUGUSTA_SERVER_MATCH_H_
#define AUGUSTA_SERVER_MATCH_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"

// augusta::server::Match is the server's book of who is playing, in the Lobby
// and in a Match (ADR-0043): it decides whether a peer's join is admitted,
// names each admitted peer with a session ID of its own making, remembers its
// character, numbers every version of the Lobby's Roster, counts who is Ready
// for which, and decides when a match starts and when it has ended, handing out
// the map's spawn points in order (a mechanism until spawn rules are Game
// policy). Pure bookkeeping (no I/O, no clock: Host tells it each tick that
// passes), so all of it is tested without a network; what to do with the
// answer - reply, log, spawn bodies - is Host's mechanism.
namespace augusta::server {

/// The least time between one match ending and the next starting, so a Lobby
/// that stays full leaves anyone who is done the time to leave (ADR-0043).
inline constexpr std::chrono::seconds kMatchPause{5};

/// One player in the Lobby.
struct RosterEntry {
  protocol::SessionIdWire session{};
  /// Its character index: 1-based position in the scenario's character list (ADR-0042).
  std::uint8_t character = 1;
};

/// Who is in the Lobby, under the version that names this membership.
struct Roster {
  /// Grows on every join and leave in the Lobby.
  std::uint32_t version = 0;
  /// Ordered by session.
  std::vector<RosterEntry> players;
};

/// One player in a match, and where it spawns.
struct MatchPlayer {
  protocol::SessionIdWire session{};
  std::uint8_t character = 1;
  math::Vec3 spawn{};
};

/// What every player is told when a match starts.
struct MatchStart {
  /// Ordered by session.
  std::vector<MatchPlayer> players;
};

/// What a peer is told when it is admitted to the Lobby.
struct Admission {
  /// The name the server gave the peer's player.
  protocol::SessionIdWire session{};
  /// The character it plays.
  std::uint8_t character = 1;
};

/// What a peer's leaving did, for Host to act on.
enum class Departure : std::uint8_t {
  /// The peer had not joined: nothing changed.
  kNone,
  /// It left the Lobby, whose Roster has a new version.
  kFromLobby,
  /// It left a match in progress, which goes on without it.
  kFromMatch,
  /// It was the last player in the match, which has ended: the Lobby is open, empty.
  kEndedMatch,
};

/// What a match admits players by, fixed for its lifetime.
struct MatchConfig {
  /// Only clients whose engine version is this are admitted.
  std::string engine_version;
  /// Only clients that loaded the client pack of this hash are admitted: the
  /// one cooked with the server's pack.
  protocol::PackHashWire client_pack{};
  /// The characters a client may ask to play: the scenario's, by path, in the
  /// order that gives each its index (ADR-0042).
  std::vector<std::string> characters;
  /// How many players the Lobby holds and a match starts with (ADR-0043).
  std::size_t player_count = 1;
  /// How many ticks must pass after a match ends before the next can start.
  std::uint32_t pause_ticks = 0;
};

/// The Lobby, and the match its players go on to, keyed by the transport's handle for each player.
class Match {
 public:
  /// A match built from config, spawning players at spawn_points in order and
  /// starting over after the last (at the origin if there are none).
  explicit Match(MatchConfig config, std::vector<math::Vec3> spawn_points = {});

  /// Admits peer to the Lobby as request asks, or says why not: its version
  /// first, then its pack, then its character, then whether a match is in
  /// progress, then whether the Lobby is full. A peer that has already joined
  /// gets the admission it already has.
  [[nodiscard]] std::expected<Admission, protocol::JoinRefusalWire> Join(networking::PeerId peer,
                                                                         const protocol::JoinRequestWire& request);

  /// Removes peer from the Lobby or the match it is in, and says which. A
  /// departure from the Lobby leaves whoever was Ready still Ready.
  Departure Leave(networking::PeerId peer);

  /// Records that peer's client has loaded everything needed to draw the Roster
  /// of version. Only the current version counts: returns whether peer is now
  /// Ready for it, false for an older version, a match in progress or a peer
  /// that has not joined.
  bool Ready(networking::PeerId peer, std::uint32_t version);

  /// Whether peer is in the Lobby and Ready for its current Roster.
  [[nodiscard]] bool IsReady(networking::PeerId peer) const;

  /// Counts one server tick passing, towards the pause after a match.
  void Tick();

  /// Starts the match if the Lobby holds the Player count, everyone in it is
  /// Ready for the current Roster and the pause since the last match has
  /// passed; says who is in it and where each spawns. nullopt, changing
  /// nothing, if it cannot start.
  [[nodiscard]] std::optional<MatchStart> TryStart();

  /// Ends the match in progress: its players return to the Lobby, under a new
  /// Roster version none of them is Ready for yet, and the pause begins. Returns
  /// who was in it; empty, changing nothing, if no match is in progress.
  std::vector<protocol::SessionIdWire> End();

  /// Whether a match is in progress.
  [[nodiscard]] bool InMatch() const;

  /// Whether the player of session is in the match in progress.
  [[nodiscard]] bool IsPlaying(protocol::SessionIdWire session) const;

  /// Who is in the match in progress, ordered by session; empty in the Lobby.
  [[nodiscard]] std::vector<protocol::SessionIdWire> Playing() const;

  /// Who is in the Lobby; empty, under the last version, while a match is in progress.
  [[nodiscard]] Roster GetRoster() const;

  /// The session of peer, or nullopt if it has not joined.
  [[nodiscard]] std::optional<protocol::SessionIdWire> SessionOf(networking::PeerId peer) const;

  /// How many players have joined and not left, in the Lobby or the match.
  [[nodiscard]] std::size_t PlayerCount() const;

 private:
  struct Member {
    protocol::SessionIdWire session;
    std::uint8_t character;
    // The Roster version this player's client last loaded for; 0 for none.
    std::uint32_t ready_version = 0;
  };

  // The members ordered by session.
  [[nodiscard]] std::vector<Member> MembersBySession() const;

  std::string engine_version_;
  protocol::PackHashWire client_pack_;
  std::vector<std::string> characters_;
  std::size_t player_count_;
  std::uint32_t pause_ticks_;
  std::vector<math::Vec3> spawn_points_;
  bool in_match_ = false;
  // Versions start at 1 once anyone has joined, so a ready_version of 0 never counts.
  std::uint32_t roster_version_ = 0;
  // Ticks since the last match ended; the first match waits for no pause.
  std::uint32_t ticks_since_end_;
  // Counts every spawn handed out, so spawn points are taken in turn across the life of the server.
  std::size_t next_spawn_ = 0;
  // IDs count up and are never reused, so a session ID never names two
  // players over the life of the server.
  std::uint32_t next_session_ = 1;
  std::unordered_map<networking::PeerId, Member> members_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_MATCH_H_
