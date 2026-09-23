#ifndef AUGUSTA_SERVER_MATCH_H_
#define AUGUSTA_SERVER_MATCH_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

// augusta::server::Match is the server's book of who is playing: it decides
// whether a peer's join is admitted, names each admitted peer with a session ID
// of its own making, gives it the next spawn point of the map (a mechanism for
// M3; spawn rules are Game policy, M5) and remembers where every player last
// was, so a joining client can be told who is already there. Pure bookkeeping
// (no I/O), so all of it is tested without a network; what to do with the
// answer - reply, log, disconnect - is Host's mechanism.
namespace augusta::server {

/// What a peer is told when it is admitted.
struct Admission {
  /// The name the server gave the peer's player.
  protocol::SessionId session{};
  /// Where its player spawns.
  math::Vec3 spawn{};
  /// The players already in the match, ordered by session, each where it last was.
  std::vector<protocol::PlayerState> roster{};
};

/// What a match admits players by, fixed for its lifetime.
struct MatchConfig {
  /// Only clients whose engine version is this are admitted.
  std::string engine_version;
  /// The characters a client may ask to play: the scenario's, by path (ADR-0042).
  std::vector<std::string> characters{};
  /// The most players the match holds at once.
  std::size_t capacity = protocol::kMaxPlayers;
};

/// The players in one match, keyed by the transport's handle for each.
class Match {
 public:
  /// A match built from config, spawning players at spawn_points in order and
  /// starting over after the last (at the origin if there are none).
  explicit Match(MatchConfig config, std::vector<math::Vec3> spawn_points = {});

  /// Admits peer to play character, or says why not: its version first, then
  /// its character, then whether the match is full. A peer that is already in
  /// the match gets the session and spawn point it already has.
  [[nodiscard]] std::expected<Admission, protocol::JoinRefusal> Join(networking::PeerId peer,
                                                                     std::string_view engine_version,
                                                                     std::string_view character);

  /// Records where the player of session now is, for the roster of whoever joins next.
  /// A no-op if no such player is in the match.
  void UpdateBody(protocol::SessionId session, const physics::BodyState& body);

  /// Removes peer and frees its slot; a no-op if peer is not in the match.
  void Leave(networking::PeerId peer);

  /// The session of peer, or nullopt if it has not joined.
  [[nodiscard]] std::optional<protocol::SessionId> SessionOf(networking::PeerId peer) const;

  /// How many players are in the match now.
  [[nodiscard]] std::size_t PlayerCount() const;

 private:
  struct Member {
    protocol::SessionId session;
    math::Vec3 spawn;
    physics::BodyState body;
  };

  [[nodiscard]] std::vector<protocol::PlayerState> RosterExcluding(protocol::SessionId session) const;

  std::string engine_version_;
  std::vector<std::string> characters_;
  std::size_t capacity_;
  std::vector<math::Vec3> spawn_points_;
  // Counts every admission, so spawn points are taken in turn across the life of the server.
  std::size_t next_spawn_ = 0;
  // IDs count up and are never reused, so a session ID never names two
  // players over the life of the server.
  std::uint32_t next_session_ = 1;
  std::unordered_map<networking::PeerId, Member> members_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_MATCH_H_
