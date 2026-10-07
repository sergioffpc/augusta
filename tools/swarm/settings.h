#ifndef AUGUSTA_SWARM_SETTINGS_H_
#define AUGUSTA_SWARM_SETTINGS_H_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "augusta/config.h"

/// \file
/// augusta-swarm's startup settings, read from augusta-swarm.yaml under
/// ADR-0034's rules through augusta::config's schema mechanism: the keys, their
/// ranges and how their errors read are the tool's, kept out of the runtime's
/// config module. main.cpp reads them once, before anything starts.
namespace augusta::swarm {

/// The default settings file, looked up next to augusta-swarm.
inline constexpr std::string_view kSettingsFileName = "augusta-swarm.yaml";

/// The default seed the Scripted players' choices start from.
inline constexpr std::uint32_t kDefaultSeed = 1;

/// What augusta-swarm.yaml holds. Its required key `base_dir` is where the
/// relative paths below start from; it is applied, not kept.
struct Settings {
  /// Key `content.pack` (required): the client pack to load.
  std::filesystem::path pack_path;
  /// Key `content.public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `player.character` (required): the character every Scripted player
  /// plays, by its name in the scenario's manifest.
  std::string character;
  /// Key `network.server_address`: the server to connect to.
  std::string server_address{config::kDefaultServerAddress};
  /// Key `logging.level`: the console sink's runtime floor (see config::ClientConfig::log_level).
  std::string log_level{config::kDefaultLogLevel};
  /// Key `run.matches` (required): how many Match ends every Scripted player
  /// must see for the run to succeed, 1 to 255.
  std::uint8_t matches = 0;
  /// Key `run.timeout_seconds` (required): how long the run may take before it
  /// fails, in seconds; a finite number above zero.
  float timeout_seconds = 0.0F;
  /// Key `run.seed`: where the Scripted players' choices start from, 0 to
  /// 4294967295. The same seed makes the same choices from the same Server views.
  std::uint32_t seed = kDefaultSeed;
};

/// Parses settings from yaml_text. A relative `base_dir` key is resolved
/// against base_dir (the file's directory), and the other relative paths
/// against the key. The error's subject is the key that is wrong.
[[nodiscard]] std::expected<Settings, config::ConfigError> ParseSettings(std::string_view yaml_text,
                                                                         const std::filesystem::path& base_dir);

/// Reads and parses the settings file at file; its `base_dir` is relative to
/// file's directory. Errors carry file.
[[nodiscard]] std::expected<Settings, config::ConfigError> LoadSettings(const std::filesystem::path& file);

/// A message for error fit to print: config::DescribeConfigError's, but with
/// the range of the tool's own whole-number keys.
[[nodiscard]] std::string DescribeSettingsError(const config::ConfigError& error);

}  // namespace augusta::swarm

#endif  // AUGUSTA_SWARM_SETTINGS_H_
