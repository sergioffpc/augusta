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

#include "augusta/input.h"

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
namespace augusta::config {

/// The client's default config file, looked up next to augustac.
inline constexpr std::string_view kClientConfigFileName = "augustac.yaml";
/// The server's default config file, looked up next to augustad.
inline constexpr std::string_view kServerConfigFileName = "augustad.yaml";

/// Default server the client connects to (ARCHITECTURE.md §3: direct IP:port).
inline constexpr std::string_view kDefaultServerAddress = "127.0.0.1:27015";
/// Default address the server listens on.
inline constexpr std::string_view kDefaultListenAddress = "0.0.0.0:27015";
/// Default runtime floor for the console sink (ADR-0029, ADR-0036): a Debug
/// build's DEBUG heartbeat, not its per-packet TRACE.
inline constexpr std::string_view kDefaultLogLevel = "debug";

/// What augustac.yaml holds. Its required key `base_dir` is where the relative
/// paths below start from; it is applied, not kept.
struct ClientConfig {
  /// Key `content.pack` (required): the client pack to load.
  std::filesystem::path pack_path;
  /// Key `content.public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `player.character` (required): the character to play, by its path
  /// relative to `authoring/` (e.g. "characters/player"). The server admits
  /// only one of its scenario's (ADR-0042).
  std::string character;
  /// Key `network.server_address`: the server to connect to.
  std::string server_address{kDefaultServerAddress};
  /// Key `logging.level`: one of "trace", "debug", "info", "warn", "error",
  /// "critical" - the console sink's runtime floor (augusta::logging::SetLogLevel).
  /// Only lowers what the build already compiles in (AUGUSTA_LOG_ACTIVE_LEVEL);
  /// a Release build has no TRACE/DEBUG to raise it back to.
  std::string log_level{kDefaultLogLevel};
  /// Key `input.mouse_sensitivity` (a finite number above zero) and section
  /// `input.keys` (control name -> key name, e.g. `sprint: Space`), both
  /// optional: how the player's controls respond and which key triggers each.
  /// Controls the section leaves out keep their input::kDefaultKeymap key; no
  /// two controls may share a key, and none may use input::kReleaseCursorKey.
  input::Config input{};
};

/// What augustad.yaml holds. Its required key `base_dir` is where the relative
/// paths below start from; it is applied, not kept.
struct ServerConfig {
  /// Key `content.pack` (required): the server pack to load.
  std::filesystem::path pack_path;
  /// Key `content.public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `simulation.tick_rate_hz` (required): the integer rate, in Hz, at which
  /// the server simulates and every client predicts. Must be 1..255; fixed for
  /// the life of the process, and told to each client when it joins (ADR-0039).
  std::uint8_t tick_rate_hz = 0;
  /// Key `network.listen_address`: the local address to listen on.
  std::string listen_address{kDefaultListenAddress};
  /// Key `logging.level`: one of "trace", "debug", "info", "warn", "error",
  /// "critical" - the console sink's runtime floor (augusta::logging::SetLogLevel).
  /// Only lowers what the build already compiles in (AUGUSTA_LOG_ACTIVE_LEVEL);
  /// a Release build has no TRACE/DEBUG to raise it back to.
  std::string log_level{kDefaultLogLevel};
  /// Key `simulation.recording`: where to write a recording of every tick
  /// SimulationWorld runs, replacing any file there, for augusta_replay
  /// (ADR-0048). Empty, the default, records nothing.
  std::filesystem::path recording_path;
};

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
  /// An `input.keys` entry names no control input::ControlNamed knows; subject
  /// is the entry, e.g. `input.keys.jump`.
  kUnknownControl,
  /// An `input.keys` entry names no key input::KeyNamed knows; subject is the
  /// entry, e.g. `input.keys.sprint`.
  kInvalidKeyName,
  /// An `input.keys` entry binds a key another control already has; subject is the entry.
  kKeyBoundTwice,
  /// An `input.keys` entry binds input::kReleaseCursorKey; subject is the entry.
  kReservedKey,
};

/// A failure to read the command line or a config file: what went wrong (code)
/// and what it is about (subject and file, empty when the code has none).
struct ConfigError {
  ConfigErrorCode code;
  /// The key, message or usage text the code's documentation names.
  std::string subject;
  /// The config file being read, set by the Load* functions; empty from
  /// ParseClientConfig, ParseServerConfig and ParseCommandLine.
  std::filesystem::path file;
};

/// A message for error fit to print to whoever runs the process, so neither
/// executable words it on its own.
std::string DescribeConfigError(const ConfigError& error);

/// Parses a client config from yaml_text. A relative `base_dir` key is resolved
/// against base_dir (the file's directory), and the other relative paths
/// against the key. The error's subject is the key that is wrong.
std::expected<ClientConfig, ConfigError> ParseClientConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir);

/// Parses a server config from yaml_text. A relative `base_dir` key is resolved
/// against base_dir (the file's directory), and the other relative paths
/// against the key. The error's subject is the key that is wrong.
std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir);

/// Reads and parses the client config at file; its `base_dir` is relative to
/// file's directory. Errors carry file.
std::expected<ClientConfig, ConfigError> LoadClientConfig(const std::filesystem::path& file);

/// Reads and parses the server config at file; its `base_dir` is relative to
/// file's directory. Errors carry file.
std::expected<ServerConfig, ConfigError> LoadServerConfig(const std::filesystem::path& file);

// The schema mechanism the functions above read their own files with, for an
// executable outside the runtime that keeps a config file of its own under the
// same rules (ADR-0034): its keys and their meaning stay with it.

/// A config file's scalars, under their dotted paths ("network.server_address").
using ConfigValues = std::map<std::string, std::string, std::less<>>;

/// What a config file may hold, as dotted paths: its scalar keys, and its open
/// sections - sections whose entries the caller checks by name itself (the
/// client's "input.keys", whose entries are control names).
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

/// The command line, read: the action, and what the executable needs for it.
struct CommandLine {
  /// Only set for kRun.
  std::filesystem::path config_file;
  /// Only set for kShowHelp (the usage message) and kShowVersion
  /// (`<program> <version>`).
  std::string message;
  CommandLineAction action = CommandLineAction::kRun;
};

/// Reads the command line (argc/argv as main gets them). `--help` asks for the
/// usage and `--version` for version, whatever else is on it (`--help` first);
/// otherwise the config file is the path after `--config`, taken as given
/// (relative to the working directory), or default_file_name in the running
/// executable's directory when there are no arguments. Any other arguments are
/// a kInvalidArguments error whose subject is the usage message, naming
/// program.
std::expected<CommandLine, ConfigError> ParseCommandLine(int argc, const char* const* argv, std::string_view program,
                                                         std::string_view default_file_name, std::string_view version);

}  // namespace augusta::config

#endif  // AUGUSTA_CONFIG_H_
