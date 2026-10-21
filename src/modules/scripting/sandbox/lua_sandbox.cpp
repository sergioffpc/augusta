#include "augusta/lua_sandbox.h"

#include <cstddef>
#include <cstdlib>

#include <sol/sol.hpp>

namespace augusta::scripting {
namespace {

// The bytes one Lua state holds, and whether kMemoryLimit bounds them now.
struct MemoryBudget {
  std::size_t in_use = 0;
  bool limited = true;
};

MemoryBudget& BudgetOf(lua_State* lua) {
  void* budget = nullptr;
  lua_getallocf(lua, &budget);
  return *static_cast<MemoryBudget*>(budget);
}

void StopAtTheInstructionLimit(lua_State* state, lua_Debug* /*debug*/) {
  luaL_error(state, "instruction limit exceeded");
}

// setmetatable as the sandbox has it: Lua's own, its upvalue, refusing a
// metatable that holds __gc. Lua runs a finalizer with debug hooks off, so the
// instruction limit never stops one, and at a GC step the script cannot see,
// maybe while the memory limit is lifted. It marks an object for finalization
// only if the metatable holds __gc as it is set, so a __gc added after
// finalizes nothing, and the raw field is the one to check. Its arguments are
// checked here, as Lua's own checks them, so that an error names setmetatable:
// the one it calls is no global, and would be named '?'.
int SetMetatableWithoutFinalizers(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);
  luaL_argexpected(state, lua_type(state, 2) == LUA_TNIL || lua_type(state, 2) == LUA_TTABLE, 2, "nil or table");
  if (lua_type(state, 2) == LUA_TTABLE) {
    lua_pushliteral(state, "__gc");
    const bool finalizes = lua_rawget(state, 2) != LUA_TNIL;
    lua_pop(state, 1);
    if (finalizes) {
      return luaL_argerror(state, 2, "a metatable with __gc: the sandbox runs no finalizers");
    }
  }
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  lua_call(state, lua_gettop(state) - 1, 1);
  return 1;
}

// Lua's allocator (lua_Alloc), refusing, while the limit holds, a block that
// would take the state past kMemoryLimit. Lua raises a refusal as a "not enough
// memory" error, which ends the call as any error does. A block that shrinks is
// never refused: Lua assumes that cannot fail.
//
// It owns the budget, which nothing else can outlive the state for: the state's
// first block is its own, and lua_close frees it after every other (Lua's
// close_state), so the budget is deleted when that block is freed, or when it
// could not be had and there is no state.
void* AllocateWithinTheLimit(void* user_data, void* block, std::size_t old_size, std::size_t new_size) {
  auto* budget = static_cast<MemoryBudget*>(user_data);
  // With no block, old_size is the kind of object Lua is making, not a size.
  const std::size_t held = block == nullptr ? 0 : old_size;
  if (new_size == 0) {
    std::free(block);
    budget->in_use -= held;
    if (block != nullptr && budget->in_use == 0) {
      delete budget;
    }
    return nullptr;
  }
  if (budget->limited && new_size > held && budget->in_use - held + new_size > kMemoryLimit) {
    return nullptr;
  }
  void* resized = std::realloc(block, new_size);
  if (resized != nullptr) {
    budget->in_use = budget->in_use - held + new_size;
  } else if (budget->in_use == 0) {
    delete budget;
  }
  return resized;
}

}  // namespace

sol::state MakeSandbox() {
  sol::state lua(sol::default_at_panic, AllocateWithinTheLimit, new MemoryBudget);
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
  for (const char* name : {"dofile", "loadfile", "load", "print", "warn", "collectgarbage", "pcall", "xpcall"}) {
    lua[name] = sol::lua_nil;
  }
  for (const char* name : {"random", "randomseed"}) {
    lua["math"][name] = sol::lua_nil;
  }
  lua_State* state = lua.lua_state();
  lua_getglobal(state, "setmetatable");
  lua_pushcclosure(state, SetMetatableWithoutFinalizers, 1);
  lua_setglobal(state, "setmetatable");
  return lua;
}

void LimitInstructions(sol::state& lua, int instructions) {
  lua_sethook(lua.lua_state(), StopAtTheInstructionLimit, LUA_MASKCOUNT, instructions);
}

MemoryLimitScope::MemoryLimitScope(sol::state& lua, bool limited)
    : lua_(lua.lua_state()), was_limited_(BudgetOf(lua_).limited) {
  BudgetOf(lua_).limited = limited;
}

MemoryLimitScope::~MemoryLimitScope() { BudgetOf(lua_).limited = was_limited_; }

}  // namespace augusta::scripting
