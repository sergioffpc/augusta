#include "augusta/client_config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/config.h"
#include "augusta/input.h"

namespace augusta::config {

namespace {

// The open section binding each control it names to a key, on top of the
// defaults for the controls it leaves out.
constexpr std::string_view kKeysSection = "input.keys";

std::unexpected<ConfigError> Fail(ConfigErrorCode code, std::string subject) {
  return std::unexpected(ConfigError{.code = code, .subject = std::move(subject), .file = {}});
}

// The fallback when key is absent; when present, a finite number above zero.
std::expected<float, ConfigError> OptionalPositiveNumber(const ConfigValues& values, std::string_view key,
                                                         float fallback) {
  return values.contains(key) ? RequirePositiveNumber(values, key) : fallback;
}

// Decision: the keymap the `input.keys` entries of values (control name -> key
// name) make of the defaults. Each control keeps a key of its own, never the
// one that releases the cursor; errors name the entry ("input.keys.<control>").
std::expected<input::Keymap, ConfigError> ParseKeymap(const ConfigValues& values) {
  const std::string prefix = std::format("{}.", kKeysSection);
  auto bindings = values | std::views::filter([&](const auto& value) { return value.first.starts_with(prefix); });
  input::Keymap keymap = input::kDefaultKeymap;
  for (const auto& [path, key_name] : bindings) {
    const auto control = input::ControlNamed(std::string_view(path).substr(prefix.size()));
    if (!control) {
      return Fail(ConfigErrorCode::kUnknownControl, path);
    }
    const auto key = input::KeyNamed(key_name);
    if (!key) {
      return Fail(ConfigErrorCode::kInvalidKeyName, path);
    }
    if (*key == input::kReleaseCursorKey) {
      return Fail(ConfigErrorCode::kReservedKey, path);
    }
    keymap.at(static_cast<std::size_t>(*control)) = *key;
  }
  // The defaults never share a key, so any clash involves a control the section rebound.
  for (const auto& [path, key_name] : bindings) {
    if (std::ranges::count(keymap, *input::KeyNamed(key_name)) > 1) {
      return Fail(ConfigErrorCode::kKeyBoundTwice, path);
    }
  }
  return keymap;
}

std::expected<input::Config, ConfigError> ParseInputConfig(const ConfigValues& values) {
  const auto sensitivity = OptionalPositiveNumber(values, "input.mouse_sensitivity", input::kDefaultMouseSensitivity);
  if (!sensitivity) {
    return std::unexpected(sensitivity.error());
  }
  const auto keymap = ParseKeymap(values);
  if (!keymap) {
    return std::unexpected(keymap.error());
  }
  return input::Config{.mouse_sensitivity = *sensitivity, .keymap = *keymap};
}

// Every control's name, comma-separated, for an error that names none of them.
std::string ControlNames() {
  std::string names;
  for (std::size_t i = 0; i < input::kControlCount; ++i) {
    names += std::format("{}{}", i == 0 ? "" : ", ", input::NameOf(static_cast<input::Control>(i)));
  }
  return names;
}

}  // namespace

std::expected<ClientConfig, ConfigError> ParseClientConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 7> kKeys{
      "base_dir",
      "content.pack",
      "content.public_key",
      "player.character",
      "network.server_address",
      "logging.level",
      "input.mouse_sensitivity",
  };
  static constexpr std::array<std::string_view, 1> kOpenSections{kKeysSection};
  const auto values = ReadConfigValues(yaml_text, ConfigSchema{.keys = kKeys, .open_sections = kOpenSections});
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
  auto character = RequireString(*values, "player.character");
  if (!character) {
    return std::unexpected(character.error());
  }
  auto log_level = OptionalLogLevel(*values, "logging.level", kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  const auto input = ParseInputConfig(*values);
  if (!input) {
    return std::unexpected(input.error());
  }
  return ClientConfig{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .character = *std::move(character),
      .server_address = OptionalString(*values, "network.server_address", kDefaultServerAddress),
      .log_level = *std::move(log_level),
      .input = *input,
  };
}

std::expected<ClientConfig, ConfigError> LoadClientConfig(const std::filesystem::path& file) {
  return LoadConfigFile<ClientConfig>(file, ParseClientConfig);
}

std::string DescribeClientConfigError(const ConfigError& error) {
  switch (error.code) {
    case ConfigErrorCode::kUnknownControl:
      return DescribeConfigError(
          error, std::format("'{}' names no control; the controls are {}", error.subject, ControlNames()));
    case ConfigErrorCode::kReservedKey:
      return DescribeConfigError(error, std::format("'{}' can't use {}: it releases the cursor", error.subject,
                                                    input::NameOf(input::kReleaseCursorKey)));
    default:
      return DescribeConfigError(error);
  }
}

}  // namespace augusta::config
