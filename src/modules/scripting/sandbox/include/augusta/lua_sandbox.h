#ifndef AUGUSTA_LUA_SANDBOX_H_
#define AUGUSTA_LUA_SANDBOX_H_

#include <sol/sol.hpp>

/// \file
/// augusta::scripting's Lua sandbox (ADR-0022, ADR-0039), shared by the two
/// kinds of script the server runs: the Parameters script (server/parameters_loader)
/// and the Game policy scripts (scripting::Engine). Each gets a Lua state of its
/// own, so they share no globals. A header of its own, apart from scripting.h,
/// so sol2 reaches only the code that embeds Lua. Server-only.
namespace augusta::scripting {

/// A new Lua state holding only the pure libraries: base, math, string and
/// table. There is no io, os, package, debug or coroutine, so a script cannot
/// reach the filesystem, the process or the clock; the base functions that read
/// files or compile more code (dofile, loadfile, load, and so require), print,
/// collectgarbage and math's randomness are taken out, so the same script
/// always gives the same result. pcall and xpcall are taken out too: the instruction limit's stop is
/// an error, and a script that could catch it would run on for ever.
sol::state MakeSandbox();

/// Lets lua run instructions more Lua instructions from now, then stops what it
/// runs with an error saying the instruction limit was exceeded. Arming it again
/// starts the count over, so it is armed before each run it bounds.
void LimitInstructions(sol::state& lua, int instructions);

}  // namespace augusta::scripting

#endif  // AUGUSTA_LUA_SANDBOX_H_
