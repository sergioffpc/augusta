#ifndef AUGUSTA_SERVER_HEARTBEAT_H_
#define AUGUSTA_SERVER_HEARTBEAT_H_

#include <chrono>
#include <cstdint>
#include <optional>

#include "augusta/tick.h"

/// \file
/// The counts behind the server's heartbeat (ADR-0029): once a second, one line
/// of what Network I/O and the ticks did in it, since a line per tick or per
/// packet would bury the one that matters. Heartbeat counts and decides when a
/// second is over; Host counts into it and writes the line. Pure (no I/O, no
/// clock of its own: the caller hands it the time), so it is tested without a
/// network.
namespace augusta::server {

inline constexpr std::chrono::seconds kHeartbeatInterval{1};

/// What Network I/O and the ticks did over one heartbeat interval.
struct Activity {
  std::uint32_t ticks = 0;
  /// Ticks that started after their deadline, and ticks whose work took longer
  /// than a tick (NFR-01).
  std::uint32_t late = 0;
  std::uint32_t overrun = 0;
  std::uint32_t messages = 0;
  /// Commands turned away as already handled, or as sent outside a match:
  /// routine, since commands repeat and some are in flight when a match ends.
  std::uint32_t stale = 0;
  /// Messages and commands refused for being malformed, or sent out of turn.
  std::uint32_t dropped = 0;
  /// Queued commands dropped because a client ran further ahead than
  /// kMaxQueuedCommands: its pacing is not keeping up.
  std::uint32_t overflow = 0;
  /// Peers disconnected for misbehaving.
  std::uint32_t misbehaving = 0;
};

/// The Activity of the heartbeat interval in progress.
class Heartbeat {
 public:
  /// Starts the first interval at start.
  explicit Heartbeat(std::chrono::steady_clock::time_point start);

  /// The interval in progress, which the Host counts what it does into.
  [[nodiscard]] Activity& Current();

  /// Counts a tick that kept to its schedule as timing says, at now. Once
  /// kHeartbeatInterval has passed since the interval started, returns it,
  /// this tick included, and starts the next at now; nullopt until then.
  [[nodiscard]] std::optional<Activity> RecordTick(const tick::Timing& timing,
                                                   std::chrono::steady_clock::time_point now);

 private:
  Activity current_;
  std::chrono::steady_clock::time_point since_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HEARTBEAT_H_
