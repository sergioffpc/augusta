#include "augusta/effects.h"

#include <cstdint>
#include <vector>

namespace augusta::presentation {

void Age(std::vector<Effect>& effects, float elapsed, float lifetime) {
  for (Effect& effect : effects) {
    effect.age += elapsed;
  }
  std::erase_if(effects, [lifetime](const Effect& effect) { return effect.age >= lifetime; });
}

std::uint32_t FiredRounds::Update(std::uint32_t total_rounds_fired) {
  const std::uint32_t fired = seen_.has_value() ? total_rounds_fired - *seen_ : 0U;
  seen_ = total_rounds_fired;
  return fired;
}

}  // namespace augusta::presentation
