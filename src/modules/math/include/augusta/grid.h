#ifndef AUGUSTA_GRID_H_
#define AUGUSTA_GRID_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "augusta/math.h"

// The grids a body's and a command's numbers live on (ADR-0038): a position, a
// velocity, a direction, an angle or a stamina is a whole count of its grid's
// step, within its grid's range. physics::World keeps every body on them and the
// Networking Protocol sends each number as its count, so what a peer is told is
// exactly what the sender has. They are augusta::math's, which both depend on,
// so neither depends on the other and the two can never disagree on a grid.
namespace augusta::math {

/// A whole count of step, from min to max steps, which fits in bytes bytes
/// (two's complement when min is below 0): the fewest the protocol can send it
/// in. Every step is a power of two, so a count times its step is an exact
/// float, and a value on the grid snaps to itself.
struct Grid {
  float step;
  int bytes;
  std::int32_t min;
  std::int32_t max;
};

/// A position, per axis: 1/1024 m (about a millimeter), within 8192 m of the origin.
inline constexpr Grid kPositionGrid{.step = 1.0F / 1024.0F, .bytes = 3, .min = -(1 << 23), .max = (1 << 23) - 1};

/// A velocity, per axis: 1/512 m/s, within 64 m/s.
inline constexpr Grid kVelocityGrid{.step = 1.0F / 512.0F,
                                    .bytes = 2,
                                    .min = std::numeric_limits<std::int16_t>::min(),
                                    .max = std::numeric_limits<std::int16_t>::max()};

/// A movement direction, per axis: 1/16384, within 2.
inline constexpr Grid kDirectionGrid{.step = 1.0F / 16384.0F,
                                     .bytes = 2,
                                     .min = std::numeric_limits<std::int16_t>::min(),
                                     .max = std::numeric_limits<std::int16_t>::max()};

/// An angle (a yaw or a pitch): 2^-21 rad (about 0.5 microradians, 0.4 mm at 800 m), within 4 rad.
inline constexpr Grid kAngleGrid{.step = 1.0F / 2097152.0F, .bytes = 3, .min = -(1 << 23), .max = (1 << 23) - 1};

/// A stamina: 1/32768, from 0 to 2.
inline constexpr Grid kStaminaGrid{
    .step = 1.0F / 32768.0F, .bytes = 2, .min = 0, .max = std::numeric_limits<std::uint16_t>::max()};

/// value as a count of grid's step: the nearest (ties to even), held within the
/// grid's range. A NaN is 0.
[[nodiscard]] inline std::int32_t ToSteps(float value, const Grid& grid) {
  if (std::isnan(value)) {
    return 0;
  }
  const float steps = std::nearbyint(value / grid.step);
  return static_cast<std::int32_t>(std::clamp(steps, static_cast<float>(grid.min), static_cast<float>(grid.max)));
}

/// The value steps of grid's step stand for.
[[nodiscard]] inline float FromSteps(std::int32_t steps, const Grid& grid) {
  return static_cast<float>(steps) * grid.step;
}

/// value on grid.
[[nodiscard]] inline float Snap(float value, const Grid& grid) { return FromSteps(ToSteps(value, grid), grid); }

/// Each axis of value on grid.
[[nodiscard]] inline Vec3 Snap(const Vec3& value, const Grid& grid) {
  return {Snap(value.x, grid), Snap(value.y, grid), Snap(value.z, grid)};
}

/// position on kPositionGrid, which is what the protocol's Decode gives back once
/// Encode has sent it. A value beyond the range is the bound and a NaN is 0,
/// here and in the Snap functions below.
[[nodiscard]] inline Vec3 SnapPosition(const Vec3& position) { return Snap(position, kPositionGrid); }

/// velocity on kVelocityGrid.
[[nodiscard]] inline Vec3 SnapVelocity(const Vec3& velocity) { return Snap(velocity, kVelocityGrid); }

/// A movement direction on kDirectionGrid.
[[nodiscard]] inline Vec3 SnapDirection(const Vec3& direction) { return Snap(direction, kDirectionGrid); }

/// An angle (a yaw or a pitch) on kAngleGrid.
[[nodiscard]] inline float SnapAngle(float radians) { return Snap(radians, kAngleGrid); }

/// A stamina on kStaminaGrid.
[[nodiscard]] inline float SnapStamina(float stamina) { return Snap(stamina, kStaminaGrid); }

}  // namespace augusta::math

#endif  // AUGUSTA_GRID_H_
