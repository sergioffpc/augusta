#include "augusta/effects.h"

#include <cstdint>
#include <vector>

#include "augusta/math.h"

namespace augusta::presentation {

namespace {

// How far ahead of the eye, in meters, a rifle's muzzle is.
constexpr float kMuzzleDistance = 0.6F;

}  // namespace

void Age(std::vector<Effect>& effects, float elapsed, float lifetime) {
  for (Effect& effect : effects) {
    effect.age += elapsed;
  }
  std::erase_if(effects, [lifetime](const Effect& effect) { return effect.age >= lifetime; });
}

math::Vec3 MuzzleOf(const math::Vec3& eye, const math::Vec3& direction) {
  return eye + (math::Normalize(direction) * kMuzzleDistance);
}

std::uint32_t FiredRounds::Update(std::uint32_t total_rounds_fired) {
  const std::uint32_t fired = seen_.has_value() ? total_rounds_fired - *seen_ : 0U;
  seen_ = total_rounds_fired;
  return fired;
}

}  // namespace augusta::presentation
