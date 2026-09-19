#include "augusta/scripting.h"

namespace augusta::scripting {

// TODO(sergioffpc): every method below is a placeholder - neither Lua
// nor sol2 are embedded yet (ADR-0022). Just enough is defined here for
// callers to construct/link against this module.

Engine::Engine([[maybe_unused]] const std::string& script_path) {
  // TODO(sergioffpc): load script_path into a sandboxed Lua state.
}

void Engine::RunHook([[maybe_unused]] const std::string& hook_name) {
  // TODO(sergioffpc): invoke the Lua function registered for hook_name,
  // if any.
}

}  // namespace augusta::scripting
