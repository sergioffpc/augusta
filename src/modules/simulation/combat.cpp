#include "combat.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

namespace augusta::simulation {

float MaxShootersDelayTicks(std::uint8_t tick_rate_hz) {
  return std::chrono::duration<float>(kMaxShootersDelay).count() * static_cast<float>(tick_rate_hz);
}

float ShootersDelay(tick::Tick tick, const command::Command& command, float max_delay) {
  const auto now = static_cast<double>(tick);
  const double reported =
      static_cast<double>(command.seen_tick) + static_cast<double>(std::clamp(command.seen_fraction, 0.0F, 1.0F));
  const double seen = std::min(reported, now - 1.0);
  return std::min(static_cast<float>(now - seen), max_delay);
}

float DamageFor(const body_part::Damage& damage, body_part::BodyPart part) {
  switch (part) {
    case body_part::BodyPart::kHead:
      return damage.head;
    case body_part::BodyPart::kTorso:
      return damage.torso;
    case body_part::BodyPart::kLimb:
      return damage.limb;
  }
  std::unreachable();
}

std::size_t HitboxHistoryTicks(std::uint8_t tick_rate_hz) {
  return static_cast<std::size_t>(std::chrono::ceil<std::chrono::seconds>(kMaxShootersDelay * tick_rate_hz).count());
}

}  // namespace augusta::simulation
