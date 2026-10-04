#ifndef AUGUSTA_SCRIPTING_H_
#define AUGUSTA_SCRIPTING_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// \file
/// augusta::scripting runs a scenario's Game policy (ADR-0022): its Lua scripts,
/// objectives.lua and behaviours.lua, which the server reads out of its pack at
/// startup. Each script runs in a sandbox of its own (augusta/lua_sandbox.h,
/// ADR-0039) that shares no globals with the other or with the Parameters
/// script. Server-only: game policy is exclusively server-authoritative, never
/// run by either client world.
///
/// A hook is a global function a script defines, called by name from
/// SimulationWorld (ADR-0023); a script that does not define one leaves that
/// concern to the mechanism. A hook is handed a plain read-only table built for
/// the call, never a live binding into the ECS, and acts only through the plain
/// data it returns, which the caller validates in C++. Every call runs under an
/// instruction limit, and a hook that raises an error, runs past the limit or
/// returns something that is not plain data fails the call without harming the
/// engine: the next call runs as usual. Deterministic, like the rest of the
/// simulation: the same scripts called with the same views return the same values.
namespace augusta::scripting {

/// One of a scenario's Game policy scripts (ADR-0022).
enum class Script : std::uint8_t {
  /// objectives.lua: win conditions.
  kObjectives,
  /// behaviours.lua: spawn rules and other policy.
  kBehaviours,
};

/// script's path in the scenario's server pack, which is also the name it is
/// logged under: "objectives.lua" or "behaviours.lua".
std::string_view ScriptPath(Script script);

/// The text of each Game policy script a scenario has. One it lacks has no
/// policy for its concern.
struct Scripts {
  std::optional<std::string> objectives;
  std::optional<std::string> behaviours;
};

struct Field;

/// Plain data that crosses between C++ and a hook: nil, a boolean, a number, a
/// string, a list (a Lua table keyed 1 to n) or a record (one keyed by strings).
/// What a hook is handed and what it returns are both one. A whole number a
/// hook is handed reaches it as a Lua integer, so an ID or a tick reads as one.
struct Value {
  using List = std::vector<Value>;
  /// Ordered by key, each key once.
  using Record = std::vector<Field>;
  std::variant<std::monostate, bool, double, std::string, List, Record> data;
};

/// One key of a record and its value.
struct Field {
  std::string key;
  Value value;
};

/// A script that failed to load: which one, and Lua's message.
struct LoadError {
  Script script = Script::kObjectives;
  std::string message;
};

/// A message for error fit to log or print, naming the script.
std::string DescribeLoadError(const LoadError& error);

/// Why a hook call decided nothing.
enum class HookErrorCode : std::uint8_t {
  /// The hook raised an error or ran past the instruction limit; subject is Lua's message.
  kRaised,
  /// The global the hook is named by is not a function.
  kNotAFunction,
  /// It returned more than one value, or one that is not plain data (a
  /// function, a table keyed by both or neither of 1 to n and strings, or one
  /// nested too deep); subject says which.
  kNotPlainData,
};

/// A hook call that failed: what went wrong and what it is about.
struct HookError {
  HookErrorCode code = HookErrorCode::kRaised;
  std::string subject;
};

/// A message for error fit to log.
std::string DescribeHookError(const HookError& error);

/// The Game policy of one scenario, run in SimulationWorld's
/// Scripts/Behaviours phase on the Simulation thread (ADR-0005). SimulationWorld
/// owns exactly one. Move-only: it owns its Lua states.
class Engine {
 public:
  /// No script loaded: every hook is undefined.
  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) noexcept;
  Engine& operator=(Engine&&) noexcept;

  /// Loads each script scripts holds into a sandbox of its own and runs its top
  /// level, which defines its hooks, under the instruction limit. A script that
  /// does not compile, raises an error or reaches for what the sandbox leaves
  /// out (io, os, require, math.random, ...) is the error.
  static std::expected<Engine, LoadError> Load(const Scripts& scripts);

  /// Calls hook of script with view, as a read-only table, and returns what it
  /// returned: nil when script is not loaded or does not define hook, which is
  /// then a no-op. Each call starts the instruction limit over.
  std::expected<Value, HookError> Call(Script script, std::string_view hook, const Value::Record& view);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::scripting

#endif  // AUGUSTA_SCRIPTING_H_
