#ifndef AUGUSTA_SERVER_HOST_LOG_H_
#define AUGUSTA_SERVER_HOST_LOG_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "augusta/networking.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "match.h"

/// \file
/// The log lines Host writes from more than one of its parts (ADR-0029,
/// ADR-0038): how a peer, a session and a departure are named in them, and the
/// lines of a match's end and of a tick's combat. Each of Host's files writes
/// its other lines where they happen; ServerRuntime writes none of these.
namespace augusta::server {

/// Why a match ended.
enum class EndReason : std::uint8_t {
  /// Game policy decided it was over (US-14).
  kWinCondition,
  /// Its last player left.
  kNoPlayersLeft,
  /// Host::EndMatch was called.
  kEndedByTheHost,
};

/// How a player's connection ended.
enum class Leaving : std::uint8_t {
  /// The peer closed it.
  kLeft,
  /// The transport gave up on it.
  kTimedOut,
  /// This side closed it, for misbehaving.
  kMisbehaving,
};

/// The transport's handle as a number, for log lines.
[[nodiscard]] std::uint32_t PeerNumber(networking::PeerId peer);

[[nodiscard]] std::uint32_t SessionNumber(SessionId session);

/// how as a departure's log line says it.
[[nodiscard]] std::string_view LeavingName(Leaving how);

/// The one line of a match that ended: why, who won (nullopt for a draw), how
/// many ticks it lasted, counting the one it ended on, how many players were in
/// it, and the Lobby's Roster version after it.
void LogMatchEnded(EndReason reason, const std::optional<SessionId>& winner, tick::Tick ticks, std::size_t players,
                   std::uint32_t roster_version);

/// One line per hit of state's tick, then one per death. At INFO, though
/// players provoke them: a Match's hits and deaths are what its operator reads
/// the log for (US-12, US-13), in a Release build too.
void LogCombat(const simulation::State& state);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_LOG_H_
