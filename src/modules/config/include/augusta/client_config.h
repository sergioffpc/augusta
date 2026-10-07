#ifndef AUGUSTA_CLIENT_CONFIG_H_
#define AUGUSTA_CLIENT_CONFIG_H_

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "augusta/config.h"
#include "augusta/input.h"

/// \file
/// The client's startup settings, augustac.yaml, read under augusta::config's
/// rules (ADR-0034). Client-only: it binds the player's controls, so it is the
/// one config that depends on client Input (see augusta/server_config.h for
/// the server's).
namespace augusta::config {

/// The client's default config file, looked up next to augustac.
inline constexpr std::string_view kClientConfigFileName = "augustac.yaml";

/// Default server the client connects to (ARCHITECTURE.md §3: direct IP:port).
inline constexpr std::string_view kDefaultServerAddress = "127.0.0.1:27015";

/// What augustac.yaml holds. Its required key `base_dir` is where the relative
/// paths below start from; it is applied, not kept.
struct ClientConfig {
  /// Key `content.pack` (required): the client pack to load.
  std::filesystem::path pack_path;
  /// Key `content.public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `player.character` (required): the character to play, by its name
  /// in the scenario's manifest (e.g. "soldier"). The server admits
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
  /// An `input.keys` entry's error is kUnknownControl, kInvalidKeyName,
  /// kKeyBoundTwice or kReservedKey, its subject the entry (`input.keys.jump`).
  input::Config input{};
};

/// Parses a client config from yaml_text. A relative `base_dir` key is resolved
/// against base_dir (the file's directory), and the other relative paths
/// against the key. The error's subject is the key that is wrong.
std::expected<ClientConfig, ConfigError> ParseClientConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir);

/// Reads and parses the client config at file; its `base_dir` is relative to
/// file's directory. Errors carry file.
std::expected<ClientConfig, ConfigError> LoadClientConfig(const std::filesystem::path& file);

/// DescribeConfigError's message, naming what a binding may use: the controls
/// an unknown one is not, and the key a reserved one is.
std::string DescribeClientConfigError(const ConfigError& error);

}  // namespace augusta::config

#endif  // AUGUSTA_CLIENT_CONFIG_H_
