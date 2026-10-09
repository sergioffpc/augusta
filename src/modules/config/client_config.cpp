#include "augusta/client_config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "augusta/config.h"
#include "augusta/input.h"

namespace augusta::config {

namespace {

// The open section binding each control it names to a key, on top of the
// defaults for the controls it leaves out.
constexpr std::string_view kKeysSection = "input.keys";

// Every control's name, comma-separated, for an entry that names none of them.
std::string ControlNames() {
  std::string names;
  for (std::size_t i = 0; i < input::kControlCount; ++i) {
    names += std::format("{}{}", i == 0 ? "" : ", ", input::NameOf(static_cast<input::Control>(i)));
  }
  return names;
}

// An `input.keys` entry the keymap rejects, and reason why.
std::unexpected<ConfigError> Reject(std::string entry, std::string reason) {
  return std::unexpected(ConfigError{
      .code = ConfigErrorCode::kInvalidEntry, .subject = std::move(entry), .reason = std::move(reason), .file = {}});
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
      return Reject(path, std::format("names no control; the controls are {}", ControlNames()));
    }
    const auto key = input::KeyNamed(key_name);
    if (!key) {
      return Reject(path, "must name a key, e.g. W, LeftShift, Space, F1 or MouseRight");
    }
    if (*key == input::kReleaseCursorKey) {
      return Reject(path, std::format("can't use {}: it releases the cursor", input::NameOf(input::kReleaseCursorKey)));
    }
    keymap.at(static_cast<std::size_t>(*control)) = *key;
  }
  // The defaults never share a key, so any clash involves a control the section rebound.
  for (const auto& [path, key_name] : bindings) {
    if (std::ranges::count(keymap, *input::KeyNamed(key_name)) > 1) {
      return Reject(path, "is bound to a key another control already uses");
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

std::expected<std::optional<ReenactArguments>, ConfigError> ReadReenactArguments(const CommandLine& command_line) {
  const auto capture = command_line.options.find("reenact");
  const auto player = command_line.options.find("player");
  if (capture == command_line.options.end() && player == command_line.options.end()) {
    return std::nullopt;
  }
  const auto invalid = [](std::string_view why) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidArguments,
                                       .subject = std::format("{}: augustac --reenact <capture> --player <n>", why),
                                       .reason = {},
                                       .file = {}});
  };
  if (capture == command_line.options.end() || player == command_line.options.end()) {
    return invalid("a reenactment needs both");
  }
  if (capture->second.empty()) {
    return invalid("--reenact needs a capture file");
  }
  constexpr unsigned kMaxPlayer = 255;
  unsigned number = 0;
  const std::string& text = player->second;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number < 1 || number > kMaxPlayer) {
    return invalid("--player must be a whole number from 1 to 255");
  }
  return ReenactArguments{.capture = capture->second, .player = static_cast<std::uint8_t>(number)};
}

}  // namespace augusta::config
