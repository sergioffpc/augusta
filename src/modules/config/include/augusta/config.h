#ifndef AUGUSTA_CONFIG_H_
#define AUGUSTA_CONFIG_H_

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

// augusta::config reads the client's and the server's startup settings from a
// YAML file (ADR-0034) instead of a list of command-line arguments. By default
// each executable reads one fixed-name file from its own directory; the only
// argument, `--config <file>`, points it at another. Shared by both (ADR-0006).
//
// The file is a flat mapping of keys to strings; an unknown key, a missing
// required key or a non-string value is an error, so a typo never silently
// falls back to a default. Relative paths in the file start from the required
// key `base_dir`, never the working directory, so the executable starts the
// same from anywhere; a relative `base_dir` is itself relative to the file's
// own directory (`base_dir: .` means the file's directory).
namespace augusta::config {

/// The client's default config file, looked up next to augustac.
inline constexpr std::string_view kClientConfigFileName = "augustac.yaml";
/// The server's default config file, looked up next to augustad.
inline constexpr std::string_view kServerConfigFileName = "augustad.yaml";

/// Default server the client connects to (ARCHITECTURE.md §3: direct IP:port).
inline constexpr std::string_view kDefaultServerAddress = "127.0.0.1:27015";
/// Default address the server listens on.
inline constexpr std::string_view kDefaultListenAddress = "0.0.0.0:27015";

/// What augustac.yaml holds. Its required key `base_dir` is where the relative
/// paths below start from; it is applied, not kept.
struct ClientConfig {
  /// Key `pack` (required): the client pack to load.
  std::filesystem::path pack_path;
  /// Key `public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `server_address`: the server to connect to.
  std::string server_address{kDefaultServerAddress};
};

/// What augustad.yaml holds. Its required key `base_dir` is where the relative
/// paths below start from; it is applied, not kept.
struct ServerConfig {
  /// Key `pack` (required): the server pack to load.
  std::filesystem::path pack_path;
  /// Key `public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `listen_address`: the local address to listen on.
  std::string listen_address{kDefaultListenAddress};
};

/// Why reading the command line or a config file failed.
enum class ConfigErrorCode {
  /// The command line is not empty or `--config <file>`; subject is the usage message.
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
  /// A key is not one this config has; subject is the key.
  kUnknownKey,
  /// A key appears more than once; subject is the key.
  kDuplicateKey,
  /// A value is not a string; subject is its key.
  kNonStringValue,
  /// A required key is absent; subject is the key.
  kMissingKey,
  /// A required key's value is empty; subject is the key.
  kEmptyValue,
};

/// A failure to read the command line or a config file: what went wrong (code)
/// and what it is about (subject and file, empty when the code has none).
struct ConfigError {
  ConfigErrorCode code;
  /// The key, message or usage text the code's documentation names.
  std::string subject;
  /// The config file being read, set by the Load* functions; empty from
  /// ParseClientConfig, ParseServerConfig and ResolveConfigFile.
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

/// Where the config file is, from the command line (argc/argv as main gets
/// them): the path after `--config`, taken as given (relative to the working
/// directory), or default_file_name in the running executable's directory when
/// there are no arguments. Any other arguments are a kInvalidArguments error
/// whose subject is the usage message, naming program.
std::expected<std::filesystem::path, ConfigError> ResolveConfigFile(int argc, const char* const* argv,
                                                                    std::string_view program,
                                                                    std::string_view default_file_name);

}  // namespace augusta::config

#endif  // AUGUSTA_CONFIG_H_
