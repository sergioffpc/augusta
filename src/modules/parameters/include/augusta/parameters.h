#ifndef AUGUSTA_PARAMETERS_H_
#define AUGUSTA_PARAMETERS_H_

#include "augusta/physics.h"

// augusta::parameters is the type of the simulation's data-driven
// configuration (ADR-0039, CONTEXT.md's Parameters). It is shared because the
// server decides these values and every client predicts with them, so both
// sides carry the same struct; where the values come from is the server's
// business and not this header's.
namespace augusta::parameters {

/// Every value client and server must agree on. Plain and immutable in use:
/// mechanism code reads it and never calls into Lua.
struct Parameters {
  /// The stamina rules every player body follows (US-05).
  physics::StaminaConfig stamina{};
};

}  // namespace augusta::parameters

#endif  // AUGUSTA_PARAMETERS_H_
