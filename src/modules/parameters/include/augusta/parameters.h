#ifndef AUGUSTA_PARAMETERS_H_
#define AUGUSTA_PARAMETERS_H_

#include <expected>
#include <string_view>

#include "augusta/physics.h"

// augusta::parameters is the type of the simulation's data-driven
// configuration (ADR-0039, CONTEXT.md's Parameters). It is shared because the
// server decides these values and every client ticks and predicts with them, so
// both sides carry the same struct and check it with the same rules; where the
// values come from is the server's business and not this header's.
namespace augusta::parameters {

/// Every value client and server must agree on. Plain and immutable in use:
/// mechanism code reads it and never calls into Lua.
struct Parameters {
  /// The rate, in Hz, at which the server simulates and every client predicts.
  /// Fixed for the life of the server process. Zero means it was never set,
  /// which Validate refuses.
  float tick_rate_hz = 0.0F;
  /// The stamina rules every player body follows (US-05).
  physics::StaminaConfig stamina{};
};

/// The parameter a Parameters gets wrong.
struct InvalidParameter {
  /// Its path, as the Parameters script spells it, e.g. `stamina.regen_per_second`.
  std::string_view path;
};

/// Whether every value of parameters is one the simulation can run on: numbers
/// finite, the tick rate above zero, the stamina rates 0 or more and the forced
/// walk threshold 0 or more and below 1. The first that is not, in the order
/// the struct declares them, is the error. The server checks what its script
/// gives and a client what its server sends, with these same rules.
[[nodiscard]] std::expected<void, InvalidParameter> Validate(const Parameters& parameters);

}  // namespace augusta::parameters

#endif  // AUGUSTA_PARAMETERS_H_
