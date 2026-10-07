#include "augusta/config.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
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

#include "augusta/logging.h"

namespace augusta::config {

namespace {

// An error about subject, not yet tied to a config file: the Load* functions
// set that once they know it.
std::unexpected<ConfigError> Fail(ConfigErrorCode code, std::string subject = {}) {
  return std::unexpected(ConfigError{.code = code, .subject = std::move(subject), .file = {}});
}

std::optional<std::filesystem::path> ExecutableDirectory() {
  boost::system::error_code error;
  const auto executable = boost::dll::program_location(error);
  if (error) {
    return std::nullopt;
  }
  // native(): a std::wstring on Windows, so non-ASCII directories survive.
  return std::filesystem::path(executable.native()).parent_path();
}

std::string Child(std::string_view parent, std::string_view name) {
  return parent.empty() ? std::string(name) : std::format("{}.{}", parent, name);
}

bool IsKey(const ConfigSchema& schema, std::string_view path) {
  return std::ranges::find(schema.keys, path) != schema.keys.end();
}

// Whether path is a section: an open section, or a prefix of a key or of an open section.
bool IsSection(const ConfigSchema& schema, std::string_view path) {
  // member is path itself or lies under it.
  const auto opens = [&](std::string_view member) {
    return member == path || (member.starts_with(path) && member.size() > path.size() && member[path.size()] == '.');
  };
  return std::ranges::any_of(schema.open_sections, opens) ||
         std::ranges::any_of(schema.keys, [&](std::string_view key) { return key != path && opens(key); });
}

// Whether path is an entry of an open section, which the schema checks by name itself.
bool InOpenSection(const ConfigSchema& schema, std::string_view path) {
  const auto dot = path.rfind('.');
  return dot != std::string_view::npos &&
         std::ranges::find(schema.open_sections, path.substr(0, dot)) != schema.open_sections.end();
}

// Mechanism: reads node, the mapping at section (empty at the top), into
// values under dotted paths. Every scalar must be a key or open-section entry
// of schema and every mapping one of its sections; errors name the path.
std::expected<void, ConfigError> Flatten(const YAML::Node& node, const std::string& section, const ConfigSchema& schema,
                                         ConfigValues& values) {
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
    case ConfigErrorCode::kNotASection:
      return std::format("'{}' must be a mapping of names to values", error.subject);
    case ConfigErrorCode::kUnknownControl:
      return std::format("'{}' names no control", error.subject);
    case ConfigErrorCode::kInvalidKeyName:
      return std::format("'{}' must name a key, e.g. W, LeftShift, Space, F1 or MouseRight", error.subject);
    case ConfigErrorCode::kKeyBoundTwice:
      return std::format("'{}' is bound to a key another control already uses", error.subject);
    case ConfigErrorCode::kReservedKey:
      return std::format("'{}' binds a reserved key", error.subject);
  }
  return "unknown config error";
}

// The usage message ParseCommandLine's help and errors show.
std::string Usage(std::string_view program, std::string_view default_file_name) {
  return std::format(
      "usage: {0} [--config <file>]\n"
      "       {0} --help | --version\n"
      "  --config <file>  the config file, relative to the working directory\n"
      "                   (without it, {1} next to the executable)\n"
      "  --help           print this message and exit\n"
      "  --version        print the version and exit",
      program, default_file_name);
}

}  // namespace

// Mechanism: reads text as a YAML mapping, its sections flattened into dotted
// paths. Which keys exist, and which are required, is the schema's business
// (the Parse* functions).
std::expected<ConfigValues, ConfigError> ReadConfigValues(std::string_view text, const ConfigSchema& schema) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(text));
  } catch (const YAML::Exception& error) {
    return Fail(ConfigErrorCode::kInvalidYaml, error.what());
  }
  if (!root.IsMap()) {
    return Fail(ConfigErrorCode::kNotAMapping);
  }
  ConfigValues values;
  if (auto read = Flatten(root, "", schema, values); !read) {
    return std::unexpected(read.error());
  }
  return values;
}

std::expected<std::string, ConfigError> RequireString(const ConfigValues& values, std::string_view key) {
  const auto found = values.find(key);
  if (found == values.end()) {
    return Fail(ConfigErrorCode::kMissingKey, std::string(key));
  }
  if (found->second.empty()) {
    return Fail(ConfigErrorCode::kEmptyValue, std::string(key));
  }
  return found->second;
}

std::expected<std::filesystem::path, ConfigError> RequirePath(const ConfigValues& values, std::string_view key,
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
std::expected<float, ConfigError> RequirePositiveNumber(const ConfigValues& values, std::string_view key) {
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
std::string OptionalString(const ConfigValues& values, std::string_view key, std::string_view fallback) {
  const auto found = values.find(key);
  return found == values.end() ? std::string(fallback) : found->second;
}

// fallback when key is absent; when present, its value must be one
// augusta::logging::ParseSeverity accepts.
std::expected<std::string, ConfigError> OptionalLogLevel(const ConfigValues& values, std::string_view key,
                                                         std::string_view fallback) {
  auto value = OptionalString(values, key, fallback);
  if (!logging::ParseSeverity(value)) {
    return Fail(ConfigErrorCode::kInvalidLogLevel, std::string(key));
  }
  return value;
}

std::expected<std::string, ConfigError> ReadConfigFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return std::unexpected(ConfigError{.code = ConfigErrorCode::kCannotOpenFile, .subject = {}, .file = file});
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

std::expected<std::uint32_t, ConfigError> RequireWholeNumber(const ConfigValues& values, std::string_view key,
                                                             std::uint32_t min, std::uint32_t max) {
  const auto text = RequireString(values, key);
  if (!text) {
    return std::unexpected(text.error());
  }
  std::uint64_t number = 0;
  const char* const end = text->data() + text->size();
  const auto parsed = std::from_chars(text->data(), end, number);
  if (parsed.ec != std::errc{} || parsed.ptr != end || number < min || number > max) {
    return Fail(ConfigErrorCode::kInvalidNumber, std::string(key));
  }
  return static_cast<std::uint32_t>(number);
}

std::string DescribeConfigError(const ConfigError& error) { return DescribeConfigError(error, Phrase(error)); }

std::string DescribeConfigError(const ConfigError& error, std::string_view phrase) {
  return error.file.empty() ? std::string(phrase) : std::format("{}: {}", error.file.string(), phrase);
}

std::expected<CommandLine, ConfigError> ParseCommandLine(int argc, const char* const* argv, std::string_view program,
                                                         std::string_view default_file_name, std::string_view version) {
  const auto usage = Usage(program, default_file_name);

  namespace po = boost::program_options;
  po::options_description options;
  options.add_options()("config", po::value<std::string>(), "config file, relative to the working directory")(
      "help", "print the usage and exit")("version", "print the version and exit");

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

  // variables_map's own operator[] (an empty value for an absent option), not
  // std::map::contains: MSVC links that through Boost's DLL, which lacks it.
  if (!arguments["help"].empty()) {
    return CommandLine{.config_file = {}, .message = usage, .action = CommandLineAction::kShowHelp};
  }
  if (!arguments["version"].empty()) {
    return CommandLine{.config_file = {},
                       .message = std::format("{} {}", program, version),
                       .action = CommandLineAction::kShowVersion};
  }
  if (arguments["config"].empty()) {
    const auto directory = ExecutableDirectory();
    if (!directory) {
      return Fail(ConfigErrorCode::kExecutableDirectoryUnknown, std::string(default_file_name));
    }
    return CommandLine{.config_file = *directory / default_file_name, .message = {}, .action = CommandLineAction::kRun};
  }
  const auto& file = arguments["config"].as<std::string>();
  if (file.empty()) {
    return Fail(ConfigErrorCode::kInvalidArguments, std::format("--config needs a file name\n{}", usage));
  }
  return CommandLine{.config_file = file, .message = {}, .action = CommandLineAction::kRun};
}

}  // namespace augusta::config
