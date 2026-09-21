#ifndef AUGUSTA_RECONCILIATION_H_
#define AUGUSTA_RECONCILIATION_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include "augusta/math.h"
#include "augusta/physics.h"

// The decision half of client-side reconciliation (ADR-0004): given what the
// client predicted and what the server says, how far to move the client's
// current state. Pure - no physics, no network, no clock - so it is tested on
// its own; PredictionWorld applies the result through physics::World::Correct.
//
// The server's state is always of an earlier tick than the client's current
// one (it has to travel, and the command it answers had to travel first), so
// it is compared with the state the client predicted after that same command,
// kept in a History keyed by the command's sequence. The error found is then
// applied to the current state, and to every later state in the History, so the
// next comparison sees only what is still left of it.
namespace augusta::prediction {

/// An error at or beyond this many meters is corrected at once; a closer one is blended in.
inline constexpr float kSnapDistance = 2.0F;

/// The share of the remaining error a blended correction removes. Applied once per
/// server update (one per tick), the error falls to 0.7^9 = 4% within 9 ticks,
/// the 150 ms of NFR-02.
inline constexpr float kBlendFactor = 0.3F;

/// The predicted states kept, about two seconds at 60 Hz. The server's answer
/// arrives long before it is older than this.
inline constexpr std::size_t kMaxHistory = 128;

/// How far to move the client's state, as ResolveCorrection decided it.
struct Correction {
  math::Vec3 position{};
  math::Vec3 velocity{};
  float stamina = 0.0F;
  /// Set when the server holds a different stance than was predicted (it refused a change).
  std::optional<physics::Stance> stance{};
  /// Whether the error was too large to blend and the correction is the whole of it.
  bool snapped = false;
  /// The distance between the server's position and the predicted one, in meters.
  float error = 0.0F;
};

/// How to move toward authoritative from predicted, both of the same command.
[[nodiscard]] Correction ResolveCorrection(const physics::BodyState& predicted,
                                           const physics::BodyState& authoritative);

/// state moved by correction.
[[nodiscard]] physics::BodyState Apply(const physics::BodyState& state, const Correction& correction);

/// The states the client predicted after each of its recent commands.
class History {
 public:
  /// Remembers the state after the command with this sequence. Sequences grow;
  /// beyond kMaxHistory the oldest is forgotten.
  void Record(std::uint32_t sequence, const physics::BodyState& state);

  /// The state predicted after the command with this sequence, discarding it
  /// and everything older, since a newer acknowledgement supersedes them. Empty
  /// if that state is not held: it was already acknowledged (the server repeats
  /// an acknowledgement while it waits for input), or is older than the history.
  [[nodiscard]] std::optional<physics::BodyState> Acknowledge(std::uint32_t sequence);

  /// Moves every state held by correction, except its stance: what those states
  /// would have been had the client started from the corrected state.
  void Shift(const Correction& correction);

  [[nodiscard]] std::size_t Size() const { return entries_.size(); }

 private:
  struct Entry {
    std::uint32_t sequence;
    physics::BodyState state;
  };

  std::deque<Entry> entries_;
};

}  // namespace augusta::prediction

#endif  // AUGUSTA_RECONCILIATION_H_
