#include "augusta/server_config.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "augusta/config.h"

namespace augusta::config {

namespace {

constexpr std::string_view kTickRateKey = "simulation.tick_rate_hz";
constexpr std::uint32_t kMaxTickRate = std::numeric_limits<std::uint8_t>::max();
constexpr std::string_view kListenAddressKey = "network.listen_address";
constexpr std::string_view kMetricsPortKey = "metrics.port";
constexpr std::uint32_t kMaxPort = std::numeric_limits<std::uint16_t>::max();
constexpr std::string_view kCaptureKey = "simulation.capture";
constexpr std::string_view kCaptureModeKey = "simulation.capture_mode";
constexpr std::string_view kCaptureMaxFilesKey = "simulation.capture_retention.max_files";
constexpr std::string_view kCaptureMaxMibKey = "simulation.capture_retention.max_mib";
constexpr std::uint32_t kMaxCaptureLimit = std::numeric_limits<std::uint32_t>::max();
#if AUGUSTA_DEBUG_TOOLS
constexpr std::string_view kReenactmentsKey = "debug.reenactments";
constexpr std::string_view kShotJudgementsKey = "debug.shot_judgements";
constexpr std::string_view kPhysicsRecordingKey = "debug.physics_recording";
constexpr std::string_view kNetworkPcapngKey = "debug.network.pcapng";
#endif
constexpr std::string_view kReplayCapturesKey = "replay.captures";
constexpr std::string_view kReplayMaxViewersKey = "replay.max_viewers";
constexpr std::uint32_t kMaxReplayViewers = std::numeric_limits<std::uint8_t>::max();

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
  return RequireWholeNumber(values, kMetricsPortKey, 1, kMaxPort).transform([](std::uint32_t port) {
    return static_cast<std::uint16_t>(port);
  });
}

// Whether port is a TCP port from 1 to 65535 in plain decimal.
bool IsPort(std::string_view port) {
  std::uint32_t number = 0;
  const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), number);
  return !port.empty() && error == std::errc{} && end == port.data() + port.size() && number >= 1 && number <= kMaxPort;
}

// Whether address is <host>:<port>, an IPv6 host bracketed, with a port from
// 1 to 65535: a port left out or 0 has the server listen wherever the OS puts
// it, where no client looks. The host itself is the networking module's to
// parse.
bool IsListenAddress(std::string_view address) {
  const std::size_t colon = address.rfind(':');
  if (colon == std::string_view::npos || !IsPort(address.substr(colon + 1))) {
    return false;
  }
  const std::string_view host = address.substr(0, colon);
  if (host.starts_with('[') || host.contains(':')) {
    return host.size() > 2 && host.front() == '[' && host.back() == ']' &&
           host.substr(1, host.size() - 2).contains(':');
  }
  return !host.empty();
}

// The fallback when absent; when present, an address IsListenAddress accepts.
std::expected<std::string, ConfigError> OptionalListenAddress(const ConfigValues& values) {
  std::string address = OptionalString(values, kListenAddressKey, kDefaultListenAddress);
  if (!IsListenAddress(address)) {
    return std::unexpected(ConfigError{
        .code = ConfigErrorCode::kInvalidEntry,
        .subject = std::string(kListenAddressKey),
        .reason = std::format("must be <host>:<port> or [<IPv6 host>]:<port>, the port from 1 to {}", kMaxPort),
        .file = {},
    });
  }
  return address;
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

// A key that may not be set with another, or that needs one.
ConfigError CrossCheckError(std::string_view key, std::string_view reason) {
  return ConfigError{
      .code = ConfigErrorCode::kInvalidEntry, .subject = std::string(key), .reason = std::string(reason), .file = {}};
}

// Refuses a capture setting without the capture directory it shapes
// (ADR-0050): with no simulation.capture, nothing is captured.
std::expected<void, ConfigError> RequireCaptureForCaptureSettings(const ConfigValues& values) {
  if (values.contains(kCaptureKey)) {
    return {};
  }
  for (const std::string_view key : {kCaptureModeKey, kCaptureMaxFilesKey, kCaptureMaxMibKey}) {
    if (values.contains(key)) {
      return std::unexpected(CrossCheckError(key, "needs simulation.capture"));
    }
  }
  return {};
}

// Reads a replay server's settings into config: its captures' directory,
// empty for a live server, and how many Replays it runs at once. A replay
// server runs no Match, so none of it is captured or reenacted; a live
// server's capture settings need the directory they shape.
std::expected<void, ConfigError> ReadReplay(const ConfigValues& values, const std::filesystem::path& root,
                                            ServerConfig& config) {
  auto captures = OptionalPath(values, kReplayCapturesKey, root);
  if (!captures) {
    return std::unexpected(captures.error());
  }
  if (captures->empty()) {
    if (values.contains(kReplayMaxViewersKey)) {
      return std::unexpected(CrossCheckError(kReplayMaxViewersKey, "needs replay.captures"));
    }
    return RequireCaptureForCaptureSettings(values);
  }
  for (const std::string_view key : {
           kCaptureKey,
           kCaptureModeKey,
           kCaptureMaxFilesKey,
           kCaptureMaxMibKey,
#if AUGUSTA_DEBUG_TOOLS
           kReenactmentsKey,
           kShotJudgementsKey,
#endif
       }) {
    if (values.contains(key)) {
      return std::unexpected(CrossCheckError(key, "cannot be set on a replay server: it runs no Match"));
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

// Reads a live server's capture settings into config (ADR-0050): the
// directory, relative to root, its mode and its retention limits.
std::expected<void, ConfigError> ReadCapture(const ConfigValues& values, const std::filesystem::path& root,
                                             ServerConfig& config) {
  auto directory = OptionalPath(values, kCaptureKey, root);
  if (!directory) {
    return std::unexpected(directory.error());
  }
  const auto strict = OptionalStrict(values, kCaptureModeKey);
  if (!strict) {
    return std::unexpected(strict.error());
  }
  const auto max_files = OptionalCaptureLimit(values, kCaptureMaxFilesKey);
  if (!max_files) {
    return std::unexpected(max_files.error());
  }
  const auto max_mib = OptionalCaptureLimit(values, kCaptureMaxMibKey);
  if (!max_mib) {
    return std::unexpected(max_mib.error());
  }
  config.capture_directory = *std::move(directory);
  config.strict_capture = *strict;
  config.capture_max_files = *max_files;
  config.capture_max_mib = *max_mib;
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
  auto listen_address = OptionalListenAddress(values);
  if (!listen_address) {
    return std::unexpected(listen_address.error());
  }
  const auto metrics_port = OptionalMetricsPort(values);
  if (!metrics_port) {
    return std::unexpected(metrics_port.error());
  }
  ServerConfig config{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .tick_rate_hz = *tick_rate_hz,
      .listen_address = *std::move(listen_address),
      .log_level = *std::move(log_level),
      // Read next, by ReadCapture.
      .capture_directory = {},
      .strict_capture = false,
      .capture_max_files = std::nullopt,
      .capture_max_mib = std::nullopt,
#if AUGUSTA_DEBUG_TOOLS
      // Read last, by ReadDebug.
      .reenactments = false,
      .shot_judgements = false,
      .physics_recording = false,
      .network_pcapng = {},
#endif
      .metrics_port = *metrics_port,
      .replay_captures = {},
  };
  return ReadCapture(values, root, config).transform([&config] { return std::move(config); });
}

#if AUGUSTA_DEBUG_TOOLS
// Reads the `debug` section (ADR-0074) into config: whether Reenact requests
// are taken, whether Shot judgements are sent to whoever subscribes to them,
// whether the process's physics is recorded, and the directory the pcapng goes
// in (ADR-0076), relative to root. Last, so that a config refused for anything
// else creates no directory.
std::expected<ServerConfig, ConfigError> ReadDebug(const ConfigValues& values, const std::filesystem::path& root,
                                                   ServerConfig config) {
  const auto reenactments = OptionalFlag(values, kReenactmentsKey, false);
  if (!reenactments) {
    return std::unexpected(reenactments.error());
  }
  const auto shot_judgements = OptionalFlag(values, kShotJudgementsKey, false);
  if (!shot_judgements) {
    return std::unexpected(shot_judgements.error());
  }
  const auto physics_recording = OptionalFlag(values, kPhysicsRecordingKey, false);
  if (!physics_recording) {
    return std::unexpected(physics_recording.error());
  }
  config.reenactments = *reenactments;
  config.shot_judgements = *shot_judgements;
  config.physics_recording = *physics_recording;
  auto directory = OptionalWritableDirectory(values, kNetworkPcapngKey, root);
  if (!directory) {
    return std::unexpected(directory.error());
  }
  config.network_pcapng = *std::move(directory);
  return config;
}
#endif

}  // namespace

std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  // The `debug` section's keys only in a Development build (ADR-0074): a
  // Shipping build refuses them as it refuses any unknown key.
  static constexpr auto kKeys = std::to_array<std::string_view>({
      "base_dir",
      "content.pack",
      "content.public_key",
      kTickRateKey,
      kCaptureKey,
      kCaptureModeKey,
      kCaptureMaxFilesKey,
      kCaptureMaxMibKey,
      kListenAddressKey,
      "logging.level",
      kMetricsPortKey,
      kReplayCapturesKey,
      kReplayMaxViewersKey,
#if AUGUSTA_DEBUG_TOOLS
      kReenactmentsKey,
      kShotJudgementsKey,
      kPhysicsRecordingKey,
      kNetworkPcapngKey,
#endif
  });
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
  auto config = ServerConfigFrom(*values, *root).and_then([&](ServerConfig read) {
    return ReadReplay(*values, *root, read).transform([&read] { return std::move(read); });
  });
#if AUGUSTA_DEBUG_TOOLS
  return std::move(config).and_then([&](ServerConfig read) { return ReadDebug(*values, *root, std::move(read)); });
#else
  return config;
#endif
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
    return DescribeConfigError(error, std::format("'{}' must be an integer from 1 to {}", error.subject, kMaxPort));
  }
  if (error.subject == kCaptureMaxFilesKey || error.subject == kCaptureMaxMibKey) {
    return DescribeConfigError(error,
                               std::format("'{}' must be an integer from 1 to {}", error.subject, kMaxCaptureLimit));
  }
  return DescribeConfigError(error);
}

}  // namespace augusta::config
