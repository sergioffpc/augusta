#ifndef AUGUSTA_TICK_H_
#define AUGUSTA_TICK_H_

#include <chrono>

// The fixed schedule both tick loops keep - the server's Simulation thread and
// the client's Prediction thread (ADR-0005): each Tick is due one tick after the
// previous one was due, not one tick after it ended, so a late Tick delays no
// later one. A loop that falls too far behind starts over from now rather than
// running the missed Ticks back to back.
//
// Pure - the caller passes the times in - so it is tested with a fake clock; the
// loops own the clock and the sleeping. The tick's duration is passed on every
// call, not fixed at construction, so a loop may change it between Ticks.
namespace augusta::tick {

using Clock = std::chrono::steady_clock;

/// How many ticks a loop may fall behind its schedule and still catch up; any
/// further and it resynchronises to now.
inline constexpr int kMaxTicksBehind = 3;

/// How long after its deadline a Tick may start and still count as on time: a
/// sleeping thread wakes a little after the time it asked for, never exactly at it.
inline constexpr Clock::duration kLateTolerance = std::chrono::milliseconds{1};

/// When the Tick after the one due at deadline is due, for Ticks tick_duration
/// long, whose work finished at now: one tick after deadline - or now, if that
/// is more than kMaxTicksBehind ticks in the past.
[[nodiscard]] Clock::time_point NextDeadline(Clock::time_point deadline, Clock::duration tick_duration,
                                             Clock::time_point now);

/// How one Tick kept to its schedule, as the server's heartbeat counts it (ADR-0029).
struct Timing {
  /// It started more than kLateTolerance after its deadline.
  bool late = false;
  /// Its work took longer than a tick.
  bool overrun = false;
};

/// How the Tick due at deadline, which ran from start to end, kept to a
/// schedule of Ticks tick_duration long.
[[nodiscard]] Timing Measure(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point start,
                             Clock::time_point end);

}  // namespace augusta::tick

#endif  // AUGUSTA_TICK_H_
