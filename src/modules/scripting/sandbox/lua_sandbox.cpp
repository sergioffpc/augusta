#include "augusta/lua_sandbox.h"

#include <sol/sol.hpp>

namespace augusta::scripting {
namespace {

void StopAtTheInstructionLimit(lua_State* state, lua_Debug* /*debug*/) {
  luaL_error(state, "instruction limit exceeded");
}

}  // namespace

sol::state MakeSandbox() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
  for (const char* name : {"dofile", "loadfile", "load", "print", "collectgarbage", "pcall", "xpcall"}) {
    lua[name] = sol::lua_nil;
  }
  for (const char* name : {"random", "randomseed"}) {
    lua["math"][name] = sol::lua_nil;
  }
  return lua;
}

void LimitInstructions(sol::state& lua, int instructions) {
  lua_sethook(lua.lua_state(), StopAtTheInstructionLimit, LUA_MASKCOUNT, instructions);
}

}  // namespace augusta::scripting
