#include "augusta/parameters.h"

#include <cmath>

namespace augusta::parameters {
namespace {

// A finite number of at least min.
bool AtLeast(float value, float min) { return std::isfinite(value) && value >= min; }

}  // namespace

std::expected<void, InvalidParameter> Validate(const Parameters& parameters) {
  if (!std::isfinite(parameters.tick_rate_hz) || parameters.tick_rate_hz <= 0.0F) {
    return std::unexpected(InvalidParameter{.path = "tick_rate_hz"});
  }
  const physics::StaminaConfig& stamina = parameters.stamina;
  if (!AtLeast(stamina.deplete_per_second, 0.0F)) {
    return std::unexpected(InvalidParameter{.path = "stamina.deplete_per_second"});
  }
  if (!AtLeast(stamina.regen_per_second, 0.0F)) {
    return std::unexpected(InvalidParameter{.path = "stamina.regen_per_second"});
  }
  if (!AtLeast(stamina.forced_walk_below, 0.0F) || stamina.forced_walk_below >= 1.0F) {
    return std::unexpected(InvalidParameter{.path = "stamina.forced_walk_below"});
  }
  return {};
}

}  // namespace augusta::parameters
