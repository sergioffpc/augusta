#ifndef AUGUSTA_DEBUG_KEPT_SHAPES_H_
#define AUGUSTA_DEBUG_KEPT_SHAPES_H_

#include <chrono>
#include <cstddef>
#include <limits>
#include <vector>

#include "augusta/debug_shapes.h"

/// \file
/// augusta::debug's keep-until helper (ADR-0075): Debug overlay shapes kept
/// until a time, for a producer that hears of an event once, such as a shot
/// the server announced, and keeps it drawn for a few seconds. The producer
/// owns the store as part of its state and hands the Renderer, each frame, the
/// shapes still alive, so the Renderer keeps nothing between frames. It owns
/// no clock: the caller passes the time. Compiled only in a Development build
/// (ADR-0074).
namespace augusta::debug {

class KeptShapes {
 public:
  using Clock = std::chrono::steady_clock;

  /// Keeps at most capacity of what Keep is given at a time.
  explicit KeptShapes(std::size_t capacity = std::numeric_limits<std::size_t>::max());

  /// Keeps shapes until, after which Alive no longer returns them; at capacity,
  /// first drops what was kept first.
  void Keep(const Shapes& shapes, Clock::time_point until);

  /// The shapes kept until after now, in the order they were kept; drops the
  /// rest from the store, so a later call never returns them, even for an
  /// earlier time.
  Shapes Alive(Clock::time_point now);

  /// How many of what Keep was given the store holds: what the last Alive left
  /// and what was kept since.
  [[nodiscard]] std::size_t Count() const;

 private:
  struct Kept {
    Shapes shapes;
    Clock::time_point until;
  };

  std::size_t capacity_;
  std::vector<Kept> kept_;
};

}  // namespace augusta::debug

#endif  // AUGUSTA_DEBUG_KEPT_SHAPES_H_
