#ifndef AUGUSTA_EFFECTS_H_
#define AUGUSTA_EFFECTS_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "augusta/math.h"

/// \file
/// The fight's short-lived effects (ADR-0024's Interpolation phase): muzzle
/// flashes, and the impacts tracers leave on the Map (tracers.h). Each shows at
/// a point for a moment and is gone. The local player's flashes come from its
/// predicted fire, every other player's from its Shots (ADR-0044).
///
/// Pure - no clock, no ECS - so it is tested on its own; PresentationWorld feeds
/// it each frame (see presentation.cpp).
namespace augusta::presentation {

/// How long, in seconds, a muzzle flash shows.
inline constexpr float kMuzzleFlashSeconds = 0.05F;
/// How long, in seconds, an impact on the Map shows.
inline constexpr float kImpactSeconds = 1.0F;

/// A short-lived effect at a point: a muzzle flash or an impact.
struct Effect {
  math::Vec3 position{};
  /// How long ago, in seconds, it appeared.
  float age = 0.0F;
};

/// Ages every effect by elapsed seconds and forgets those whose age reaches
/// lifetime. An effect added after the frame's call shows on that frame.
void Age(std::vector<Effect>& effects, float elapsed, float lifetime);

/// Where the muzzle flash of a round leaving from eye (a Shot's origin) for
/// direction shows: a short way ahead of the eye along it, where the rifle's
/// muzzle is.
[[nodiscard]] math::Vec3 MuzzleOf(const math::Vec3& eye, const math::Vec3& direction);

/// Counts the rounds the local player's rifle fired between two frames, from
/// the running total each Prediction State carries
/// (prediction::State::total_rounds_fired): every one once, even when a frame
/// sees none of the ticks that fired them.
class FiredRounds {
 public:
  /// The rounds fired since the last call's total_rounds_fired; none on the
  /// first call, which only sets where counting starts.
  [[nodiscard]] std::uint32_t Update(std::uint32_t total_rounds_fired);

 private:
  std::optional<std::uint32_t> seen_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_EFFECTS_H_
