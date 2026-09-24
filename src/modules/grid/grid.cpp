#include "augusta/grid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "augusta/math.h"

namespace augusta::grid {

namespace {

float Snap(float value, const Grid& grid) { return FromSteps(ToSteps(value, grid), grid); }

math::Vec3 Snap(const math::Vec3& value, const Grid& grid) {
  return {Snap(value.x, grid), Snap(value.y, grid), Snap(value.z, grid)};
}

}  // namespace

std::int32_t ToSteps(float value, const Grid& grid) {
  if (std::isnan(value)) {
    return 0;
  }
  const float steps = std::nearbyint(value / grid.step);
  return static_cast<std::int32_t>(std::clamp(steps, static_cast<float>(grid.min), static_cast<float>(grid.max)));
}

float FromSteps(std::int32_t steps, const Grid& grid) { return static_cast<float>(steps) * grid.step; }

math::Vec3 SnapPosition(const math::Vec3& position) { return Snap(position, kPosition); }

math::Vec3 SnapVelocity(const math::Vec3& velocity) { return Snap(velocity, kVelocity); }

math::Vec3 SnapDirection(const math::Vec3& direction) { return Snap(direction, kDirection); }

float SnapAngle(float radians) { return Snap(radians, kAngle); }

float SnapStamina(float stamina) { return Snap(stamina, kStamina); }

}  // namespace augusta::grid
