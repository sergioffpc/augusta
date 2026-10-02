#include "augusta/parameters.h"

#include <cmath>
#include <cstdint>
#include <expected>
#include <numbers>
#include <string_view>

#include "augusta/physics.h"
#include "augusta/protocol.h"

namespace augusta::parameters {
namespace {

// A finite number of at least min.
bool FiniteAndAtLeast(float value, float min) { return std::isfinite(value) && value >= min; }

// A finite number above min.
bool FiniteAndAbove(float value, float min) { return std::isfinite(value) && value > min; }

std::unexpected<InvalidParameter> Invalid(std::string_view path) {
  return std::unexpected(InvalidParameter{.path = path});
}

std::expected<void, InvalidParameter> ValidateStamina(const physics::StaminaConfig& stamina) {
  if (!FiniteAndAtLeast(stamina.deplete_per_second, 0.0F)) {
    return Invalid("stamina.deplete_per_second");
  }
  if (!FiniteAndAtLeast(stamina.regen_per_second, 0.0F)) {
    return Invalid("stamina.regen_per_second");
  }
  if (!FiniteAndAtLeast(stamina.forced_walk_below, 0.0F) || stamina.forced_walk_below >= 1.0F) {
    return Invalid("stamina.forced_walk_below");
  }
  return {};
}

std::expected<void, InvalidParameter> ValidateRifle(const Rifle& rifle) {
  if (!FiniteAndAbove(rifle.rounds_per_minute, 0.0F)) {
    return Invalid("rifle.rounds_per_minute");
  }
  if (!FiniteAndAbove(rifle.muzzle_velocity, 0.0F)) {
    return Invalid("rifle.muzzle_velocity");
  }
  if (!FiniteAndAtLeast(rifle.reload_seconds, 0.0F)) {
    return Invalid("rifle.reload_seconds");
  }
  if (!FiniteAndAtLeast(rifle.recoil_recovery_per_second, 0.0F)) {
    return Invalid("rifle.recoil_recovery_per_second");
  }
  if (!FiniteAndAbove(rifle.ads_recoil_scale, 0.0F) || rifle.ads_recoil_scale > 1.0F) {
    return Invalid("rifle.ads_recoil_scale");
  }
  if (!FiniteAndAbove(rifle.ads_field_of_view, 0.0F) || rifle.ads_field_of_view >= std::numbers::pi_v<float>) {
    return Invalid("rifle.ads_field_of_view");
  }
  if (rifle.recoil_pattern.size() > protocol::kMaxRecoilKicks) {
    return Invalid("rifle.recoil_pattern");
  }
  for (const RecoilKick& kick : rifle.recoil_pattern) {
    if (!std::isfinite(kick.pitch) || !std::isfinite(kick.yaw)) {
      return Invalid("rifle.recoil_pattern");
    }
  }
  if (rifle.magazine_capacity < 1) {
    return Invalid("rifle.magazine_capacity");
  }
  return {};
}

std::expected<void, InvalidParameter> ValidateAmmo(const Ammo& ammo) {
  if (!FiniteAndAtLeast(ammo.gravity, 0.0F)) {
    return Invalid("ammo.gravity");
  }
  if (!FiniteAndAbove(ammo.max_range, 0.0F)) {
    return Invalid("ammo.max_range");
  }
  if (!FiniteAndAtLeast(ammo.damage.head, 0.0F)) {
    return Invalid("ammo.damage.head");
  }
  if (!FiniteAndAtLeast(ammo.damage.torso, 0.0F)) {
    return Invalid("ammo.damage.torso");
  }
  if (!FiniteAndAtLeast(ammo.damage.limb, 0.0F)) {
    return Invalid("ammo.damage.limb");
  }
  return {};
}

}  // namespace

std::expected<void, InvalidParameter> Validate(const Parameters& parameters) {
  if (parameters.player_count < 1 || parameters.player_count > protocol::kMaxPlayers) {
    return Invalid("player_count");
  }
  if (auto valid = ValidateStamina(parameters.stamina); !valid) {
    return valid;
  }
  if (auto valid = ValidateRifle(parameters.rifle); !valid) {
    return valid;
  }
  if (auto valid = ValidateAmmo(parameters.ammo); !valid) {
    return valid;
  }
  if (!FiniteAndAbove(parameters.starting_health, 0.0F)) {
    return Invalid("starting_health");
  }
  return {};
}

bool IsValidTickRate(std::uint8_t tick_rate_hz) { return tick_rate_hz > 0; }

}  // namespace augusta::parameters
