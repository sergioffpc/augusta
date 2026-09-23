#ifndef AUGUSTA_PARAMETERS_LOADER_H_
#define AUGUSTA_PARAMETERS_LOADER_H_

#include <expected>
#include <string>
#include <string_view>

#include "augusta/parameters.h"

// augusta::parameters::Load turns the server's Parameters script (ADR-0039)
// into the validated, immutable Parameters struct. It is a pure function of
// the script text: no file, socket or clock, so it is tested with scripts as
// strings. The server reads the text out of its pack (ADR-0039) and this turns
// it into Parameters. Server-only, since a client never reads the script: it is
// sent the result (ADR-0038).
namespace augusta::parameters {

/// Why a script is not a Parameters.
enum class LoadErrorCode {
  /// The script does not compile or raises an error; subject is Lua's message.
  kScriptError,
  /// The script does not return a table.
  kNotATable,
  /// A key is not one Parameters has; subject is its path, e.g. `stamina.regen`.
  kUnknownKey,
  /// A required key is absent; subject is its path.
  kMissingKey,
  /// A value is not of the type its key takes (a number, or a whole number for a
  /// count); subject is its path.
  kWrongType,
  /// A number is not finite or is outside the range its key takes; subject is its path.
  kOutOfRange,
};

/// A failure to load a script: what went wrong (code) and what it is about
/// (subject, empty when the code has none).
struct LoadError {
  LoadErrorCode code;
  /// The key path or message the code's documentation names.
  std::string subject{};
};

/// A message for error fit to log or print, so no caller words it on its own.
std::string DescribeLoadError(const LoadError& error);

/// Runs script and reads the table it returns into Parameters. A key the script
/// lacks, does not know, or gives the wrong type or an out-of-range value is an
/// error naming its path: nothing falls back to a default, so a misspelled key
/// is never silent.
std::expected<Parameters, LoadError> Load(std::string_view script);

}  // namespace augusta::parameters

#endif  // AUGUSTA_PARAMETERS_LOADER_H_
