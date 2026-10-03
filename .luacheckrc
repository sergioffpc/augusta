-- luacheck's configuration for the scenarios' Lua scripts (ADR-0022, ADR-0039).
-- They run in the engine's sandbox (scripting::MakeSandbox, lua_sandbox.cpp),
-- not a stock Lua, so the standard library below is that sandbox's, written
-- out in full: Lua's base, math, string and table libraries, less what the
-- sandbox takes out. A global missing from it - print, pcall, load, io, os,
-- math.random... - is one a script cannot reach, so using it is a warning.
-- The engine embeds Lua 5.5; luacheck parses 5.4, which these scripts do not
-- tell apart.

stds.sandbox = {
  read_globals = {
    "_G",
    "_VERSION",
    "assert",
    "error",
    "getmetatable",
    "ipairs",
    "next",
    "pairs",
    "rawequal",
    "rawget",
    "rawlen",
    "rawset",
    "select",
    "setmetatable",
    "tonumber",
    "tostring",
    "type",
    "warn",
    math = {
      fields = {
        "abs",
        "acos",
        "asin",
        "atan",
        "ceil",
        "cos",
        "deg",
        "exp",
        "floor",
        "fmod",
        "huge",
        "log",
        "max",
        "maxinteger",
        "min",
        "mininteger",
        "modf",
        "pi",
        "rad",
        "sin",
        "sqrt",
        "tan",
        "tointeger",
        "type",
        "ult",
      },
    },
    string = {
      fields = {
        "byte",
        "char",
        "dump",
        "find",
        "format",
        "gmatch",
        "gsub",
        "len",
        "lower",
        "match",
        "pack",
        "packsize",
        "rep",
        "reverse",
        "sub",
        "unpack",
        "upper",
      },
    },
    table = {
      fields = { "concat", "insert", "move", "pack", "remove", "sort", "unpack" },
    },
  },
}

std = "sandbox"

-- The Parameters script reads the server it is loaded for (ADR-0039).
files["**/parameters.lua"] = {
  read_globals = {
    server = { fields = { "tick_rate_hz" } },
  },
}

-- The hooks each Game policy script defines and SimulationWorld calls (ADR-0022).
files["**/objectives.lua"] = { globals = { "on_tick" } }
files["**/behaviours.lua"] = { globals = { "assign_spawns" } }
