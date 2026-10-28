#include "augusta/scripting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <sol/sol.hpp>

#include "augusta/lua_sandbox.h"

namespace augusta::scripting {
namespace {

// A script's top level only defines its hooks and whatever they share, so it
// has the Parameters script's budget (ADR-0039).
constexpr int kLoadInstructionLimit = 1'000'000;

// A hook runs inside the 60 Hz tick (NFR-01): this many instructions take well
// under a millisecond, and policy that needs more is mechanism in disguise.
constexpr int kHookInstructionLimit = 100'000;

// Deeper than any view or decision a hook deals in, and shallow enough that a
// table that holds itself is refused rather than followed for ever.
constexpr int kMaxDepth = 16;

// Far more values than any answer a hook gives (the Spawn points of the most
// players are a few dozen), and few enough to convert in well under a
// millisecond. A table an answer holds at several places counts again at each,
// since each becomes a copy of its own: unbounded, a few small tables each held
// twice by the one above would make millions, outside the sandbox's limits.
constexpr std::size_t kMaxValues = 4096;

// Turns a table into a read-only view of it: reads, lengths and pairs see the
// table, a write raises an error, and its metatable cannot be read or replaced.
// Compiled before the script's top level runs, so it keeps the base functions
// it captures whatever the script does to the globals.
constexpr std::string_view kFreezeSource = R"(
  local setmetatable, next, error = setmetatable, next, error
  return function(data)
    return setmetatable({}, {
      __index = data,
      __newindex = function() error("the view a hook is handed is read-only", 2) end,
      __len = function() return #data end,
      __pairs = function() return next, data, nil end,
      __metatable = false,
    })
  end
)";

// One loaded script: its own sandboxed Lua state and the function that turns a
// table of it into a read-only view. freeze refers into lua, so it is declared
// after it and let go of first.
struct Slot {
  sol::state lua;
  sol::protected_function freeze;
};

std::unexpected<HookError> Fail(HookErrorCode code, std::string subject = {}) {
  return std::unexpected(HookError{.code = code, .subject = std::move(subject)});
}

std::expected<Slot, LoadError> LoadScript(std::string_view name, std::string_view text) {
  Slot slot{.lua = MakeSandbox(), .freeze = {}};
  slot.freeze = slot.lua.safe_script(kFreezeSource, sol::script_pass_on_error, "=freeze");
  LimitInstructions(slot.lua, kLoadInstructionLimit);
  const std::string chunk_name = "=" + std::string(name);
  const sol::protected_function_result result = slot.lua.safe_script(text, sol::script_pass_on_error, chunk_name);
  if (!result.valid()) {
    const sol::error failure = result;
    return std::unexpected(LoadError{.message = failure.what()});
  }
  return slot;
}

sol::object ToLua(Slot& slot, const Value& value);

// A whole number reaches Lua as an integer, so an ID or a tick reads and prints
// as one (11, not 11.0); any other number as a float.
sol::object ToLuaNumber(Slot& slot, double number) {
  constexpr double kIntegerLimit = 9'007'199'254'740'992.0;  // 2^53: every whole double below it is exact.
  if (std::trunc(number) == number && std::abs(number) < kIntegerLimit) {
    return sol::make_object(slot.lua, static_cast<lua_Integer>(number));
  }
  return sol::make_object(slot.lua, number);
}

// A read-only view of a table the caller fills.
template <typename Fill>
sol::object Frozen(Slot& slot, Fill fill) {
  sol::table table = slot.lua.create_table();
  fill(table);
  return slot.freeze.call<sol::object>(table);
}

sol::object ToLua(Slot& slot, const Value::Record& record) {
  return Frozen(slot, [&](sol::table& table) {
    for (const Field& field : record) {
      table.raw_set(field.key, ToLua(slot, field.value));
    }
  });
}

sol::object ToLua(Slot& slot, const Value::List& list) {
  return Frozen(slot, [&](sol::table& table) {
    for (std::size_t i = 0; i < list.size(); ++i) {
      table.raw_set(i + 1, ToLua(slot, list[i]));
    }
  });
}

sol::object ToLua(Slot& slot, const Value& value) {
  return std::visit(
      [&](const auto& data) -> sol::object {
        using Data = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<Data, std::monostate>) {
          return sol::make_object(slot.lua, sol::lua_nil);
        } else if constexpr (std::is_same_v<Data, Value::List> || std::is_same_v<Data, Value::Record>) {
          return ToLua(slot, data);
        } else if constexpr (std::is_same_v<Data, double>) {
          return ToLuaNumber(slot, data);
        } else {
          return sol::make_object(slot.lua, data);
        }
      },
      value.data);
}

std::expected<Value, HookError> ToValue(const sol::object& object, int depth, std::size_t& values_built);

// A table as a list, keyed 1 to n, or a record, keyed by strings: any other key
// is not plain data. An empty table is an empty list. values_built counts every
// value of the answer so far: each entry counts one before it is copied.
std::expected<Value, HookError> TableToValue(const sol::table& table, int depth, std::size_t& values_built) {
  if (depth >= kMaxDepth) {
    return Fail(HookErrorCode::kNotPlainData, "tables nested more than " + std::to_string(kMaxDepth) + " deep");
  }
  std::vector<std::pair<double, sol::object>> indexed;
  std::vector<std::pair<std::string, sol::object>> named;
  bool other_key = false;
  bool too_many = false;
  table.for_each([&](const sol::object& key, const sol::object& value) {
    if (too_many || ++values_built > kMaxValues) {
      too_many = true;
    } else if (key.get_type() == sol::type::string) {
      named.emplace_back(key.as<std::string>(), value);
    } else if (key.get_type() == sol::type::number) {
      indexed.emplace_back(key.as<double>(), value);
    } else {
      other_key = true;
    }
  });
  if (too_many) {
    return Fail(HookErrorCode::kNotPlainData, "more than " + std::to_string(kMaxValues) + " values in all");
  }
  if (other_key || (!indexed.empty() && !named.empty())) {
    return Fail(HookErrorCode::kNotPlainData, "a table keyed neither 1 to n nor by strings");
  }
  if (!named.empty()) {
    std::ranges::sort(named, {}, &std::pair<std::string, sol::object>::first);
    Value::Record record;
    for (const auto& [key, entry] : named) {
      auto value = ToValue(entry, depth + 1, values_built);
      if (!value) {
        return std::unexpected(value.error());
      }
      record.push_back(Field{.key = key, .value = *std::move(value)});
    }
    return Value{.data = std::move(record)};
  }
  std::ranges::sort(indexed, {}, &std::pair<double, sol::object>::first);
  Value::List list;
  for (const auto& [index, entry] : indexed) {
    if (index != static_cast<double>(list.size() + 1)) {
      return Fail(HookErrorCode::kNotPlainData, "a table keyed neither 1 to n nor by strings");
    }
    auto value = ToValue(entry, depth + 1, values_built);
    if (!value) {
      return std::unexpected(value.error());
    }
    list.push_back(*std::move(value));
  }
  return Value{.data = std::move(list)};
}

std::expected<Value, HookError> ToValue(const sol::object& object, int depth, std::size_t& values_built) {
  switch (object.get_type()) {
    case sol::type::lua_nil:
    case sol::type::none:
      return Value{};
    case sol::type::boolean:
      return Value{.data = object.as<bool>()};
    case sol::type::number:
      return Value{.data = object.as<double>()};
    case sol::type::string:
      return Value{.data = object.as<std::string>()};
    case sol::type::table:
      return TableToValue(object.as<sol::table>(), depth, values_built);
    default:
      return Fail(HookErrorCode::kNotPlainData,
                  "a " + std::string(sol::type_name(object.lua_state(), object.get_type())));
  }
}

}  // namespace

std::string DescribeHookError(const HookError& error) {
  switch (error.code) {
    case HookErrorCode::kRaised:
      return "the hook failed: " + error.subject;
    case HookErrorCode::kNotAFunction:
      return "the hook is not a function";
    case HookErrorCode::kNotPlainData:
      return "the hook returned " + error.subject + ", which is not plain data";
  }
  return "the hook failed";
}

struct Script::Impl {
  Slot slot;
};

Script::Script(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Script::~Script() = default;
Script::Script(Script&&) noexcept = default;
Script& Script::operator=(Script&&) noexcept = default;

std::expected<Script, LoadError> Script::Load(std::string_view name, std::string_view text) {
  auto slot = LoadScript(name, text);
  if (!slot) {
    return std::unexpected(slot.error());
  }
  return Script(std::make_unique<Impl>(Impl{.slot = *std::move(slot)}));
}

std::expected<Value, HookError> Script::Call(std::string_view hook, const Value::Record& view) {
  Slot* const slot = &impl_->slot;
  // The engine's own work in the state is outside a protected call, so it runs
  // with the memory limit lifted; the hook alone runs under it.
  const MemoryLimitScope unlimited(slot->lua, false);
  const sol::object function = slot->lua.globals().raw_get<sol::object>(hook);
  if (function.get_type() == sol::type::lua_nil) {
    return Value{};
  }
  if (function.get_type() != sol::type::function) {
    return Fail(HookErrorCode::kNotAFunction);
  }
  // Armed before the view is built too, so the count left over from the last
  // call cannot stop this one early.
  LimitInstructions(slot->lua, kHookInstructionLimit);
  const sol::object argument = ToLua(*slot, view);
  // With no message handler: sol2 would take the one in its global, which the
  // script may have replaced with one that runs with hooks off.
  const sol::protected_function run(slot->lua.lua_state(), function, sol::reference());
  const sol::protected_function_result result = [&] {
    const MemoryLimitScope limited(slot->lua, true);
    return run(argument);
  }();
  if (!result.valid()) {
    const sol::error failure = result;
    return Fail(HookErrorCode::kRaised, failure.what());
  }
  if (result.return_count() > 1) {
    return Fail(HookErrorCode::kNotPlainData, "more than one value");
  }
  if (result.return_count() == 0) {
    return Value{};
  }
  std::size_t values_built = 1;  // The answer itself.
  return ToValue(result.get<sol::object>(), 0, values_built);
}

}  // namespace augusta::scripting
