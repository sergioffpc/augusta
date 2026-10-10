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
constexpr std::string_view kCaptureKey = "simulation.capture";
constexpr std::string_view kCaptureModeKey = "simulation.capture_mode";
constexpr std::string_view kCaptureMaxFilesKey = "simulation.capture_retention.max_files";
constexpr std::string_view kCaptureMaxMibKey = "simulation.capture_retention.max_mib";
constexpr std::uint32_t kMaxCaptureLimit = std::numeric_limits<std::uint32_t>::max();
constexpr std::string_view kReplayCapturesKey = "replay.captures";
constexpr std::string_view kReplayMaxViewersKey = "replay.max_viewers";
constexpr std::uint32_t kMaxReplayViewers = std::numeric_limits<std::uint8_t>::max();
constexpr std::string_view kReenactmentsKey = "simulation.reenactments";

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

// Whether the mode under key is strict: "optional" when absent.
std::expected<bool, ConfigError> OptionalStrict(const ConfigValues& values, std::string_view key) {
  const std::string mode = OptionalString(values, key, "optional");
  if (mode != "optional" && mode != "strict") {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidEntry,
                                       .subject = std::string(key),
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

// A key that may not be set with replay.captures, or that needs it.
ConfigError ReplayEntryError(std::string_view key, std::string_view reason) {
  return ConfigError{
      .code = ConfigErrorCode::kInvalidEntry, .subject = std::string(key), .reason = std::string(reason), .file = {}};
}

// Reads a replay server's settings into config: its captures' directory,
// empty for a live server, and how many Replays it runs at once. A replay
// server runs no Match, so none of it is captured.
std::expected<void, ConfigError> ReadReplay(const ConfigValues& values, const std::filesystem::path& root,
                                            ServerConfig& config) {
  auto captures = OptionalPath(values, kReplayCapturesKey, root);
  if (!captures) {
    return std::unexpected(captures.error());
  }
  if (captures->empty()) {
    if (values.contains(kReplayMaxViewersKey)) {
      return std::unexpected(ReplayEntryError(kReplayMaxViewersKey, "needs replay.captures"));
    }
    return {};
  }
  for (const std::string_view key :
       {kCaptureKey, kCaptureModeKey, kCaptureMaxFilesKey, kCaptureMaxMibKey, kReenactmentsKey}) {
    if (values.contains(key)) {
      return std::unexpected(ReplayEntryError(key, "cannot be set on a replay server: it runs no Match"));
    }
  }
  std::uint8_t max_viewers = kDefaultReplayMaxViewers;
  if (values.contains(kReplayMaxViewersKey)) {
    const auto read = RequireWholeNumber(values, kReplayMaxViewersKey, 1, kMaxReplayViewers);
    if (!read) {
      return std::unexpected(read.error());
    }
    max_viewers = static_cast<std::uint8_t>(*read);
  }
  config.replay_captures = *std::move(captures);
  config.replay_max_viewers = max_viewers;
  return {};
}

// The ServerConfig values holds, each relative path in it from root.
std::expected<ServerConfig, ConfigError> ServerConfigFrom(const ConfigValues& values,
                                                          const std::filesystem::path& root) {
  auto pack_path = RequirePath(values, "content.pack", root);
  if (!pack_path) {
    return std::unexpected(pack_path.error());
  }
  auto public_key_path = RequirePath(values, "content.public_key", root);
  if (!public_key_path) {
    return std::unexpected(public_key_path.error());
  }
  const auto tick_rate_hz = RequireTickRate(values);
  if (!tick_rate_hz) {
    return std::unexpected(tick_rate_hz.error());
  }
  auto log_level = OptionalLogLevel(values, "logging.level", kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  auto capture_directory = OptionalPath(values, kCaptureKey, root);
  if (!capture_directory) {
    return std::unexpected(capture_directory.error());
  }
  const auto strict_capture = OptionalStrict(values, kCaptureModeKey);
  if (!strict_capture) {
    return std::unexpected(strict_capture.error());
  }
  const auto capture_max_files = OptionalCaptureLimit(values, kCaptureMaxFilesKey);
  if (!capture_max_files) {
    return std::unexpected(capture_max_files.error());
  }
  const auto capture_max_mib = OptionalCaptureLimit(values, kCaptureMaxMibKey);
  if (!capture_max_mib) {
    return std::unexpected(capture_max_mib.error());
  }
  const auto metrics_port = OptionalMetricsPort(values);
  if (!metrics_port) {
    return std::unexpected(metrics_port.error());
  }
  return ServerConfig{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .tick_rate_hz = *tick_rate_hz,
      .listen_address = OptionalString(values, "network.listen_address", kDefaultListenAddress),
      .log_level = *std::move(log_level),
      .capture_directory = *std::move(capture_directory),
      .strict_capture = *strict_capture,
      .capture_max_files = *capture_max_files,
      .capture_max_mib = *capture_max_mib,
      .reenactments = false,
      .metrics_port = *metrics_port,
      .replay_captures = {},
  };
}

}  // namespace

std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 14> kKeys{
      "base_dir",           "content.pack",    "content.public_key",
      kTickRateKey,         kCaptureKey,       kCaptureModeKey,
      kCaptureMaxFilesKey,  kCaptureMaxMibKey, "network.listen_address",
      "logging.level",      kMetricsPortKey,   kReplayCapturesKey,
      kReplayMaxViewersKey, kReenactmentsKey,
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
  return ServerConfigFrom(*values, *root).and_then([&](ServerConfig config) {
    return ReadReplay(*values, *root, config)
        .and_then([&] { return OptionalReenactments(*values); })
        .transform([&config](bool reenactments) {
          config.reenactments = reenactments;
          return std::move(config);
        });
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
  if (error.subject == kReplayMaxViewersKey) {
    return DescribeConfigError(error,
                               std::format("'{}' must be an integer from 1 to {}", error.subject, kMaxReplayViewers));
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
