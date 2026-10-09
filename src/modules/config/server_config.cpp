#include "augusta/server_config.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/config.h"

namespace augusta::config {

namespace {

constexpr std::string_view kTickRateKey = "simulation.tick_rate_hz";
constexpr std::uint32_t kMaxTickRate = std::numeric_limits<std::uint8_t>::max();
constexpr std::string_view kMetricsPortKey = "metrics.port";
constexpr std::uint32_t kMaxMetricsPort = std::numeric_limits<std::uint16_t>::max();
constexpr std::string_view kRecordingModeKey = "simulation.recording_mode";
constexpr std::string_view kCaptureKey = "simulation.capture";
constexpr std::string_view kCaptureMaxFilesKey = "simulation.capture_retention.max_files";
constexpr std::string_view kCaptureMaxMibKey = "simulation.capture_retention.max_mib";
constexpr std::uint32_t kMaxCaptureLimit = std::numeric_limits<std::uint32_t>::max();
constexpr std::string_view kReenactmentsKey = "simulation.reenactments";

// Every key augustad.yaml may hold.
constexpr std::array<std::string_view, 13> kServerKeys{
    "base_dir",       "content.pack",           "content.public_key",
    kTickRateKey,     "simulation.recording",   kRecordingModeKey,
    kCaptureKey,      kCaptureMaxFilesKey,      kCaptureMaxMibKey,
    kReenactmentsKey, "network.listen_address", "logging.level",
    kMetricsPortKey,
};

std::expected<std::uint8_t, ConfigError> RequireTickRate(const ConfigValues& values) {
  return RequireWholeNumber(values, kTickRateKey, 1, kMaxTickRate).transform([](std::uint32_t rate) {
    return static_cast<std::uint8_t>(rate);
  });
}

// The fallback when absent; when present, a TCP port from 1 to 65535.
std::expected<std::uint16_t, ConfigError> OptionalMetricsPort(const ConfigValues& values) {
  if (!values.contains(kMetricsPortKey)) {
    return kDefaultMetricsPort;
  }
  return RequireWholeNumber(values, kMetricsPortKey, 1, kMaxMetricsPort).transform([](std::uint32_t port) {
    return static_cast<std::uint16_t>(port);
  });
}

// The path under key, relative to root; empty when absent.
std::expected<std::filesystem::path, ConfigError> OptionalPath(const ConfigValues& values, std::string_view key,
                                                               const std::filesystem::path& root) {
  if (!values.contains(key)) {
    return std::filesystem::path{};
  }
  return RequirePath(values, key, root);
}

// A capture retention limit: nullopt when absent; when present, at least 1.
std::expected<std::optional<std::uint32_t>, ConfigError> OptionalCaptureLimit(const ConfigValues& values,
                                                                              std::string_view key) {
  if (!values.contains(key)) {
    return std::nullopt;
  }
  return RequireWholeNumber(values, key, 1, kMaxCaptureLimit);
}

// Reads simulation.capture, relative to root, and its retention's limits into config.
std::expected<void, ConfigError> ReadCapture(const ConfigValues& values, const std::filesystem::path& root,
                                             ServerConfig& config) {
  auto directory = OptionalPath(values, kCaptureKey, root);
  if (!directory) {
    return std::unexpected(directory.error());
  }
  auto max_files = OptionalCaptureLimit(values, kCaptureMaxFilesKey);
  if (!max_files) {
    return std::unexpected(max_files.error());
  }
  auto max_mib = OptionalCaptureLimit(values, kCaptureMaxMibKey);
  if (!max_mib) {
    return std::unexpected(max_mib.error());
  }
  config.capture_directory = *std::move(directory);
  config.capture_max_files = *max_files;
  config.capture_max_mib = *max_mib;
  return {};
}

// Whether the recording is strict: "optional" when absent.
std::expected<bool, ConfigError> OptionalStrictRecording(const ConfigValues& values) {
  const std::string mode = OptionalString(values, kRecordingModeKey, "optional");
  if (mode != "optional" && mode != "strict") {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidEntry,
                                       .subject = std::string(kRecordingModeKey),
                                       .reason = "must be optional or strict",
                                       .file = {}});
  }
  return mode == "strict";
}

// Whether Reenact requests are taken: "false" when absent.
std::expected<bool, ConfigError> OptionalReenactments(const ConfigValues& values) {
  const std::string taken = OptionalString(values, kReenactmentsKey, "false");
  if (taken != "true" && taken != "false") {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidEntry,
                                       .subject = std::string(kReenactmentsKey),
                                       .reason = "must be true or false",
                                       .file = {}});
  }
  return taken == "true";
}

}  // namespace

std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  const auto values = ReadConfigValues(yaml_text, ConfigSchema{.keys = kServerKeys, .open_sections = {}});
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
  auto recording_path = OptionalPath(*values, "simulation.recording", *root);
  if (!recording_path) {
    return std::unexpected(recording_path.error());
  }
  const auto strict_recording = OptionalStrictRecording(*values);
  if (!strict_recording) {
    return std::unexpected(strict_recording.error());
  }
  const auto metrics_port = OptionalMetricsPort(*values);
  if (!metrics_port) {
    return std::unexpected(metrics_port.error());
  }
  ServerConfig config{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .tick_rate_hz = *tick_rate_hz,
      .listen_address = OptionalString(*values, "network.listen_address", kDefaultListenAddress),
      .log_level = *std::move(log_level),
      .recording_path = *std::move(recording_path),
      .strict_recording = *strict_recording,
      .capture_directory = {},
      .capture_max_files = std::nullopt,
      .capture_max_mib = std::nullopt,
      .reenactments = false,
      .metrics_port = *metrics_port,
  };
  return ReadCapture(*values, *root, config)
      .and_then([&values] { return OptionalReenactments(*values); })
      .transform([&config](bool reenactments) {
        config.reenactments = reenactments;
        return std::move(config);
      });
}

std::expected<ServerConfig, ConfigError> LoadServerConfig(const std::filesystem::path& file) {
  return LoadConfigFile<ServerConfig>(file, ParseServerConfig);
}

std::string DescribeServerConfigError(const ConfigError& error) {
  if (error.code != ConfigErrorCode::kInvalidNumber) {
    return DescribeConfigError(error);
  }
  if (error.subject == kTickRateKey) {
    return DescribeConfigError(error, std::format("'{}' must be an integer from 1 to {}", error.subject, kMaxTickRate));
  }
  if (error.subject == kMetricsPortKey) {
    return DescribeConfigError(error,
                               std::format("'{}' must be an integer from 1 to {}", error.subject, kMaxMetricsPort));
  }
  if (error.subject == kCaptureMaxFilesKey || error.subject == kCaptureMaxMibKey) {
    return DescribeConfigError(error,
                               std::format("'{}' must be an integer from 1 to {}", error.subject, kMaxCaptureLimit));
  }
  return DescribeConfigError(error);
}

}  // namespace augusta::config
