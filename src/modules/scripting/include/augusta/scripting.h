#ifndef AUGUSTA_SCRIPTING_H_
#define AUGUSTA_SCRIPTING_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// \file
/// augusta::scripting is the sandboxed Lua embedding (ADR-0022): a script
/// loaded into a sandbox of its own (augusta/lua_sandbox.h, ADR-0039), sharing
/// no globals with any other, whose global functions are called by name. It
/// knows nothing of what a script decides: Game policy (augusta/policy.h) gives
/// its script its meaning. Server-only.
///
/// A hook is a global function a script defines, called by name; a script that
/// does not define one answers nil. A hook is handed a plain read-only table
/// built for the call, never a live binding into the engine, and acts only
/// through the plain data it returns, which the caller validates in C++. Every
/// call runs under an instruction limit, and a hook that raises an error, runs
/// past the limit or returns something that is not plain data fails the call
/// without harming the engine: the next call runs as usual. Deterministic: the
/// same script called with the same views returns the same values.
namespace augusta::scripting {

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

/// A script that failed to load: Lua's message, which names the script.
struct LoadError {
  std::string message;
};

/// Why a hook call decided nothing.
enum class HookErrorCode : std::uint8_t {
  /// The hook raised an error or ran past the instruction limit; subject is Lua's message.
  kRaised,
  /// The global the hook is named by is not a function.
  kNotAFunction,
  /// It returned more than one value, or one that is not plain data (a
  /// function, a table keyed by both or neither of 1 to n and strings, one
  /// nested too deep, or one of too many values in all, counting a table it
  /// holds at several places once at each); subject says which.
  kNotPlainData,
};

/// A hook call that failed: what went wrong and what it is about.
struct HookError {
  HookErrorCode code = HookErrorCode::kRaised;
  std::string subject;
};

/// A message for error fit to log.
std::string DescribeHookError(const HookError& error);

/// One loaded script in a Lua state of its own. Move-only: it owns its Lua
/// state.
class Script {
 public:
  ~Script();
  Script(const Script&) = delete;
  Script& operator=(const Script&) = delete;
  Script(Script&&) noexcept;
  Script& operator=(Script&&) noexcept;

  /// Loads text into a sandbox, under name (what Lua's messages call it), and
  /// runs its top level, which defines its hooks, under the instruction limit.
  /// A script that does not compile, raises an error or reaches for what the
  /// sandbox leaves out (io, os, require, math.random, ...) is the error.
  static std::expected<Script, LoadError> Load(std::string_view name, std::string_view text);

  /// Calls hook with view, as a read-only table, and returns what it returned:
  /// nil when the script does not define hook. Each call starts the instruction
  /// limit over.
  std::expected<Value, HookError> Call(std::string_view hook, const Value::Record& view);

 private:
  struct Impl;
  explicit Script(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::scripting

#endif  // AUGUSTA_SCRIPTING_H_
