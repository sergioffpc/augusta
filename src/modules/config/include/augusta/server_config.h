#ifndef AUGUSTA_SERVER_CONFIG_H_
#define AUGUSTA_SERVER_CONFIG_H_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "augusta/config.h"

/// \file
/// The headless server's startup settings, augustad.yaml, read under
/// augusta::config's rules (ADR-0034). Server-only, and only server-owned
/// concerns: nothing here depends on client Input (see
/// augusta/client_config.h for the client's).
namespace augusta::config {

/// The server's default config file, looked up next to augustad.
inline constexpr std::string_view kServerConfigFileName = "augustad.yaml";

/// Default address the server listens on.
inline constexpr std::string_view kDefaultListenAddress = "0.0.0.0:27015";

/// Default TCP port the server's metrics endpoint listens on (ADR-0049).
inline constexpr std::uint16_t kDefaultMetricsPort = 9464;

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
  /// Key `simulation.capture`: the directory to capture every Match into, one
  /// file each (ADR-0050), created if missing. Empty, the default, captures nothing.
  std::filesystem::path capture_directory;
  /// Key `metrics.port`: the TCP port, 1..65535, the metrics endpoint serves
  /// /metrics and /livez on, on every interface (ADR-0049).
  std::uint16_t metrics_port = kDefaultMetricsPort;
};

/// Parses a server config from yaml_text. A relative `base_dir` key is resolved
/// against base_dir (the file's directory), and the other relative paths
/// against the key. The error's subject is the key that is wrong.
std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir);

/// Reads and parses the server config at file; its `base_dir` is relative to
/// file's directory. Errors carry file.
std::expected<ServerConfig, ConfigError> LoadServerConfig(const std::filesystem::path& file);

/// DescribeConfigError's message, naming the range a tick rate or metrics
/// port must lie in.
std::string DescribeServerConfigError(const ConfigError& error);

}  // namespace augusta::config

#endif  // AUGUSTA_SERVER_CONFIG_H_
