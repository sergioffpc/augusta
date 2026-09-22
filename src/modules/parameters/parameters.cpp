#include "augusta/parameters.h"

#include <cmath>

namespace augusta::parameters {
namespace {

// A finite number of at least min.
bool FiniteAndAtLeast(float value, float min) { return std::isfinite(value) && value >= min; }

}  // namespace

std::expected<void, InvalidParameter> Validate(const Parameters& parameters) {
  const physics::StaminaConfig& stamina = parameters.stamina;
  if (!FiniteAndAtLeast(stamina.deplete_per_second, 0.0F)) {
    return std::unexpected(InvalidParameter{.path = "stamina.deplete_per_second"});
  }
  if (!FiniteAndAtLeast(stamina.regen_per_second, 0.0F)) {
    return std::unexpected(InvalidParameter{.path = "stamina.regen_per_second"});
  }
  if (!FiniteAndAtLeast(stamina.forced_walk_below, 0.0F) || stamina.forced_walk_below >= 1.0F) {
    return std::unexpected(InvalidParameter{.path = "stamina.forced_walk_below"});
  }
  return {};
}

bool IsValidTickRate(float tick_rate_hz) { return std::isfinite(tick_rate_hz) && tick_rate_hz > 0.0F; }

std::expected<void, ReplacementError> CheckReplacement(const NumberedParameters& held,
                                                       const NumberedParameters& candidate) {
  if (candidate.generation <= held.generation) {
    return std::unexpected(ReplacementError{.reason = ReplacementRefusal::kNotNewer});
  }
  if (const auto valid = Validate(candidate.parameters); !valid) {
    return std::unexpected(ReplacementError{.reason = ReplacementRefusal::kInvalid, .parameter = valid.error().path});
  }
  return {};
}

}  // namespace augusta::parameters
