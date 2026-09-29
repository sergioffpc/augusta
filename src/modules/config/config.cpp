#include "augusta/config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/program_options.hpp>
#include <yaml-cpp/yaml.h>

#include "augusta/input.h"
#include "augusta/logging.h"

namespace augusta::config {

namespace {

// An error about subject, not yet tied to a config file: the Load* functions
// set that once they know it.
std::unexpected<ConfigError> Fail(ConfigErrorCode code, std::string subject = {}) {
  return std::unexpected(ConfigError{.code = code, .subject = std::move(subject), .file = {}});
}

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

// What a config file may hold, as dotted paths ("network.server_address"):
// its scalar keys, and its open sections - sections whose entries the schema
// names itself ("input.keys", whose entries are control names).
struct Schema {
  std::span<const std::string_view> keys;
  std::span<const std::string_view> open_sections;
};

std::string Child(std::string_view parent, std::string_view name) {
  return parent.empty() ? std::string(name) : std::format("{}.{}", parent, name);
}

bool IsKey(const Schema& schema, std::string_view path) {
  return std::ranges::find(schema.keys, path) != schema.keys.end();
}

// Whether path is a section: an open section, or a prefix of a key or of an open section.
bool IsSection(const Schema& schema, std::string_view path) {
  // member is path itself or lies under it.
  const auto opens = [&](std::string_view member) {
    return member == path || (member.starts_with(path) && member.size() > path.size() && member[path.size()] == '.');
  };
  return std::ranges::any_of(schema.open_sections, opens) ||
         std::ranges::any_of(schema.keys, [&](std::string_view key) { return key != path && opens(key); });
}

// Whether path is an entry of an open section, which the schema checks by name itself.
bool InOpenSection(const Schema& schema, std::string_view path) {
  const auto dot = path.rfind('.');
  return dot != std::string_view::npos &&
         std::ranges::find(schema.open_sections, path.substr(0, dot)) != schema.open_sections.end();
}

// Mechanism: reads node, the mapping at section (empty at the top), into
// values under dotted paths. Every scalar must be a key or open-section entry
// of schema and every mapping one of its sections; errors name the path.
std::expected<void, ConfigError> Flatten(const YAML::Node& node, const std::string& section, const Schema& schema,
                                         ScalarMap& values) {
  std::set<std::string, std::less<>> seen;
  for (const auto& entry : node) {
    if (!entry.first.IsScalar()) {
      return Fail(ConfigErrorCode::kNonStringKey);
    }
    const std::string& name = entry.first.Scalar();
    const std::string path = Child(section, name);
    // A dot is how paths join, never part of a name: `content.pack: x` is not `content: {pack: x}`.
    if (name.contains('.')) {
      return Fail(ConfigErrorCode::kUnknownKey, path);
    }
    if (!seen.insert(path).second) {
      return Fail(ConfigErrorCode::kDuplicateKey, path);
    }
    if (IsSection(schema, path)) {
      // A section with every entry left out (or commented out) sets nothing.
      if (entry.second.IsNull()) {
        continue;
      }
      if (!entry.second.IsMap()) {
        return Fail(ConfigErrorCode::kNotASection, path);
      }
      if (auto nested = Flatten(entry.second, path, schema, values); !nested) {
        return nested;
      }
      continue;
    }
    if (!IsKey(schema, path) && !InOpenSection(schema, path)) {
      return Fail(ConfigErrorCode::kUnknownKey, path);
    }
    if (!entry.second.IsScalar()) {
      return Fail(ConfigErrorCode::kNonStringValue, path);
    }
    values.emplace(path, entry.second.Scalar());
  }
  return {};
}

// Mechanism: reads text as a YAML mapping, its sections flattened into dotted
// paths. Which keys exist, and which are required, is the schema's business
// (the Parse* functions).
std::expected<ScalarMap, ConfigError> ReadMapping(std::string_view text, const Schema& schema) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(text));
  } catch (const YAML::Exception& error) {
    return Fail(ConfigErrorCode::kInvalidYaml, error.what());
  }
  if (!root.IsMap()) {
    return Fail(ConfigErrorCode::kNotAMapping);
  }
  ScalarMap values;
  if (auto read = Flatten(root, "", schema, values); !read) {
    return std::unexpected(read.error());
  }
  return values;
}

std::expected<std::string, ConfigError> RequireString(const ScalarMap& values, std::string_view key) {
  const auto found = values.find(key);
  if (found == values.end()) {
    return Fail(ConfigErrorCode::kMissingKey, std::string(key));
  }
  if (found->second.empty()) {
    return Fail(ConfigErrorCode::kEmptyValue, std::string(key));
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
    return Fail(ConfigErrorCode::kInvalidNumber, std::string(key));
  }
  return number;
}

std::expected<std::uint8_t, ConfigError> RequireTickRate(const ScalarMap& values, std::string_view key) {
  const auto text = RequireString(values, key);
  if (!text) {
    return std::unexpected(text.error());
  }
  std::uint32_t number = 0;
  const char* const end = text->data() + text->size();
  const auto parsed = std::from_chars(text->data(), end, number);
  if (parsed.ec != std::errc{} || parsed.ptr != end || number == 0 ||
      number > std::numeric_limits<std::uint8_t>::max()) {
    return Fail(ConfigErrorCode::kInvalidNumber, std::string(key));
  }
  return static_cast<std::uint8_t>(number);
}

// The fallback when key is absent; when present, a finite number above zero.
std::expected<float, ConfigError> OptionalPositiveNumber(const ScalarMap& values, std::string_view key,
                                                         float fallback) {
  return values.contains(key) ? RequirePositiveNumber(values, key) : fallback;
}

// The client's open section binding each control it names to a key, on top of
// the defaults for the controls it leaves out.
constexpr std::string_view kKeysSection = "input.keys";

// Decision: the keymap the `input.keys` entries of values (control name -> key
// name) make of the defaults. Each control keeps a key of its own, never the
// one that releases the cursor; errors name the entry ("input.keys.<control>").
std::expected<input::Keymap, ConfigError> ParseKeymap(const ScalarMap& values) {
  const std::string prefix = std::format("{}.", kKeysSection);
  auto bindings = values | std::views::filter([&](const auto& value) { return value.first.starts_with(prefix); });
  const auto error = [](ConfigErrorCode code, const std::string& path) { return Fail(code, path); };
  input::Keymap keymap = input::kDefaultKeymap;
  for (const auto& [path, key_name] : bindings) {
    const auto control = input::ControlNamed(std::string_view(path).substr(prefix.size()));
    if (!control) {
      return error(ConfigErrorCode::kUnknownControl, path);
    }
    const auto key = input::KeyNamed(key_name);
    if (!key) {
      return error(ConfigErrorCode::kInvalidKeyName, path);
    }
    if (*key == input::kReleaseCursorKey) {
      return error(ConfigErrorCode::kReservedKey, path);
    }
    keymap.at(static_cast<std::size_t>(*control)) = *key;
  }
  // The defaults never share a key, so any clash involves a control the section rebound.
  for (const auto& [path, key_name] : bindings) {
    if (std::ranges::count(keymap, *input::KeyNamed(key_name)) > 1) {
      return error(ConfigErrorCode::kKeyBoundTwice, path);
    }
  }
  return keymap;
}

std::expected<input::Config, ConfigError> ParseInputConfig(const ScalarMap& values) {
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
    return Fail(ConfigErrorCode::kInvalidLogLevel, std::string(key));
  }
  return value;
}

std::expected<std::string, ConfigError> ReadFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kCannotOpenFile, .subject = {}, .file = file});
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

// Every control's name, comma-separated, for an error that names none of them.
std::string ControlNames() {
  std::string names;
  for (std::size_t i = 0; i < input::kControlCount; ++i) {
    names += std::format("{}{}", i == 0 ? "" : ", ", input::NameOf(static_cast<input::Control>(i)));
  }
  return names;
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
      if (error.subject == "simulation.tick_rate_hz") {
        return std::format("'{}' must be an integer from 1 to 255", error.subject);
      }
      return std::format("'{}' must be a finite number above zero", error.subject);
    case ConfigErrorCode::kInvalidLogLevel:
      return std::format("'{}' must be one of trace, debug, info, warn, error, critical", error.subject);
    case ConfigErrorCode::kNotASection:
      return std::format("'{}' must be a mapping of names to values", error.subject);
    case ConfigErrorCode::kUnknownControl:
      return std::format("'{}' names no control; the controls are {}", error.subject, ControlNames());
    case ConfigErrorCode::kInvalidKeyName:
      return std::format("'{}' must name a key, e.g. W, LeftShift, Space, F1 or MouseRight", error.subject);
    case ConfigErrorCode::kKeyBoundTwice:
      return std::format("'{}' is bound to a key another control already uses", error.subject);
    case ConfigErrorCode::kReservedKey:
      return std::format("'{}' can't use {}: it releases the cursor", error.subject,
                         input::NameOf(input::kReleaseCursorKey));
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
    return Fail(ConfigErrorCode::kInvalidArguments, std::format("{}\n{}", error.what(), usage));
  }

  if (arguments.empty()) {
    const auto directory = ExecutableDirectory();
    if (!directory) {
      return Fail(ConfigErrorCode::kExecutableDirectoryUnknown, std::string(default_file_name));
    }
    return *directory / default_file_name;
  }
  const auto& file = arguments["config"].as<std::string>();
  if (file.empty()) {
    return Fail(ConfigErrorCode::kInvalidArguments, std::format("--config needs a file name\n{}", usage));
  }
  return std::filesystem::path(file);
}

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
  const auto values = ReadMapping(yaml_text, Schema{.keys = kKeys, .open_sections = kOpenSections});
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

std::expected<ServerConfig, ConfigError> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 6> kKeys{
      "base_dir",      "content.pack", "content.public_key", "simulation.tick_rate_hz", "network.listen_address",
      "logging.level",
  };
  const auto values = ReadMapping(yaml_text, Schema{.keys = kKeys, .open_sections = {}});
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
  const auto tick_rate_hz = RequireTickRate(*values, "simulation.tick_rate_hz");
  if (!tick_rate_hz) {
    return std::unexpected(tick_rate_hz.error());
  }
  auto log_level = OptionalLogLevel(*values, "logging.level", kDefaultLogLevel);
  if (!log_level) {
    return std::unexpected(log_level.error());
  }
  return ServerConfig{
      .pack_path = *std::move(pack_path),
      .public_key_path = *std::move(public_key_path),
      .tick_rate_hz = *tick_rate_hz,
      .listen_address = OptionalString(*values, "network.listen_address", kDefaultListenAddress),
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
