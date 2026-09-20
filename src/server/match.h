#ifndef AUGUSTA_SERVER_MATCH_H_
#define AUGUSTA_SERVER_MATCH_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "augusta/networking.h"
#include "augusta/protocol.h"

// augusta::server::Match is the server's book of who is playing: it decides
// whether a peer's join is admitted, and names each admitted peer with a
// session ID of its own making. Pure bookkeeping (no I/O), so admission is
// tested without a network; what to do with the answer - reply, log,
// disconnect - is Host's mechanism.
namespace augusta::server {

/// The players a match supports (US-02).
inline constexpr std::size_t kMaxPlayers = 8;

/// The players in one match, keyed by the transport's handle for each.
class Match {
 public:
  /// A match that admits only clients whose engine version is engine_version, up to capacity of them.
  explicit Match(std::string engine_version, std::size_t capacity = kMaxPlayers);

  /// Admits peer and returns its session ID, or says why not. A peer that is
  /// already in the match gets the session it already has.
  [[nodiscard]] std::expected<protocol::SessionId, protocol::JoinRefusal> Join(networking::PeerId peer,
                                                                               std::string_view engine_version);

  /// Removes peer and frees its slot; a no-op if peer is not in the match.
  void Leave(networking::PeerId peer);

  /// The session of peer, or nullopt if it has not joined.
  [[nodiscard]] std::optional<protocol::SessionId> SessionOf(networking::PeerId peer) const;

  /// How many players are in the match now.
  [[nodiscard]] std::size_t PlayerCount() const;

 private:
  std::string engine_version_;
  std::size_t capacity_;
  // IDs count up and are never reused, so a session ID never names two
  // players over the life of the server.
  std::uint32_t next_session_ = 1;
  std::unordered_map<networking::PeerId, protocol::SessionId> sessions_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_MATCH_H_
