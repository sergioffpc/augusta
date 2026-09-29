#ifndef AUGUSTA_RECONCILIATION_H_
#define AUGUSTA_RECONCILIATION_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

#include "augusta/physics.h"

// What client-side reconciliation (ADR-0004) has to remember: the commands the
// client has sent that the server has not yet answered, and the body it
// predicted after each. Pure - no physics, no network, no clock - so it is
// tested on its own; PredictionWorld does the stepping, handing History the
// step to run.
//
// The server's state is always of an earlier tick than the client's current
// one (it has to travel, and the command it answers had to travel first), so
// it is compared with the state the client predicted after that same command,
// kept here keyed by the command's sequence. The client then puts its body at
// the server's state and replays the commands sent since, so that its present
// is the server's past with the client's own commands carried forward.
namespace augusta::prediction {

/// The commands and predicted states kept, about two seconds at 60 Hz. The
/// server's answer arrives long before it is older than this.
inline constexpr std::size_t kMaxHistory = 128;

/// A body as the client predicted it after a command: what the server's state
/// is compared with, and (with the fall) what a replay of the commands after it
/// restarts from.
struct Predicted {
  physics::BodyState body{};
  physics::FallState fall{};
};

/// The commands the client sent, and what it predicted after each, until the server answers them.
class History {
 public:
  /// The step a replay runs for one command: moves the body by it and returns
  /// the result.
  using Step = std::function<Predicted(const physics::MovementInput&)>;

  /// Remembers command, the one sent under this sequence, and predicted, the
  /// state after it, until the server answers that command. Sequences grow;
  /// beyond kMaxHistory the oldest is forgotten.
  void Record(std::uint32_t sequence, const physics::MovementInput& command, const Predicted& predicted);

  /// The server has answered the command with this sequence: returns the state
  /// predicted after it, to be compared with the server's, and discards it and
  /// everything older, since a newer acknowledgement supersedes them. Empty if
  /// that state is not held: it was already acknowledged (the server repeats an
  /// acknowledgement while it waits for input), is older than the history, or
  /// was never recorded. Whatever is older than sequence is discarded even then.
  [[nodiscard]] std::optional<Predicted> Acknowledge(std::uint32_t sequence);

  /// Runs step for each command held, oldest first, and keeps what it returns as
  /// the state predicted after that command: what those states are once the
  /// body has been put somewhere else. Without it the next acknowledgement would
  /// be compared with a state from before that, and count the difference twice.
  void Replay(const Step& step);

  [[nodiscard]] std::size_t Size() const { return entries_.size(); }

 private:
  struct Entry {
    std::uint32_t sequence;
    physics::MovementInput command;
    Predicted predicted;
  };

  std::deque<Entry> entries_;
};

}  // namespace augusta::prediction

#endif  // AUGUSTA_RECONCILIATION_H_
