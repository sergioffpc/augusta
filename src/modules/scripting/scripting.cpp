#include "augusta/scripting.h"

#include <string>

namespace augusta::scripting {

// TODO(sergioffpc): every method below is a placeholder - neither Lua
// nor sol2 are embedded yet (ADR-0022). Just enough is defined here for
// callers to construct/link against this module.

void Engine::RunHook([[maybe_unused]] const std::string& hook_name) {
  // TODO(sergioffpc): invoke the Lua function registered for hook_name,
  // if any.
}

}  // namespace augusta::scripting
