#include "settings.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/client_config.h"
#include "augusta/config.h"

namespace augusta::swarm {

namespace {

// A key whose value is a whole number, and the range it must be in: what both
// reading it and saying what is wrong with it go by.
struct WholeNumberKey {
  std::string_view key;
  std::uint32_t min = 0;
  std::uint32_t max = 0;
};

constexpr WholeNumberKey kMatches{.key = "run.matches", .min = 1, .max = std::numeric_limits<std::uint8_t>::max()};
constexpr WholeNumberKey kSeed{.key = "run.seed", .min = 0, .max = std::numeric_limits<std::uint32_t>::max()};
constexpr std::array<WholeNumberKey, 2> kWholeNumberKeys{kMatches, kSeed};

std::expected<std::uint32_t, config::ConfigError> RequireWholeNumber(const config::ConfigValues& values,
                                                                     const WholeNumberKey& key) {
  return config::RequireWholeNumber(values, key.key, key.min, key.max);
}

// The whole-number key named `subject`, or null if it names none.
const WholeNumberKey* FindWholeNumberKey(std::string_view subject) {
  for (const WholeNumberKey& key : kWholeNumberKeys) {
    if (key.key == subject) {
      return &key;
    }
  }
  return nullptr;
}

}  // namespace

std::expected<Settings, config::ConfigError> ParseSettings(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 9> kKeys{
      "base_dir",      "content.pack", "content.public_key",  "player.character", "network.server_address",
      "logging.level", kMatches.key,   "run.timeout_seconds", kSeed.key,
  };
  const auto values = config::ReadConfigValues(yaml_text, config::ConfigSchema{.keys = kKeys, .open_sections = {}});
  if (!values) {
    return std::unexpected(values.error());
  }
  // The file's own directory only anchors a relative base_dir; every other
  // relative path starts from base_dir.
  const auto root = config::RequirePath(*values, "base_dir", base_dir);
  if (!root) {
    return std::unexpected(root.error());
  }
  auto pack_path = config::RequirePath(*values, "content.pack", *root);
  if (!pack_path) {
    return std::unexpected(pack_path.error());
  }
  auto public_key_path = config::RequirePath(*values, "content.public_key", *root);
  if (!public_key_path) {
    return std::unexpected(public_key_path.error());
  }
  auto character = config::RequireString(*values, "player.character");
  if (!character) {
    return std::unexpected(character.error());
  }
  auto log_level = config::OptionalLogLevel(*values, "logging.level", config::kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  const auto matches = RequireWholeNumber(*values, kMatches);
  if (!matches) {
    return std::unexpected(matches.error());
  }
  const auto timeout_seconds = config::RequirePositiveNumber(*values, "run.timeout_seconds");
  if (!timeout_seconds) {
    return std::unexpected(timeout_seconds.error());
  }
  const auto seed = values->contains(kSeed.key) ? RequireWholeNumber(*values, kSeed) : kDefaultSeed;
  if (!seed) {
    return std::unexpected(seed.error());
  }
  return Settings{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .character = *std::move(character),
      .server_address = config::OptionalString(*values, "network.server_address", config::kDefaultServerAddress),
      .log_level = *std::move(log_level),
      .matches = static_cast<std::uint8_t>(*matches),
      .timeout_seconds = *timeout_seconds,
      .seed = *seed,
  };
}

std::expected<Settings, config::ConfigError> LoadSettings(const std::filesystem::path& file) {
  const auto text = config::ReadConfigFile(file);
  if (!text) {
    return std::unexpected(text.error());
  }
  auto settings = ParseSettings(*text, file.parent_path());
  if (!settings) {
    settings.error().file = file;
  }
  return settings;
}

std::string DescribeSettingsError(const config::ConfigError& error) {
  const WholeNumberKey* const range = FindWholeNumberKey(error.subject);
  if (error.code != config::ConfigErrorCode::kInvalidNumber || range == nullptr) {
    return config::DescribeConfigError(error);
  }
  const std::string phrase =
      std::format("'{}' must be an integer from {} to {}", error.subject, range->min, range->max);
  return error.file.empty() ? phrase : std::format("{}: {}", error.file.string(), phrase);
}

}  // namespace augusta::swarm
