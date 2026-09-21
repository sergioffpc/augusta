#ifndef AUGUSTA_PARAMETERS_H_
#define AUGUSTA_PARAMETERS_H_

#include <cstdint>
#include <expected>
#include <string_view>

#include "augusta/physics.h"

// augusta::parameters is the type of the simulation's data-driven
// configuration (ADR-0039, CONTEXT.md's Parameters). It is shared because the
// server decides these values and every client ticks and predicts with them, so
// both sides carry the same struct and judge it with the same rules; where the
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

/// The number of the parameters a server starts on; each accepted reload takes the next.
inline constexpr std::uint32_t kFirstGeneration = 1;

/// Parameters and the generation they are: what a reload replaces, and what a
/// client holds and is sent.
struct NumberedParameters {
  std::uint32_t generation = 0;
  Parameters parameters{};
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

/// Whether candidate keeps the tick rate of held. It is fixed for the life of
/// the server process: every command, acknowledgement and stretch of history is
/// counted in ticks, so no reload may change it.
[[nodiscard]] bool KeepsTickRate(const Parameters& held, const Parameters& candidate);

/// Why a client may not replace the parameters it holds with those it was sent.
enum class ReplacementRefusal {
  /// The generation is not newer than the one held (a late or repeated message).
  kNotNewer,
  /// A value fails Validate; see ReplacementError::parameter.
  kInvalid,
  /// The tick rate is not the one held, so it is a damaged message and not a reload.
  kTickRateChanged,
};

/// A refusal and, for kInvalid, the parameter that is wrong.
struct ReplacementError {
  ReplacementRefusal reason{};
  std::string_view parameter{};
};

/// Whether a client holding held may take candidate: a newer generation, on
/// values that pass Validate, at the tick rate held. Checked in that order.
[[nodiscard]] std::expected<void, ReplacementError> CheckReplacement(const NumberedParameters& held,
                                                                     const NumberedParameters& candidate);

}  // namespace augusta::parameters

#endif  // AUGUSTA_PARAMETERS_H_
