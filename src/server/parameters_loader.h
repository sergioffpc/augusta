#ifndef AUGUSTA_SERVER_PARAMETERS_LOADER_H_
#define AUGUSTA_SERVER_PARAMETERS_LOADER_H_

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include "augusta/parameters.h"

// augusta::server::LoadParameters turns the server's Parameters script (ADR-0039)
// into the validated, immutable Parameters struct. It is a pure function of
// the script text and the server's tick rate, which the script may read to
// check its own values against: no file, socket or clock, so it is tested with
// scripts as strings. The server reads the text out of its pack (ADR-0039) and this turns
// it into Parameters. Server-only, since a client never reads the script: it is
// sent the result (ADR-0038).
namespace augusta::server {

/// Why a script is not a Parameters.
enum class ParametersLoadErrorCode {
  /// The script does not compile or raises an error; subject is Lua's message.
  kScriptError,
  /// The script does not return a table.
  kNotATable,
  /// A key is not one Parameters has; subject is its path, e.g. `stamina.regen`.
  kUnknownKey,
  /// A required key is absent; subject is its path.
  kMissingKey,
  /// A value is not of the type its key takes (a number, a whole number for a
  /// count, a table, or a list for the recoil pattern); subject is its path.
  kWrongType,
  /// A number is not finite or is outside the range its key takes; subject is its path.
  kOutOfRange,
};

/// A failure to load a script: what went wrong (code) and what it is about
/// (subject, empty when the code has none).
struct ParametersLoadError {
  ParametersLoadErrorCode code;
  /// The key path or message the code's documentation names.
  std::string subject;
};

/// A message for error fit to log or print, so no caller words it on its own.
std::string DescribeParametersLoadError(const ParametersLoadError& error);

/// Receives each warning a script gives with Lua's warn(), as one message.
using ParametersWarningSink = std::function<void(std::string_view message)>;

/// Runs script and reads the table it returns into Parameters. The script reads
/// the server's tick rate as server.tick_rate_hz, so it can check the values
/// that depend on it, and decides itself whether one that does not fit is a
/// warning, given to on_warning, or an error() that stops it loading. A key the
/// script lacks, does not know, or gives the wrong type or an out-of-range value
/// is an error naming its path: nothing falls back to a default, so a misspelled
/// key is never silent.
std::expected<parameters::Parameters, ParametersLoadError> LoadParameters(std::string_view script,
                                                                          std::uint8_t tick_rate_hz,
                                                                          const ParametersWarningSink& on_warning = {});

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_PARAMETERS_LOADER_H_
