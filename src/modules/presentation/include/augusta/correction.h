#ifndef AUGUSTA_CORRECTION_H_
#define AUGUSTA_CORRECTION_H_

#include "augusta/math.h"

// The visual half of client-side reconciliation (ADR-0004). Reconciliation
// replays the client's commands from the server's state and so moves the
// predicted body at once, by however much the prediction was off. Shown as it
// is, that is a jump; this hides it, by showing the body where it was and
// letting it slide to where it is. Only what is shown is smoothed: the
// predicted body itself is already where the replay put it.
//
// Pure - no clock, no ECS - so it is tested on its own; PresentationWorld feeds
// it each frame.
namespace augusta::presentation {

/// A jump at or beyond this many meters is shown at once rather than slid: too far
/// to look right sliding, most plausibly a respawn or teleport.
inline constexpr float kSnapDistance = 2.0F;

/// The time constant, in seconds, with which a hidden jump fades. At 3.2 times it
/// a jump is down to 4% of its size: gone within 150 ms, the correction budget of NFR-02.
inline constexpr float kFadeTimeConstant = 0.047F;

class Correction {
 public:
  /// What to add to the predicted position to show, for a frame delta_time
  /// seconds after the last one. total_correction is that of the latest
  /// prediction::State (see there): the jumps since the last frame are what it
  /// has grown by, whether the ticks in between were all seen or not.
  [[nodiscard]] math::Vec3 Update(const math::Vec3& total_correction, float delta_time);

 private:
  math::Vec3 seen_total_{};
  math::Vec3 offset_{};
  bool has_seen_ = false;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_CORRECTION_H_
