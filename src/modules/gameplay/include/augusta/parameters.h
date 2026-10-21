#ifndef AUGUSTA_PARAMETERS_H_
#define AUGUSTA_PARAMETERS_H_

#include <cstdint>
#include <expected>
#include <string_view>

#include "augusta/physics.h"
#include "augusta/weapon.h"

/// \file
/// augusta::parameters is the type of the simulation's data-driven
/// configuration (ADR-0039, CONTEXT.md's Parameters). It is shared because the
/// server decides these values and every client ticks and predicts with them, so
/// both sides carry the same struct and judge it with the same rules; where the
/// values come from is the server's business and not this header's.
namespace augusta::parameters {

/// Every tunable value client and server must agree on. Plain and immutable in
/// use: mechanism code reads it and never calls into Lua. The tick rate is not
/// one: it is fixed for the life of the server process, so it is the server's
/// startup setting (ADR-0034) and is sent to a client once, when it joins.
///
/// A default Parameters holds the least values the simulation can run on (no
/// stamina drain, a one-round magazine, one round a minute, no recoil, no
/// damage), not a tuned rifle: it is for tests and tools that need some. A
/// server never runs on the defaults, since its script must give every value
/// (ADR-0039).
struct Parameters {
  /// The stamina rules every player body follows (US-05).
  physics::StaminaConfig stamina{};
  /// The rifle every player carries.
  weapon::Rifle rifle{};
  /// The rifle's ammunition.
  weapon::Ammo ammo{};
  /// The health every player starts a Match with, above 0; it never regenerates.
  float starting_health = 1.0F;
  /// How many players a match needs to start (ADR-0043), 1 to limits::kMaxPlayers.
  std::uint8_t player_count{1};
};

/// The parameter a Parameters gets wrong.
struct InvalidParameter {
  /// Its path, as the Parameters script spells it, e.g. `stamina.regen_per_second`.
  std::string_view path;
};

/// Whether every value of parameters is one the simulation can run on: the
/// player count 1 to limits::kMaxPlayers, and then, in the order the struct
/// declares them, numbers finite and: the stamina rates 0 or more and the forced
/// walk threshold 0 or more and below 1; the fire rate, muzzle velocity and
/// magazine capacity above 0, the reload time and recoil recovery 0 or more, the
/// ADS recoil scale above 0 and at most 1, the ADS field of view above 0 and
/// below pi, and at most limits::kMaxRecoilKicks recoil kicks; the gravity 0
/// or more, the range above 0 and every damage 0 or more; the starting health
/// above 0. The first that is not is the error. The server checks what its
/// script gives and a client what its server sends, with these same rules.
[[nodiscard]] std::expected<void, InvalidParameter> Validate(const Parameters& parameters);

/// Whether tick_rate_hz is a rate the simulation can run at: 1..255 Hz.
[[nodiscard]] bool IsValidTickRate(std::uint8_t tick_rate_hz);

}  // namespace augusta::parameters

#endif  // AUGUSTA_PARAMETERS_H_
