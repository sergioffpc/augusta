#ifndef AUGUSTA_TICK_H_
#define AUGUSTA_TICK_H_

#include <chrono>
#include <cstdint>

/// \file
/// The fixed schedule both tick loops keep - the server's Simulation thread and
/// the client's Prediction thread (ADR-0005): each Tick is due one tick after the
/// previous one was due, not one tick after it ended, so a late Tick delays no
/// later one. A loop that falls too far behind starts over from now rather than
/// running the missed Ticks back to back.
///
/// Pure - the caller passes the times in - so it is tested with a fake clock; the
/// loops own the clock and the sleeping. The tick's duration is passed on every
/// call, not fixed at construction, so a loop may change it between Ticks.
///
/// It depends on nothing but the standard library, and must stay that way, so
/// the Networking Protocol encodes the Tick and the Sequence without taking the
/// gameplay rules; and nothing else gives either an integer type of its own
/// (checks/core_boundary.cmake).
namespace augusta::tick {

using Clock = std::chrono::steady_clock;

/// A server Tick's number, as both tick loops count it: from 1 for the life of
/// the server process, never starting over, so wide enough never to wrap (32
/// bits would after about 828 days at 60 Hz). The wire takes this width too.
using Tick = std::uint64_t;

/// The number a client gives each Command it sends, and the acknowledged
/// sequence that answers it (ADR-0038): from 1, one more per command, over one
/// connection. As wide as Tick, so it never wraps either and receivers order
/// sequences as plain numbers.
using Sequence = std::uint64_t;

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

/// How one Tick kept to its schedule, as the server's heartbeat and metrics
/// count it (ADR-0029, ADR-0049).
struct Timing {
  /// How long its work took.
  Clock::duration duration{};
  /// It started more than kLateTolerance after its deadline.
  bool late = false;
  /// Its work took longer than a tick.
  bool overrun = false;
  /// It ended so far behind its schedule that the loop resynchronises to now
  /// (NextDeadline).
  bool resynchronised = false;
};

/// How the Tick due at deadline, which ran from start to end, kept to a
/// schedule of Ticks tick_duration long.
[[nodiscard]] Timing Measure(Clock::time_point deadline, Clock::duration tick_duration, Clock::time_point start,
                             Clock::time_point end);

/// How many of a client's commands the server should hold queued after each of
/// its Ticks: one or two, so a command that arrives a little late still finds
/// one ahead of it, without adding more than a tick or two of input latency.
inline constexpr float kTargetQueuedCommands = 1.5F;

/// How much a client lengthens (or shortens) its Tick per command the server
/// holds above (or below) kTargetQueuedCommands.
inline constexpr float kPacingPerCommand = 0.04F;

/// The most a client's Tick may be lengthened or shortened, as a fraction of
/// its nominal duration, while no more than kBacklogCommands are held. It must
/// exceed the drift between a client's clock and the server's that pacing is to
/// absorb.
inline constexpr float kMaxPacing = 0.05F;

/// More of a client's commands held than this is a backlog, such as a gap in
/// its commands leaves once they arrive, not drift.
inline constexpr std::uint8_t kBacklogCommands = 3;

/// The most a client's Tick may be lengthened while a backlog is held, as a
/// fraction of its nominal duration: enough that 7 held drain in about 0.75 s.
inline constexpr float kMaxBacklogPacing = 0.25F;

/// How long a client's next Tick lasts, nominal being the server's tick
/// duration, when the server last said it held queued_commands of its commands
/// (ADR-0038): longer when there are more than kTargetQueuedCommands (the client
/// runs ahead), shorter when there are fewer, never more than kMaxPacing shorter,
/// and no more than kMaxPacing longer, or kMaxBacklogPacing while more than
/// kBacklogCommands are held. Only when the client's Ticks happen changes: each still simulates a
/// nominal tick, and the server still consumes one command per Tick.
[[nodiscard]] Clock::duration PacedTickDuration(Clock::duration nominal, std::uint8_t queued_commands);

/// How far through a Tick tick_duration long, due at tick_start, now is: 0 at
/// its start, 1 at its end, and held there before and after it (a Tick of no
/// duration is already over) - the fraction the client's render frame blends
/// the Tick's two Prediction States by.
[[nodiscard]] float FractionElapsed(Clock::time_point tick_start, Clock::duration tick_duration, Clock::time_point now);

}  // namespace augusta::tick

#endif  // AUGUSTA_TICK_H_
