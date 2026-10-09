#ifndef AUGUSTA_CONFIG_H_
#define AUGUSTA_CONFIG_H_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

/// \file
/// augusta::config reads the client's and the server's startup settings from a
/// YAML file (ADR-0034) instead of a list of command-line arguments. By default
/// each executable reads one fixed-name file from its own directory;
/// `--config <file>` points it at another, and `--help` and `--version` are the
/// only other arguments. Shared by both (ADR-0006).
///
/// The file groups its keys into sections (`content`, `network`, `logging`,
/// ...), each a mapping; a key is named by its dotted path
/// (`network.server_address`), and that path is what an error's subject names.
/// Values are strings; an unknown key or section, a missing required key, a
/// non-string value or a section that is not a mapping is an error, so a typo
/// never silently falls back to a default. Relative paths in the file start
/// from the required top-level key `base_dir`, never the working directory, so
/// the executable starts the same from anywhere; a relative `base_dir` is itself
/// relative to the file's own directory (`base_dir: .` means the file's
/// directory).
///
/// This header is the mechanism both share, with no side's keys in it: each
/// executable's own file is its own module's - augusta/client_config.h
/// (client-only, with the player's controls) and augusta/server_config.h
/// (server-only) - so the headless server never depends on client Input.
namespace augusta::config {

/// Default runtime floor for the console sink (ADR-0029, ADR-0036): a Debug
/// build's DEBUG heartbeat, not its per-packet TRACE.
inline constexpr std::string_view kDefaultLogLevel = "debug";

/// Why reading the command line or a config file failed.
enum class ConfigErrorCode {
  /// The command line is not empty, `--config <file>`, `--help` or `--version`;
  /// subject is the usage message.
  kInvalidArguments,
  /// The running executable's directory is unknown, so the default file can't be
  /// found; subject is the default file name.
  kExecutableDirectoryUnknown,
  /// The config file can't be opened; the file field names it.
  kCannotOpenFile,
  /// The text is not YAML; subject is the parser's message.
  kInvalidYaml,
  /// The top level is not a mapping.
  kNotAMapping,
  /// A key is not a plain string.
  kNonStringKey,
  /// A key or section is not one this config has; subject is its dotted path.
  kUnknownKey,
  /// A key appears more than once; subject is the key.
  kDuplicateKey,
  /// A value is not a string; subject is its key.
  kNonStringValue,
  /// A required key is absent; subject is the key.
  kMissingKey,
  /// A required key's value is empty; subject is the key.
  kEmptyValue,
  /// A key's value is not a finite number above zero; subject is the key.
  kInvalidNumber,
  /// A `logging.level` value is not one augusta::logging::ParseSeverity accepts;
  /// subject is the key.
  kInvalidLogLevel,
  /// A section's value is not a mapping (an empty one is: it sets nothing);
  /// subject is the section.
  kNotASection,
  /// A value the config's own rules reject, beyond what the mechanism checks
  /// (e.g. an open-section entry naming something that config does not know);
  /// subject is the key or entry, and reason, worded by that config, says why.
  kInvalidEntry,
};

/// A failure to read the command line or a config file: what went wrong (code)
/// and what it is about (subject and file, empty when the code has none).
struct ConfigError {
  ConfigErrorCode code;
  /// The key, message or usage text the code's documentation names.
  std::string subject;
  /// Only for kInvalidEntry: why the config rejected subject, as a phrase that
  /// follows it (e.g. "names no control; the controls are ...").
  std::string reason;
  /// The config file being read, set by LoadConfigFile; empty from the Parse*
  /// functions.
  std::filesystem::path file;
};

/// A message for error fit to print to whoever runs the process, so neither
/// executable words it on its own. What it knows of a code is the mechanism's
/// alone, and a kInvalidEntry's reason: a config whose keys say more of
/// another code (a number's range) words that error itself, through the
/// overload below.
std::string DescribeConfigError(const ConfigError& error);

/// DescribeConfigError's message with phrase in place of the code's own: error's
/// file, if any, then phrase.
std::string DescribeConfigError(const ConfigError& error, std::string_view phrase);

// The schema mechanism every config file is read with - the client's, the
// server's, and any executable outside the runtime that keeps one of its own
// under the same rules (ADR-0034): its keys and their meaning stay with it.

/// A config file's scalars, under their dotted paths ("network.server_address").
using ConfigValues = std::map<std::string, std::string, std::less<>>;

/// What a config file may hold, as dotted paths: its scalar keys, and its open
/// sections - sections whose entries the caller checks by name itself (e.g. a
/// key-binding section, whose entries are control names).
struct ConfigSchema {
  std::span<const std::string_view> keys;
  std::span<const std::string_view> open_sections;
};

/// Reads yaml_text as a mapping, its sections flattened into dotted paths:
/// every scalar must be a key or open-section entry of schema and every
/// mapping one of its sections. Which keys are required is the caller's.
std::expected<ConfigValues, ConfigError> ReadConfigValues(std::string_view yaml_text, const ConfigSchema& schema);

/// The text of file; kCannotOpenFile, with file set, if it can't be read.
std::expected<std::string, ConfigError> ReadConfigFile(const std::filesystem::path& file);

/// Reads file and parses its text with parse(text, file's directory), which
/// returns std::expected<Config, ConfigError>; a relative `base_dir` in it is
/// thus relative to file's directory. Errors carry file.
template <typename Config, typename Parse>
std::expected<Config, ConfigError> LoadConfigFile(const std::filesystem::path& file, Parse parse) {
  const auto text = ReadConfigFile(file);
  if (!text) {
    return std::unexpected(text.error());
  }
  std::expected<Config, ConfigError> config = parse(*text, file.parent_path());
  if (!config) {
    config.error().file = file;
  }
  return config;
}

/// key's value: kMissingKey if values lacks it, kEmptyValue if it is empty.
std::expected<std::string, ConfigError> RequireString(const ConfigValues& values, std::string_view key);

/// key's value as a path, read as UTF-8: as it is if absolute, else under base_dir.
std::expected<std::filesystem::path, ConfigError> RequirePath(const ConfigValues& values, std::string_view key,
                                                              const std::filesystem::path& base_dir);

/// key's value as a finite number above zero, in plain decimal or exponent
/// notation; kInvalidNumber otherwise.
std::expected<float, ConfigError> RequirePositiveNumber(const ConfigValues& values, std::string_view key);

/// key's value as a whole number from min to max, in plain decimal;
/// kInvalidNumber otherwise. DescribeConfigError knows only the runtime's own
/// ranges, so a caller with others words that error itself.
std::expected<std::uint32_t, ConfigError> RequireWholeNumber(const ConfigValues& values, std::string_view key,
                                                             std::uint32_t min, std::uint32_t max);

/// key's value, or fallback if values lacks it.
std::string OptionalString(const ConfigValues& values, std::string_view key, std::string_view fallback);

/// key's value, or fallback if values lacks it; kInvalidLogLevel if it is not
/// one augusta::logging::ParseSeverity accepts.
std::expected<std::string, ConfigError> OptionalLogLevel(const ConfigValues& values, std::string_view key,
                                                         std::string_view fallback);

/// What the command line asks the executable to do.
enum class CommandLineAction : std::uint8_t {
  /// Start, reading config_file.
  kRun,
  /// Print message (the usage) to stdout and exit successfully.
  kShowHelp,
  /// Print message (the version) to stdout and exit successfully.
  kShowVersion,
};

/// An option one executable's command line takes beyond `--config`, `--help`
/// and `--version`: what is chosen for one run, not kept as a setting
/// (ADR-0034), such as augustac's `--replay <capture>` (ADR-0051).
struct CommandLineOption {
  /// Its name, without the leading `--`.
  std::string_view name;
  /// What its value is called in the usage, or empty for an option with none.
  std::string_view value;
  /// What it does, for the usage.
  std::string_view description;
};

/// The command line, read: the action, and what the executable needs for it.
struct CommandLine {
  /// Only set for kRun.
  std::filesystem::path config_file;
  /// Only set for kShowHelp (the usage message) and kShowVersion
  /// (`<program> <version>`).
  std::string message;
  /// Only set for kRun: each CommandLineOption given, by name, with its value,
  /// empty for one that takes none. What they mean together is the
  /// executable's to decide.
  std::map<std::string, std::string, std::less<>> options{};
  CommandLineAction action = CommandLineAction::kRun;
};

/// Reads the command line (argc/argv as main gets them). `--help` asks for the
/// usage and `--version` for version, whatever else is on it (`--help` first);
/// otherwise the config file is the path after `--config`, taken as given
/// (relative to the working directory), or default_file_name in the running
/// executable's directory when there are no arguments. options are those the
/// executable takes beyond these, each at most once. Any other arguments are a
/// kInvalidArguments error whose subject is the usage message, naming program
/// and each of options.
std::expected<CommandLine, ConfigError> ParseCommandLine(int argc, const char* const* argv, std::string_view program,
                                                         std::string_view default_file_name, std::string_view version,
                                                         std::span<const CommandLineOption> options = {});

}  // namespace augusta::config

#endif  // AUGUSTA_CONFIG_H_
