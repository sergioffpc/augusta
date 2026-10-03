#include "parameters_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

#include "augusta/lua_sandbox.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"

namespace augusta::server {
namespace {

constexpr std::string_view kPlayerCountKey = "player_count";
constexpr std::string_view kStaminaKey = "stamina";
constexpr std::string_view kRifleKey = "rifle";
constexpr std::string_view kAmmoKey = "ammo";
constexpr std::string_view kStartingHealthKey = "starting_health";
constexpr std::array<std::string_view, 5> kRootKeys{
    kPlayerCountKey, kStaminaKey, kRifleKey, kAmmoKey, kStartingHealthKey,
};
constexpr std::array<std::string_view, 3> kStaminaKeys{"deplete_per_second", "regen_per_second", "forced_walk_below"};
constexpr std::string_view kRecoilPatternKey = "recoil_pattern";
constexpr std::array<std::string_view, 8> kRifleKeys{
    "magazine_capacity", "rounds_per_minute",          "muzzle_velocity",  "reload_seconds",
    kRecoilPatternKey,   "recoil_recovery_per_second", "ads_recoil_scale", "ads_field_of_view",
};
constexpr std::array<std::string_view, 2> kRecoilKickKeys{"pitch", "yaw"};
constexpr std::string_view kDamageKey = "damage";
constexpr std::array<std::string_view, 3> kAmmoKeys{"gravity", "max_range", kDamageKey};
constexpr std::array<std::string_view, 3> kDamageKeys{"head", "torso", "limb"};

std::unexpected<ParametersLoadError> Fail(ParametersLoadErrorCode code, std::string subject = {}) {
  return std::unexpected(ParametersLoadError{.code = code, .subject = std::move(subject)});
}

std::string KeyPath(std::string_view parent, std::string_view child) {
  return parent.empty() ? std::string(child) : std::string(parent) + "." + std::string(child);
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
std::optional<ParametersLoadError> FirstUnknownKey(const sol::table& table, std::string_view parent,
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
  return ParametersLoadError{.code = ParametersLoadErrorCode::kUnknownKey,
                             .subject = KeyPath(parent, *std::ranges::min_element(unknown))};
}

// The number at key; whether it is one the simulation can run on is for parameters::Validate.
std::expected<double, ParametersLoadError> ReadNumber(const sol::table& table, std::string_view parent,
                                                      std::string_view key) {
  const sol::object value = table.raw_get<sol::object>(key);
  if (value.get_type() == sol::type::lua_nil) {
    return Fail(ParametersLoadErrorCode::kMissingKey, KeyPath(parent, key));
  }
  if (value.get_type() != sol::type::number) {
    return Fail(ParametersLoadErrorCode::kWrongType, KeyPath(parent, key));
  }
  return value.as<double>();
}

// The number at key as a float; whether it is one the simulation can run on is for parameters::Validate.
std::expected<float, ParametersLoadError> ReadFloat(const sol::table& table, std::string_view parent,
                                                    std::string_view key) {
  const auto number = ReadNumber(table, parent, key);
  if (!number) {
    return std::unexpected(number.error());
  }
  return static_cast<float>(*number);
}

// A count at key, a whole number; one the type cannot hold is out of range
// here, and whether it is a count the simulation can run on is for parameters::Validate. A
// whole number with a fraction part (8 / 4 is 2.0 in Lua) is still whole.
std::expected<std::uint8_t, ParametersLoadError> ReadCount(const sol::table& table, std::string_view parent,
                                                           std::string_view key) {
  const auto count = ReadNumber(table, parent, key);
  if (!count) {
    return std::unexpected(count.error());
  }
  if (!std::isfinite(*count) || *count < 0.0 || *count > std::numeric_limits<std::uint8_t>::max()) {
    return Fail(ParametersLoadErrorCode::kOutOfRange, KeyPath(parent, key));
  }
  if (*count != std::floor(*count)) {
    return Fail(ParametersLoadErrorCode::kWrongType, KeyPath(parent, key));
  }
  return static_cast<std::uint8_t>(*count);
}

// The table at key, holding no key but known.
std::expected<sol::table, ParametersLoadError> ReadTable(const sol::table& table, std::string_view parent,
                                                         std::string_view key,
                                                         std::span<const std::string_view> known) {
  const sol::object value = table.raw_get<sol::object>(key);
  const std::string path = KeyPath(parent, key);
  if (value.get_type() == sol::type::lua_nil) {
    return Fail(ParametersLoadErrorCode::kMissingKey, path);
  }
  if (value.get_type() != sol::type::table) {
    return Fail(ParametersLoadErrorCode::kWrongType, path);
  }
  sol::table result = value.as<sol::table>();
  if (const auto unknown = FirstUnknownKey(result, path, known)) {
    return std::unexpected(*unknown);
  }
  return result;
}

// One float of Config and the key it is read from.
template <typename Config>
struct FloatKey {
  std::string_view key;
  float Config::* field;
};

// Reads every key of fields out of table into config, in order; the first that
// is not a number is the error.
template <typename Config, std::size_t N>
std::expected<void, ParametersLoadError> ReadFloats(const sol::table& table, std::string_view parent,
                                                    const std::array<FloatKey<Config>, N>& fields, Config& config) {
  for (const FloatKey<Config>& field : fields) {
    const auto value = ReadFloat(table, parent, field.key);
    if (!value) {
      return std::unexpected(value.error());
    }
    config.*field.field = *value;
  }
  return {};
}

std::expected<physics::StaminaConfig, ParametersLoadError> ReadStamina(const sol::table& root) {
  const auto table = ReadTable(root, {}, kStaminaKey, kStaminaKeys);
  if (!table) {
    return std::unexpected(table.error());
  }
  constexpr auto kFields = std::to_array<FloatKey<physics::StaminaConfig>>({
      {.key = "deplete_per_second", .field = &physics::StaminaConfig::deplete_per_second},
      {.key = "regen_per_second", .field = &physics::StaminaConfig::regen_per_second},
      {.key = "forced_walk_below", .field = &physics::StaminaConfig::forced_walk_below},
  });
  physics::StaminaConfig stamina;
  if (const auto read = ReadFloats(*table, kStaminaKey, kFields, stamina); !read) {
    return std::unexpected(read.error());
  }
  return stamina;
}

// Whether table is a list: its keys exactly 1 to its length, in any order.
bool IsList(const sol::table& table) {
  const std::size_t length = table.size();
  std::size_t count = 0;
  bool list = true;
  table.for_each([&](const sol::object& key, const sol::object&) {
    ++count;
    if (key.get_type() != sol::type::number) {
      list = false;
      return;
    }
    const double index = key.as<double>();
    list = list && index >= 1.0 && index <= static_cast<double>(length) && index == std::floor(index);
  });
  return list && count == length;
}

// The recoil pattern at rifle.recoil_pattern: a list of {pitch, yaw} kicks. How
// many it may hold is for parameters::Validate.
std::expected<std::vector<parameters::RecoilKick>, ParametersLoadError> ReadRecoilPattern(const sol::table& rifle) {
  const std::string path = KeyPath(kRifleKey, kRecoilPatternKey);
  const sol::object value = rifle.raw_get<sol::object>(kRecoilPatternKey);
  if (value.get_type() == sol::type::lua_nil) {
    return Fail(ParametersLoadErrorCode::kMissingKey, path);
  }
  if (value.get_type() != sol::type::table || !IsList(value.as<sol::table>())) {
    return Fail(ParametersLoadErrorCode::kWrongType, path);
  }
  const sol::table list = value.as<sol::table>();
  constexpr auto kFields = std::to_array<FloatKey<parameters::RecoilKick>>({
      {.key = "pitch", .field = &parameters::RecoilKick::pitch},
      {.key = "yaw", .field = &parameters::RecoilKick::yaw},
  });
  std::vector<parameters::RecoilKick> pattern;
  for (std::size_t i = 1; i <= list.size(); ++i) {
    const std::string kick_path = path + "[" + std::to_string(i) + "]";
    const sol::object entry = list.raw_get<sol::object>(i);
    if (entry.get_type() != sol::type::table) {
      return Fail(ParametersLoadErrorCode::kWrongType, kick_path);
    }
    const sol::table kick_table = entry.as<sol::table>();
    if (const auto unknown = FirstUnknownKey(kick_table, kick_path, kRecoilKickKeys)) {
      return std::unexpected(*unknown);
    }
    parameters::RecoilKick kick;
    if (const auto read = ReadFloats(kick_table, kick_path, kFields, kick); !read) {
      return std::unexpected(read.error());
    }
    pattern.push_back(kick);
  }
  return pattern;
}

std::expected<parameters::Rifle, ParametersLoadError> ReadRifle(const sol::table& root) {
  const auto table = ReadTable(root, {}, kRifleKey, kRifleKeys);
  if (!table) {
    return std::unexpected(table.error());
  }
  parameters::Rifle rifle;
  const auto capacity = ReadCount(*table, kRifleKey, "magazine_capacity");
  if (!capacity) {
    return std::unexpected(capacity.error());
  }
  rifle.magazine_capacity = *capacity;
  constexpr auto kFiring = std::to_array<FloatKey<parameters::Rifle>>({
      {.key = "rounds_per_minute", .field = &parameters::Rifle::rounds_per_minute},
      {.key = "muzzle_velocity", .field = &parameters::Rifle::muzzle_velocity},
      {.key = "reload_seconds", .field = &parameters::Rifle::reload_seconds},
      {.key = "recoil_recovery_per_second", .field = &parameters::Rifle::recoil_recovery_per_second},
  });
  if (const auto read = ReadFloats(*table, kRifleKey, kFiring, rifle); !read) {
    return std::unexpected(read.error());
  }
  auto pattern = ReadRecoilPattern(*table);
  if (!pattern) {
    return std::unexpected(pattern.error());
  }
  rifle.recoil_pattern = *std::move(pattern);
  constexpr auto kAiming = std::to_array<FloatKey<parameters::Rifle>>({
      {.key = "ads_recoil_scale", .field = &parameters::Rifle::ads_recoil_scale},
      {.key = "ads_field_of_view", .field = &parameters::Rifle::ads_field_of_view},
  });
  if (const auto read = ReadFloats(*table, kRifleKey, kAiming, rifle); !read) {
    return std::unexpected(read.error());
  }
  return rifle;
}

std::expected<parameters::Ammo, ParametersLoadError> ReadAmmo(const sol::table& root) {
  const auto table = ReadTable(root, {}, kAmmoKey, kAmmoKeys);
  if (!table) {
    return std::unexpected(table.error());
  }
  parameters::Ammo ammo;
  constexpr auto kFlight = std::to_array<FloatKey<parameters::Ammo>>({
      {.key = "gravity", .field = &parameters::Ammo::gravity},
      {.key = "max_range", .field = &parameters::Ammo::max_range},
  });
  if (const auto read = ReadFloats(*table, kAmmoKey, kFlight, ammo); !read) {
    return std::unexpected(read.error());
  }
  const std::string damage_path = KeyPath(kAmmoKey, kDamageKey);
  const auto damage = ReadTable(*table, kAmmoKey, kDamageKey, kDamageKeys);
  if (!damage) {
    return std::unexpected(damage.error());
  }
  constexpr auto kDamage = std::to_array<FloatKey<parameters::Damage>>({
      {.key = "head", .field = &parameters::Damage::head},
      {.key = "torso", .field = &parameters::Damage::torso},
      {.key = "limb", .field = &parameters::Damage::limb},
  });
  if (const auto read = ReadFloats(*damage, damage_path, kDamage, ammo.damage); !read) {
    return std::unexpected(read.error());
  }
  return ammo;
}

// Lua's warn(), whose arguments are on state's stack: like Lua's own, it takes
// one or more strings (or numbers) and gives them as one message, but always to
// on_warning, never to stderr.
void Warn(lua_State* state, const ParametersWarningSink& on_warning) {
  const int count = lua_gettop(state);
  luaL_checkstring(state, 1);
  std::string message;
  for (int index = 1; index <= count; ++index) {
    std::size_t length = 0;
    const char* part = luaL_checklstring(state, index, &length);
    message.append(part, length);
  }
  if (on_warning) {
    on_warning(message);
  }
}

// Long enough for any script that only states values and computes a few from
// each other, short enough that one that never returns is stopped in milliseconds.
constexpr int kInstructionLimit = 1'000'000;

}  // namespace

std::string DescribeParametersLoadError(const ParametersLoadError& error) {
  switch (error.code) {
    case ParametersLoadErrorCode::kScriptError:
      return "the parameters script failed: " + error.subject;
    case ParametersLoadErrorCode::kNotATable:
      return "the parameters script must return a table";
    case ParametersLoadErrorCode::kUnknownKey:
      return "the parameters script has a key that is not a parameter: " + error.subject;
    case ParametersLoadErrorCode::kMissingKey:
      return "the parameters script lacks the key " + error.subject;
    case ParametersLoadErrorCode::kWrongType:
      return "the parameters script gives " + error.subject + " a value of the wrong type";
    case ParametersLoadErrorCode::kOutOfRange:
      return "the parameters script gives " + error.subject + " a value that is out of range";
  }
  return "the parameters script is invalid";
}

std::expected<parameters::Parameters, ParametersLoadError> LoadParameters(std::string_view script,
                                                                          std::uint8_t tick_rate_hz,
                                                                          const ParametersWarningSink& on_warning) {
  // A Lua state of its own: it shares no global with a policy script (ADR-0039).
  sol::state lua = scripting::MakeSandbox();
  // What the script may read of the server it is loaded for.
  lua["server"] = lua.create_table_with("tick_rate_hz", static_cast<int>(tick_rate_hz));
  lua.set_function("warn", [&on_warning](sol::this_state state) { Warn(state, on_warning); });
  scripting::LimitInstructions(lua, kInstructionLimit);

  const sol::protected_function_result result = lua.safe_script(script, sol::script_pass_on_error, "=parameters");
  if (!result.valid()) {
    const sol::error failure = result;
    return Fail(ParametersLoadErrorCode::kScriptError, failure.what());
  }
  if (result.get_type() != sol::type::table) {
    return Fail(ParametersLoadErrorCode::kNotATable);
  }
  const sol::table root = result.get<sol::table>();

  if (const auto unknown = FirstUnknownKey(root, {}, kRootKeys)) {
    return std::unexpected(*unknown);
  }
  const auto player_count = ReadCount(root, {}, kPlayerCountKey);
  if (!player_count) {
    return std::unexpected(player_count.error());
  }
  auto stamina = ReadStamina(root);
  if (!stamina) {
    return std::unexpected(stamina.error());
  }
  auto rifle = ReadRifle(root);
  if (!rifle) {
    return std::unexpected(rifle.error());
  }
  auto ammo = ReadAmmo(root);
  if (!ammo) {
    return std::unexpected(ammo.error());
  }
  const auto starting_health = ReadFloat(root, {}, kStartingHealthKey);
  if (!starting_health) {
    return std::unexpected(starting_health.error());
  }
  const parameters::Parameters parameters{
      .stamina = *stamina,
      .rifle = *std::move(rifle),
      .ammo = *ammo,
      .starting_health = *starting_health,
      .player_count = *player_count,
  };
  if (const auto valid = parameters::Validate(parameters); !valid) {
    return Fail(ParametersLoadErrorCode::kOutOfRange, std::string(valid.error().path));
  }
  return parameters;
}

}  // namespace augusta::server
