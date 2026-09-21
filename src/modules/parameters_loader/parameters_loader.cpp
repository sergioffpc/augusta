#include "augusta/parameters_loader.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sol/sol.hpp>

namespace augusta::parameters {
namespace {

constexpr std::string_view kTickRateKey = "tick_rate_hz";
constexpr std::string_view kStaminaKey = "stamina";
constexpr std::string_view kStaminaKeys[] = {"deplete_per_second", "regen_per_second", "forced_walk_below"};

std::unexpected<LoadError> Fail(LoadErrorCode code, std::string subject = {}) {
  return std::unexpected(LoadError{.code = code, .subject = std::move(subject)});
}

std::string KeyPath(std::string_view parent, std::string_view key) {
  return parent.empty() ? std::string(key) : std::string(parent) + "." + std::string(key);
}

// The name a key is reported under; only a string key can be one the loader knows.
std::string KeyName(const sol::object& key) {
  if (key.get_type() == sol::type::string) {
    return key.as<std::string>();
  }
  return "<" + std::string(sol::type_name(key.lua_state(), key.get_type())) + " key>";
}

// The first key of table, in name order, that is not in known. The order is the
// names' and not the table's, so the same script always reports the same key.
std::optional<LoadError> FirstUnknownKey(const sol::table& table, std::string_view parent,
                                         std::span<const std::string_view> known) {
  std::vector<std::string> unknown;
  table.for_each([&](const sol::object& key, const sol::object&) {
    const std::string name = KeyName(key);
    if (key.get_type() != sol::type::string || std::ranges::find(known, name) == known.end()) {
      unknown.push_back(name);
    }
  });
  if (unknown.empty()) {
    return std::nullopt;
  }
  return LoadError{.code = LoadErrorCode::kUnknownKey, .subject = KeyPath(parent, *std::ranges::min_element(unknown))};
}

// The number at key; whether it is one the simulation can run on is for Validate.
std::expected<float, LoadError> ReadNumber(const sol::table& table, std::string_view parent, std::string_view key) {
  const sol::object value = table.raw_get<sol::object>(key);
  if (value.get_type() == sol::type::lua_nil) {
    return Fail(LoadErrorCode::kMissingKey, KeyPath(parent, key));
  }
  if (value.get_type() != sol::type::number) {
    return Fail(LoadErrorCode::kWrongType, KeyPath(parent, key));
  }
  return static_cast<float>(value.as<double>());
}

std::expected<physics::StaminaConfig, LoadError> ReadStamina(const sol::table& root) {
  const sol::object value = root.raw_get<sol::object>(kStaminaKey);
  if (value.get_type() == sol::type::lua_nil) {
    return Fail(LoadErrorCode::kMissingKey, std::string(kStaminaKey));
  }
  if (value.get_type() != sol::type::table) {
    return Fail(LoadErrorCode::kWrongType, std::string(kStaminaKey));
  }
  const sol::table table = value.as<sol::table>();
  if (const auto unknown = FirstUnknownKey(table, kStaminaKey, kStaminaKeys)) {
    return std::unexpected(*unknown);
  }
  const auto deplete = ReadNumber(table, kStaminaKey, "deplete_per_second");
  if (!deplete) {
    return std::unexpected(deplete.error());
  }
  const auto regen = ReadNumber(table, kStaminaKey, "regen_per_second");
  if (!regen) {
    return std::unexpected(regen.error());
  }
  const auto forced_walk_below = ReadNumber(table, kStaminaKey, "forced_walk_below");
  if (!forced_walk_below) {
    return std::unexpected(forced_walk_below.error());
  }
  return physics::StaminaConfig{
      .deplete_per_second = *deplete, .regen_per_second = *regen, .forced_walk_below = *forced_walk_below};
}

// Long enough for any script that only states values and computes a few from
// each other, short enough that one that never returns is stopped in milliseconds.
constexpr int kInstructionLimit = 1'000'000;

void StopAtTheInstructionLimit(lua_State* state, lua_Debug* /*debug*/) {
  luaL_error(state, "instruction limit of %d exceeded", kInstructionLimit);
}

// A Lua state of its own (it shares no global with a policy script) holding only
// the pure libraries. There is no io, os, package, debug or coroutine, so the
// script cannot reach the filesystem, the process or the clock; the base
// functions that read files or compile more code and math's randomness are
// taken out, so loading the same script always gives the same result; and a
// script that runs on past the instruction limit is stopped with an error.
// pcall and xpcall are taken out too: the stop is an error, and a script that
// could catch it would loop on for ever.
sol::state MakeSandbox() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
  for (const char* name : {"dofile", "loadfile", "load", "print", "collectgarbage", "pcall", "xpcall"}) {
    lua[name] = sol::lua_nil;
  }
  for (const char* name : {"random", "randomseed"}) {
    lua["math"][name] = sol::lua_nil;
  }
  lua_sethook(lua.lua_state(), StopAtTheInstructionLimit, LUA_MASKCOUNT, kInstructionLimit);
  return lua;
}

}  // namespace

std::string DescribeLoadError(const LoadError& error) {
  switch (error.code) {
    case LoadErrorCode::kCannotOpenFile:
      return "cannot open the parameters script " + error.subject;
    case LoadErrorCode::kScriptError:
      return "the parameters script failed: " + error.subject;
    case LoadErrorCode::kNotATable:
      return "the parameters script must return a table";
    case LoadErrorCode::kUnknownKey:
      return "the parameters script has a key that is not a parameter: " + error.subject;
    case LoadErrorCode::kMissingKey:
      return "the parameters script lacks the key " + error.subject;
    case LoadErrorCode::kWrongType:
      return "the parameters script gives " + error.subject + " a value of the wrong type";
    case LoadErrorCode::kOutOfRange:
      return "the parameters script gives " + error.subject + " a value that is out of range";
  }
  return "the parameters script is invalid";
}

std::expected<Parameters, LoadError> Load(std::string_view script) {
  sol::state lua = MakeSandbox();

  const sol::protected_function_result result = lua.safe_script(script, sol::script_pass_on_error, "=parameters");
  if (!result.valid()) {
    const sol::error failure = result;
    return Fail(LoadErrorCode::kScriptError, failure.what());
  }
  if (result.get_type() != sol::type::table) {
    return Fail(LoadErrorCode::kNotATable);
  }
  const sol::table root = result.get<sol::table>();

  constexpr std::string_view kRootKeys[] = {kTickRateKey, kStaminaKey};
  if (const auto unknown = FirstUnknownKey(root, {}, kRootKeys)) {
    return std::unexpected(*unknown);
  }
  const auto tick_rate_hz = ReadNumber(root, {}, kTickRateKey);
  if (!tick_rate_hz) {
    return std::unexpected(tick_rate_hz.error());
  }
  const auto stamina = ReadStamina(root);
  if (!stamina) {
    return std::unexpected(stamina.error());
  }
  const Parameters parameters{.tick_rate_hz = *tick_rate_hz, .stamina = *stamina};
  if (const auto valid = Validate(parameters); !valid) {
    return Fail(LoadErrorCode::kOutOfRange, std::string(valid.error().path));
  }
  return parameters;
}

std::expected<Parameters, LoadError> LoadFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return Fail(LoadErrorCode::kCannotOpenFile, file.string());
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return Load(contents.str());
}

}  // namespace augusta::parameters
