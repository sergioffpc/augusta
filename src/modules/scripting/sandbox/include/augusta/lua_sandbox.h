#ifndef AUGUSTA_LUA_SANDBOX_H_
#define AUGUSTA_LUA_SANDBOX_H_

#include <cstddef>

#include <sol/sol.hpp>

/// \file
/// augusta::scripting's Lua sandbox (ADR-0022, ADR-0039), shared by the two
/// kinds of script the server runs: the Parameters script (scenario/parameters_loader)
/// and the Game policy's rules (scripting::Script). Each gets a Lua state of its
/// own, so they share no globals. A header of its own, apart from scripting.h,
/// so sol2 reaches only the code that embeds Lua. Server-only.
namespace augusta::scripting {

/// What one Lua state MakeSandbox makes may hold at once, in bytes. A rules or
/// Parameters script states values and decides from a view of the Match, which
/// takes kilobytes, so this is thousands of times what one needs, and still a
/// small part of the server's container memory limit (charts/augustad), which
/// the server's own state needs.
inline constexpr std::size_t kMemoryLimit = std::size_t{32} << 20U;

/// A new Lua state holding only the pure libraries: base, math, string and
/// table. There is no io, os, package, debug or coroutine, so a script cannot
/// reach the filesystem, the process or the clock; the base functions that read
/// files or compile more code (dofile, loadfile, load, and so require), print,
/// warn, collectgarbage and math's randomness are taken out, so the same script
/// always gives the same result and writes nothing. The state holds at most
/// kMemoryLimit bytes: an allocation past that fails what runs with a "not
/// enough memory" error. pcall and xpcall are taken out too: the instruction
/// limit's stop and a refused allocation are errors, and a script that could
/// catch them would run on for ever.
sol::state MakeSandbox();

/// Lets lua run instructions more Lua instructions from now, then stops what it
/// runs with an error saying the instruction limit was exceeded. Arming it again
/// starts the count over, so it is armed before each run it bounds.
void LimitInstructions(sol::state& lua, int instructions);

/// Holds or lifts kMemoryLimit on a state MakeSandbox made while the scope
/// lives, then puts back what was there. The limit is held from the state's
/// making. The server lifts it for its own work in the state outside a
/// protected call (looking up a hook, building its view, reading its answer):
/// there Lua cannot raise a refusal as the script's error, and aborts the
/// process instead. It holds it again around the script's own run.
class MemoryLimitScope {
 public:
  MemoryLimitScope(sol::state& lua, bool limited);
  ~MemoryLimitScope();
  MemoryLimitScope(const MemoryLimitScope&) = delete;
  MemoryLimitScope& operator=(const MemoryLimitScope&) = delete;

 private:
  lua_State* lua_;
  bool was_limited_;
};

}  // namespace augusta::scripting

#endif  // AUGUSTA_LUA_SANDBOX_H_
