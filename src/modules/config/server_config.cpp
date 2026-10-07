#include "augusta/server_config.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/config.h"

namespace augusta::config {

namespace {

constexpr std::string_view kTickRateKey = "simulation.tick_rate_hz";
constexpr std::string_view kMetricsPortKey = "metrics.port";

std::expected<std::uint8_t, ConfigError> RequireTickRate(const ConfigValues& values) {
  return RequireWholeNumber(values, kTickRateKey, 1, std::numeric_limits<std::uint8_t>::max())
      .transform([](std::uint32_t rate) { return static_cast<std::uint8_t>(rate); });
}

// The fallback when absent; when present, a TCP port from 1 to 65535.
std::expected<std::uint16_t, ConfigError> OptionalMetricsPort(const ConfigValues& values) {
  if (!values.contains(kMetricsPortKey)) {
    return kDefaultMetricsPort;
  }
  return RequireWholeNumber(values, kMetricsPortKey, 1, std::numeric_limits<std::uint16_t>::max())
      .transform([](std::uint32_t port) { return static_cast<std::uint16_t>(port); });
}

}  // namespace

std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 8> kKeys{
      "base_dir",      "content.pack",         "content.public_key",
      kTickRateKey,    "simulation.recording", "network.listen_address",
      "logging.level", kMetricsPortKey,
  };
  const auto values = ReadConfigValues(yaml_text, ConfigSchema{.keys = kKeys, .open_sections = {}});
  if (!values) {
    return std::unexpected(values.error());
  }
  // The file's own directory only anchors a relative base_dir; every other
  // relative path starts from base_dir.
  const auto root = RequirePath(*values, "base_dir", base_dir);
  if (!root) {
    return std::unexpected(root.error());
  }
  auto pack_path = RequirePath(*values, "content.pack", *root);
  if (!pack_path) {
    return std::unexpected(pack_path.error());
  }
  auto public_key_path = RequirePath(*values, "content.public_key", *root);
  if (!public_key_path) {
    return std::unexpected(public_key_path.error());
  }
  const auto tick_rate_hz = RequireTickRate(*values);
  if (!tick_rate_hz) {
    return std::unexpected(tick_rate_hz.error());
  }
  auto log_level = OptionalLogLevel(*values, "logging.level", kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  std::filesystem::path recording_path;
  if (values->contains("simulation.recording")) {
    auto path = RequirePath(*values, "simulation.recording", *root);
    if (!path) {
      return std::unexpected(path.error());
    }
    recording_path = *std::move(path);
  }
  const auto metrics_port = OptionalMetricsPort(*values);
  if (!metrics_port) {
    return std::unexpected(metrics_port.error());
  }
  return ServerConfig{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .tick_rate_hz = *tick_rate_hz,
      .listen_address = OptionalString(*values, "network.listen_address", kDefaultListenAddress),
      .log_level = *std::move(log_level),
      .recording_path = std::move(recording_path),
      .metrics_port = *metrics_port,
  };
}

std::expected<ServerConfig, ConfigError> LoadServerConfig(const std::filesystem::path& file) {
  return LoadConfigFile<ServerConfig>(file, ParseServerConfig);
}

std::string DescribeServerConfigError(const ConfigError& error) {
  if (error.code != ConfigErrorCode::kInvalidNumber) {
    return DescribeConfigError(error);
  }
  if (error.subject == kTickRateKey) {
    return DescribeConfigError(error, std::format("'{}' must be an integer from 1 to 255", error.subject));
  }
  if (error.subject == kMetricsPortKey) {
    return DescribeConfigError(error, std::format("'{}' must be an integer from 1 to 65535", error.subject));
  }
  return DescribeConfigError(error);
}

}  // namespace augusta::config
