#ifndef AUGUSTA_SERVER_HEARTBEAT_H_
#define AUGUSTA_SERVER_HEARTBEAT_H_

#include <chrono>
#include <cstdint>
#include <optional>

/// \file
/// The counts behind the server's heartbeat (ADR-0029): once a second, one line
/// of what Network I/O and the ticks did in it, since a line per tick or per
/// packet would bury the one that matters. Heartbeat decides when a second is
/// over and what happened in it, from the running totals of the counters the
/// metrics expose (HostMetrics, ADR-0049), so the line and the series count each
/// event once and cannot disagree; Host hands it the totals and writes the line.
/// Pure (no I/O, no clock of its own: the caller hands it the time), so it is
/// tested without a network.
namespace augusta::server {

inline constexpr std::chrono::seconds kHeartbeatInterval{1};

/// What Network I/O and the ticks did: since the server started, as running
/// totals, or over one heartbeat interval.
struct Activity {
  std::uint64_t ticks = 0;
  /// Ticks that started after their deadline, and ticks whose work took longer
  /// than a tick (NFR-01).
  std::uint64_t late = 0;
  std::uint64_t overrun = 0;
  /// Messages received that decoded as one of the protocol's.
  std::uint64_t messages = 0;
  /// Commands turned away as already handled, or as sent outside a match:
  /// routine, since commands repeat and some are in flight when a match ends.
  std::uint64_t stale = 0;
  /// Messages and commands refused for being malformed, or sent out of turn:
  /// every misbehaviour (misbehaviour.h).
  std::uint64_t dropped = 0;
  /// Queued commands dropped because a client ran further ahead than
  /// kMaxQueuedCommands: its pacing is not keeping up.
  std::uint64_t overflow = 0;
  /// Peers disconnected for misbehaving, or for not being admitted in time.
  std::uint64_t misbehaving = 0;
};

/// When each heartbeat interval ends, and the Activity of the one in progress.
class Heartbeat {
 public:
  /// Starts the first interval at start, with every total at 0.
  explicit Heartbeat(std::chrono::steady_clock::time_point start);

  /// Once kHeartbeatInterval has passed since the interval in progress started,
  /// returns what happened in it - totals, the running totals at now, less those
  /// when it started - and starts the next at now; nullopt until then.
  [[nodiscard]] std::optional<Activity> Record(const Activity& totals, std::chrono::steady_clock::time_point now);

 private:
  Activity at_start_;
  std::chrono::steady_clock::time_point since_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HEARTBEAT_H_
