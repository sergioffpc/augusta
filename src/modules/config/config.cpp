#include "augusta/config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/program_options.hpp>
#include <yaml-cpp/yaml.h>

#include "augusta/logging.h"

namespace augusta::config {

namespace {

using ScalarMap = std::map<std::string, std::string, std::less<>>;

std::optional<std::filesystem::path> ExecutableDirectory() {
  boost::system::error_code error;
  const auto executable = boost::dll::program_location(error);
  if (error) {
    return std::nullopt;
  }
  // native(): a std::wstring on Windows, so non-ASCII directories survive.
  return std::filesystem::path(executable.native()).parent_path();
}

// Mechanism: reads text as a flat YAML mapping of scalars. Which keys exist,
// and which are required, is the schema's business (the Parse* functions).
std::expected<ScalarMap, ConfigError> ReadScalarMap(std::string_view text,
                                                    std::span<const std::string_view> allowed_keys) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(text));
  } catch (const YAML::Exception& error) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidYaml, .subject = error.what()});
  }
  if (!root.IsMap()) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kNotAMapping});
  }

  ScalarMap values;
  for (const auto& entry : root) {
    if (!entry.first.IsScalar()) {
      return std::unexpected(ConfigError{.code = ConfigErrorCode::kNonStringKey});
    }
    const std::string key = entry.first.Scalar();
    if (std::ranges::find(allowed_keys, key) == allowed_keys.end()) {
      return std::unexpected(ConfigError{.code = ConfigErrorCode::kUnknownKey, .subject = key});
    }
    if (!entry.second.IsScalar()) {
      return std::unexpected(ConfigError{.code = ConfigErrorCode::kNonStringValue, .subject = key});
    }
    if (!values.emplace(key, entry.second.Scalar()).second) {
      return std::unexpected(ConfigError{.code = ConfigErrorCode::kDuplicateKey, .subject = key});
    }
  }
  return values;
}

std::expected<std::string, ConfigError> RequireString(const ScalarMap& values, std::string_view key) {
  const auto found = values.find(key);
  if (found == values.end()) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kMissingKey, .subject = std::string(key)});
  }
  if (found->second.empty()) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kEmptyValue, .subject = std::string(key)});
  }
  return found->second;
}

std::expected<std::filesystem::path, ConfigError> RequirePath(const ScalarMap& values, std::string_view key,
                                                              const std::filesystem::path& base_dir) {
  const auto value = RequireString(values, key);
  if (!value) {
    return std::unexpected(value.error());
  }
  // YAML text is UTF-8, but a path built from a plain std::string is read in
  // the system codepage on Windows, which corrupts non-ASCII directory names.
  const std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(value->data()), value->size()));
  return (path.is_absolute() ? path : base_dir / path).lexically_normal();
}

// A finite number above zero, in plain decimal or exponent notation.
std::expected<float, ConfigError> RequirePositiveNumber(const ScalarMap& values, std::string_view key) {
  const auto text = RequireString(values, key);
  if (!text) {
    return std::unexpected(text.error());
  }
  float number = 0.0F;
  const char* const end = text->data() + text->size();
  const auto parsed = std::from_chars(text->data(), end, number);
  if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(number) || number <= 0.0F) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidNumber, .subject = std::string(key)});
  }
  return number;
}

std::string OptionalString(const ScalarMap& values, std::string_view key, std::string_view fallback) {
  const auto found = values.find(key);
  return found == values.end() ? std::string(fallback) : found->second;
}

// fallback when key is absent; when present, its value must be one
// augusta::logging::ParseSeverity accepts.
std::expected<std::string, ConfigError> OptionalLogLevel(const ScalarMap& values, std::string_view key,
                                                         std::string_view fallback) {
  auto value = OptionalString(values, key, fallback);
  if (!logging::ParseSeverity(value)) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidLogLevel, .subject = std::string(key)});
  }
  return value;
}

std::expected<std::string, ConfigError> ReadFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kCannotOpenFile, .file = file});
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

template <typename Config, typename ParseFn>
std::expected<Config, ConfigError> LoadFile(const std::filesystem::path& file, ParseFn parse) {
  const auto text = ReadFile(file);
  if (!text) {
    return std::unexpected(text.error());
  }
  auto config = parse(*text, file.parent_path());
  if (!config) {
    config.error().file = file;
  }
  return config;
}

// The phrase for a code alone, before its subject and file are added.
std::string Phrase(const ConfigError& error) {
  switch (error.code) {
    case ConfigErrorCode::kInvalidArguments:
      return error.subject;
    case ConfigErrorCode::kExecutableDirectoryUnknown:
      return std::format("cannot locate the executable's directory to find {}", error.subject);
    case ConfigErrorCode::kCannotOpenFile:
      return "cannot open the config file";
    case ConfigErrorCode::kInvalidYaml:
      return std::format("invalid YAML: {}", error.subject);
    case ConfigErrorCode::kNotAMapping:
      return "the top level must be a mapping of keys to values";
    case ConfigErrorCode::kNonStringKey:
      return "every key must be a plain string";
    case ConfigErrorCode::kUnknownKey:
      return std::format("unknown key '{}'", error.subject);
    case ConfigErrorCode::kDuplicateKey:
      return std::format("duplicate key '{}'", error.subject);
    case ConfigErrorCode::kNonStringValue:
      return std::format("'{}' must be a string", error.subject);
    case ConfigErrorCode::kMissingKey:
      return std::format("missing required key '{}'", error.subject);
    case ConfigErrorCode::kEmptyValue:
      return std::format("'{}' must not be empty", error.subject);
    case ConfigErrorCode::kInvalidNumber:
      return std::format("'{}' must be a finite number above zero", error.subject);
    case ConfigErrorCode::kInvalidLogLevel:
      return std::format("'{}' must be one of trace, debug, info, warn, error, critical", error.subject);
  }
  return "unknown config error";
}

}  // namespace

std::string DescribeConfigError(const ConfigError& error) {
  const auto phrase = Phrase(error);
  return error.file.empty() ? phrase : std::format("{}: {}", error.file.string(), phrase);
}

std::expected<std::filesystem::path, ConfigError> ResolveConfigFile(int argc, const char* const* argv,
                                                                    std::string_view program,
                                                                    std::string_view default_file_name) {
  const auto usage = std::format("usage: {} [--config <file>]\n  without --config, reads {} next to the executable",
                                 program, default_file_name);

  namespace po = boost::program_options;
  po::options_description options;
  options.add_options()("config", po::value<std::string>(), "config file, relative to the working directory");

  // Prefix guessing is off so `--conf` is an error, not a silent `--config`;
  // the empty positional description makes a bare argument an error too,
  // where Boost would otherwise ignore it.
  po::variables_map arguments;
  try {
    po::store(po::command_line_parser(argc, argv)
                  .options(options)
                  .positional(po::positional_options_description())
                  .style(po::command_line_style::default_style & ~po::command_line_style::allow_guessing)
                  .run(),
              arguments);
    po::notify(arguments);
  } catch (const po::error& error) {
    return std::unexpected(
        ConfigError{.code = ConfigErrorCode::kInvalidArguments, .subject = std::format("{}\n{}", error.what(), usage)});
  }

  if (arguments.empty()) {
    const auto directory = ExecutableDirectory();
    if (!directory) {
      return std::unexpected(
          ConfigError{.code = ConfigErrorCode::kExecutableDirectoryUnknown, .subject = std::string(default_file_name)});
    }
    return *directory / default_file_name;
  }
  const auto& file = arguments["config"].as<std::string>();
  if (file.empty()) {
    return std::unexpected(ConfigError{
        .code = ConfigErrorCode::kInvalidArguments,
        .subject = std::format("--config needs a file name\n{}", usage),
    });
  }
  return std::filesystem::path(file);
}

std::expected<ClientConfig, ConfigError> ParseClientConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 5> kKeys{
      "base_dir", "pack", "public_key", "server_address", "log_level",
  };
  const auto values = ReadScalarMap(yaml_text, kKeys);
  if (!values) {
    return std::unexpected(values.error());
  }
  // The file's own directory only anchors a relative base_dir; every other
  // relative path starts from base_dir.
  const auto root = RequirePath(*values, "base_dir", base_dir);
  if (!root) {
    return std::unexpected(root.error());
  }
  auto pack_path = RequirePath(*values, "pack", *root);
  if (!pack_path) {
    return std::unexpected(pack_path.error());
  }
  auto public_key_path = RequirePath(*values, "public_key", *root);
  if (!public_key_path) {
    return std::unexpected(public_key_path.error());
  }
  auto log_level = OptionalLogLevel(*values, "log_level", kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  return ClientConfig{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .server_address = OptionalString(*values, "server_address", kDefaultServerAddress),
      .log_level = *std::move(log_level),
  };
}

std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 6> kKeys{
      "base_dir", "pack", "public_key", "tick_rate_hz", "listen_address", "log_level",
  };
  const auto values = ReadScalarMap(yaml_text, kKeys);
  if (!values) {
    return std::unexpected(values.error());
  }
  // The file's own directory only anchors a relative base_dir; every other
  // relative path starts from base_dir.
  const auto root = RequirePath(*values, "base_dir", base_dir);
  if (!root) {
    return std::unexpected(root.error());
  }
  auto pack_path = RequirePath(*values, "pack", *root);
  if (!pack_path) {
    return std::unexpected(pack_path.error());
  }
  auto public_key_path = RequirePath(*values, "public_key", *root);
  if (!public_key_path) {
    return std::unexpected(public_key_path.error());
  }
  const auto tick_rate_hz = RequirePositiveNumber(*values, "tick_rate_hz");
  if (!tick_rate_hz) {
    return std::unexpected(tick_rate_hz.error());
  }
  auto log_level = OptionalLogLevel(*values, "log_level", kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  return ServerConfig{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .tick_rate_hz = *tick_rate_hz,
      .listen_address = OptionalString(*values, "listen_address", kDefaultListenAddress),
      .log_level = *std::move(log_level),
  };
}

std::expected<ClientConfig, ConfigError> LoadClientConfig(const std::filesystem::path& file) {
  return LoadFile<ClientConfig>(file, ParseClientConfig);
}

std::expected<ServerConfig, ConfigError> LoadServerConfig(const std::filesystem::path& file) {
  return LoadFile<ServerConfig>(file, ParseServerConfig);
}

}  // namespace augusta::config
