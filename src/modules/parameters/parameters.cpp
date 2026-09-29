#include "augusta/parameters.h"

#include <cmath>
#include <cstdint>
#include <expected>

#include "augusta/physics.h"
#include "augusta/protocol.h"

namespace augusta::parameters {
namespace {

// A finite number of at least min.
bool FiniteAndAtLeast(float value, float min) { return std::isfinite(value) && value >= min; }

}  // namespace

std::expected<void, InvalidParameter> Validate(const Parameters& parameters) {
  if (parameters.player_count < 1 || parameters.player_count > protocol::kMaxPlayers) {
    return std::unexpected(InvalidParameter{.path = "player_count"});
  }
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

bool IsValidTickRate(std::uint8_t tick_rate_hz) { return tick_rate_hz > 0; }

}  // namespace augusta::parameters
