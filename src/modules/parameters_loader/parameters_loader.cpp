#include "augusta/parameters_loader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sol/sol.hpp>

namespace augusta::parameters {
namespace {

constexpr std::string_view kStaminaKey = "stamina";
constexpr std::string_view kStaminaKeys[] = {"deplete_per_second", "regen_per_second", "forced_walk_below"};

std::unexpected<LoadError> Fail(LoadErrorCode code, std::string subject = {}) {
  return std::unexpected(LoadError{.code = code, .subject = std::move(subject)});
}

std::string Path(std::string_view parent, std::string_view key) {
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
  return LoadError{.code = LoadErrorCode::kUnknownKey, .subject = Path(parent, *std::ranges::min_element(unknown))};
}

// A finite number at key that is at least min and below max.
std::expected<float, LoadError> ReadNumber(const sol::table& table, std::string_view parent, std::string_view key,
                                           double min, double max) {
  const sol::object value = table.raw_get<sol::object>(key);
  if (value.get_type() == sol::type::lua_nil) {
    return Fail(LoadErrorCode::kMissingKey, Path(parent, key));
  }
  if (value.get_type() != sol::type::number) {
    return Fail(LoadErrorCode::kWrongType, Path(parent, key));
  }
  const double number = value.as<double>();
  if (!std::isfinite(number) || number < min || number >= max) {
    return Fail(LoadErrorCode::kOutOfRange, Path(parent, key));
  }
  return static_cast<float>(number);
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
  constexpr double kNoMaximum = std::numeric_limits<double>::infinity();
  const auto deplete = ReadNumber(table, kStaminaKey, "deplete_per_second", 0.0, kNoMaximum);
  if (!deplete) {
    return std::unexpected(deplete.error());
  }
  const auto regen = ReadNumber(table, kStaminaKey, "regen_per_second", 0.0, kNoMaximum);
  if (!regen) {
    return std::unexpected(regen.error());
  }
  const auto forced_walk_below = ReadNumber(table, kStaminaKey, "forced_walk_below", 0.0, 1.0);
  if (!forced_walk_below) {
    return std::unexpected(forced_walk_below.error());
  }
  return physics::StaminaConfig{
      .deplete_per_second = *deplete, .regen_per_second = *regen, .forced_walk_below = *forced_walk_below};
}

}  // namespace

std::string DescribeLoadError(const LoadError& error) {
  switch (error.code) {
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
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

  const sol::protected_function_result result = lua.safe_script(script, sol::script_pass_on_error, "=parameters");
  if (!result.valid()) {
    const sol::error failure = result;
    return Fail(LoadErrorCode::kScriptError, failure.what());
  }
  if (result.get_type() != sol::type::table) {
    return Fail(LoadErrorCode::kNotATable);
  }
  const sol::table root = result.get<sol::table>();

  constexpr std::string_view kRootKeys[] = {kStaminaKey};
  if (const auto unknown = FirstUnknownKey(root, {}, kRootKeys)) {
    return std::unexpected(*unknown);
  }
  const auto stamina = ReadStamina(root);
  if (!stamina) {
    return std::unexpected(stamina.error());
  }
  return Parameters{.stamina = *stamina};
}

}  // namespace augusta::parameters
