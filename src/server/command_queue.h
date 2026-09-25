#ifndef AUGUSTA_SERVER_COMMAND_QUEUE_H_
#define AUGUSTA_SERVER_COMMAND_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string_view>

#include "augusta/command.h"

// The server's boundary for what a client sends to move its player: a
// structural sanity gate, and a per-player queue that hands SimulationWorld
// exactly one command per tick. Pure (no I/O, no clock), so both are tested
// without a network. The gate is the seam the anti-cheat baseline (US-15) grows
// into: it checks that a command is well formed, not that it is fair play.
namespace augusta::server {

/// One tick's command and the number its client gave it. Numbers start at 1 and
/// grow by one per command, so the queue can tell what it has already seen.
struct SequencedCommand {
  std::uint32_t sequence = 0;
  command::Command command{};
};

/// Why a command was not taken in.
enum class Rejection : std::uint8_t {
  /// Its sequence is not newer than the last taken in: a repeat, or out of order.
  kStale,
  /// A number in it is NaN or infinite.
  kNonFinite,
  /// A number in it is finite but outside what a client can produce.
  kOutOfRange,
};

/// A short lowercase description of rejection, for logs.
[[nodiscard]] std::string_view DescribeRejection(Rejection rejection);

/// The longest movement direction a client may send. The input device yields at
/// most a diagonal (about 1.41); the physics normalizes the direction, so only
/// absurd values are refused.
inline constexpr float kMaxMovementMagnitude = 2.0F;

/// The largest view pitch, in radians: straight up or down, with a little slack.
inline constexpr float kMaxPitch = 1.6F;

/// The largest view yaw, in radians: half a turn either way, with a little
/// slack, since a client keeps its yaw within one turn (command::Command).
inline constexpr float kMaxYaw = 3.2F;

/// Whether command is well formed and newer than last_sequence, the newest
/// sequence already taken in from this client.
[[nodiscard]] std::expected<void, Rejection> Validate(const SequencedCommand& command, std::uint32_t last_sequence);

/// The most commands a queue holds; when a client runs ahead of the server, the oldest go.
inline constexpr std::size_t kMaxQueuedCommands = 16;

/// How many ticks in a row the last movement is repeated when nothing new arrived (about 100 ms at 60 Hz).
inline constexpr int kMaxHeldTicks = 6;

/// What the queue hands a tick.
struct TickCommand {
  command::Command command;
  /// The highest sequence the queue has handed out so far, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
};

/// One player's incoming commands.
class CommandQueue {
 public:
  /// Validates command and, if it passes, queues it.
  [[nodiscard]] std::expected<void, Rejection> TryEnqueue(const SequencedCommand& command);

  /// The command for this tick: the oldest queued one; else the last movement
  /// held for up to kMaxHeldTicks ticks; else no movement. Held and idle ticks
  /// never repeat a one-shot action (reload) or keep firing.
  [[nodiscard]] TickCommand Next();

  /// Whether a command is queued, so the next Next hands out a new one rather than holding or idling.
  [[nodiscard]] bool HasQueued() const { return !queued_.empty(); }

 private:
  std::deque<SequencedCommand> queued_;
  std::optional<command::Command> last_;
  int held_ticks_ = 0;
  std::uint32_t last_offered_ = 0;
  std::uint32_t acknowledged_ = 0;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_COMMAND_QUEUE_H_
