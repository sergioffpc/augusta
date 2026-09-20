#include "augusta/config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/program_options.hpp>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>

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
std::expected<ScalarMap, std::string> ReadScalarMap(std::string_view text,
                                                    std::span<const std::string_view> allowed_keys) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(text));
  } catch (const YAML::Exception& error) {
    return std::unexpected(std::format("invalid YAML: {}", error.what()));
  }
  if (!root.IsMap()) {
    return std::unexpected("the top level must be a mapping of keys to values");
  }

  ScalarMap values;
  for (const auto& entry : root) {
    if (!entry.first.IsScalar()) {
      return std::unexpected("every key must be a plain string");
    }
    const std::string key = entry.first.Scalar();
    if (std::ranges::find(allowed_keys, key) == allowed_keys.end()) {
      return std::unexpected(std::format("unknown key '{}'", key));
    }
    if (!entry.second.IsScalar()) {
      return std::unexpected(std::format("'{}' must be a string", key));
    }
    if (!values.emplace(key, entry.second.Scalar()).second) {
      return std::unexpected(std::format("duplicate key '{}'", key));
    }
  }
  return values;
}

std::expected<std::string, std::string> RequireString(const ScalarMap& values, std::string_view key) {
  const auto found = values.find(key);
  if (found == values.end()) {
    return std::unexpected(std::format("missing required key '{}'", key));
  }
  if (found->second.empty()) {
    return std::unexpected(std::format("'{}' must not be empty", key));
  }
  return found->second;
}

std::expected<std::filesystem::path, std::string> RequirePath(const ScalarMap& values, std::string_view key,
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

std::string OptionalString(const ScalarMap& values, std::string_view key, std::string_view fallback) {
  const auto found = values.find(key);
  return found == values.end() ? std::string(fallback) : found->second;
}

std::expected<std::string, std::string> ReadFile(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return std::unexpected(std::format("{}: cannot open the config file", file.string()));
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

template <typename Config, typename ParseFn>
std::expected<Config, std::string> LoadFile(const std::filesystem::path& file, ParseFn parse) {
  const auto text = ReadFile(file);
  if (!text) {
    return std::unexpected(text.error());
  }
  auto config = parse(*text, file.parent_path());
  if (!config) {
    return std::unexpected(std::format("{}: {}", file.string(), config.error()));
  }
  return config;
}

}  // namespace

std::expected<std::filesystem::path, std::string> ResolveConfigFile(int argc, const char* const* argv,
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
    return std::unexpected(std::format("{}\n{}", error.what(), usage));
  }

  if (arguments.empty()) {
    const auto directory = ExecutableDirectory();
    if (!directory) {
      return std::unexpected(std::format("cannot locate the executable's directory to find {}", default_file_name));
    }
    return *directory / default_file_name;
  }
  const auto& file = arguments["config"].as<std::string>();
  if (file.empty()) {
    return std::unexpected(std::format("--config needs a file name\n{}", usage));
  }
  return std::filesystem::path(file);
}

std::expected<ClientConfig, std::string> ParseClientConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 4> kKeys{"base_dir", "pack", "public_key", "server_address"};
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
  return ClientConfig{.pack_path = *std::move(pack_path),
                      .public_key_path = *std::move(public_key_path),
                      .server_address = OptionalString(*values, "server_address", kDefaultServerAddress)};
}

std::expected<ServerConfig, std::string> ParseServerConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir) {
  static constexpr std::array<std::string_view, 4> kKeys{"base_dir", "pack", "public_key", "listen_address"};
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
  return ServerConfig{.pack_path = *std::move(pack_path),
                      .public_key_path = *std::move(public_key_path),
                      .listen_address = OptionalString(*values, "listen_address", kDefaultListenAddress)};
}

std::expected<ClientConfig, std::string> LoadClientConfig(const std::filesystem::path& file) {
  return LoadFile<ClientConfig>(file, ParseClientConfig);
}

std::expected<ServerConfig, std::string> LoadServerConfig(const std::filesystem::path& file) {
  return LoadFile<ServerConfig>(file, ParseServerConfig);
}

}  // namespace augusta::config
