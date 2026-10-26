#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <boost/program_options.hpp>

#include "augusta/config.h"
#include "fail.h"

namespace augusta::config {

namespace {

// How option reads in the usage: `--name` or `--name <value>`.
std::string Spelled(const CommandLineOption& option) {
  return option.value.empty() ? std::format("--{}", option.name) : std::format("--{} <{}>", option.name, option.value);
}

// The usage message ParseCommandLine's help and errors show. Descriptions
// start in one column, kDescriptionGap past the longest option.
std::string Usage(std::string_view program, std::string_view default_file_name,
                  std::span<const CommandLineOption> options) {
  constexpr std::size_t kDescriptionGap = 4;
  constexpr std::string_view kConfig = "--config <file>";
  std::size_t width = kConfig.size();
  for (const CommandLineOption& option : options) {
    width = std::max(width, Spelled(option).size());
  }
  width += kDescriptionGap;
  std::string synopsis;
  std::string described;
  for (const CommandLineOption& option : options) {
    synopsis += std::format(" [{}]", Spelled(option));
    described += std::format("\n  {:<{}}{}", Spelled(option), width, option.description);
  }
  return std::format(
      "usage: {0} [{4}]{2}\n"
      "       {0} --help | --version\n"
      "  {4:<{5}}the config file, relative to the working directory\n"
      "  {6:<{5}}(without it, {1} next to the executable){3}\n"
      "  {7:<{5}}print this message and exit\n"
      "  {8:<{5}}print the version and exit",
      program, default_file_name, synopsis, described, kConfig, width, "", "--help", "--version");
}

// What Boost reads a command line by: the shared options, then the
// executable's own.
boost::program_options::options_description Described(std::span<const CommandLineOption> options) {
  namespace po = boost::program_options;
  po::options_description described;
  described.add_options()("config", po::value<std::string>(), "config file, relative to the working directory")(
      "help", "print the usage and exit")("version", "print the version and exit");
  for (const CommandLineOption& option : options) {
    const std::string name(option.name);
    const std::string description(option.description);
    if (option.value.empty()) {
      described.add_options()(name.c_str(), description.c_str());
    } else {
      described.add_options()(name.c_str(), boost::program_options::value<std::string>(), description.c_str());
    }
  }
  return described;
}

// Each of options arguments holds, by name, with its value, empty for one that takes none.
std::map<std::string, std::string, std::less<>> Given(const boost::program_options::variables_map& arguments,
                                                      std::span<const CommandLineOption> options) {
  std::map<std::string, std::string, std::less<>> given;
  for (const CommandLineOption& option : options) {
    const std::string name(option.name);
    if (!arguments[name].empty()) {
      given.emplace(name, option.value.empty() ? std::string() : arguments[name].as<std::string>());
    }
  }
  return given;
}

}  // namespace

std::expected<CommandLine, ConfigError> ParseCommandLine(int argc, const char* const* argv, std::string_view program,
                                                         std::string_view default_file_name, std::string_view version,
                                                         std::span<const CommandLineOption> options) {
  const auto usage = Usage(program, default_file_name, options);

  namespace po = boost::program_options;

  // Prefix guessing is off so `--conf` is an error, not a silent `--config`;
  // the empty positional description makes a bare argument an error too,
  // where Boost would otherwise ignore it.
  po::variables_map arguments;
  try {
    po::store(po::command_line_parser(argc, argv)
                  .options(Described(options))
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
    return CommandLine{.config_file = {}, .message = usage, .options = {}, .action = CommandLineAction::kShowHelp};
  }
  if (!arguments["version"].empty()) {
    return CommandLine{.config_file = {},
                       .message = std::format("{} {}", program, version),
                       .options = {},
                       .action = CommandLineAction::kShowVersion};
  }
  std::map<std::string, std::string, std::less<>> given = Given(arguments, options);
  if (arguments["config"].empty()) {
    const auto directory = ExecutableDirectory();
    if (!directory) {
      return Fail(ConfigErrorCode::kExecutableDirectoryUnknown, std::string(default_file_name));
    }
    return CommandLine{.config_file = *directory / default_file_name,
                       .message = {},
                       .options = std::move(given),
                       .action = CommandLineAction::kRun};
  }
  const auto& file = arguments["config"].as<std::string>();
  if (file.empty()) {
    return Fail(ConfigErrorCode::kInvalidArguments, std::format("--config needs a file name\n{}", usage));
  }
  return CommandLine{
      .config_file = file, .message = {}, .options = std::move(given), .action = CommandLineAction::kRun};
}

}  // namespace augusta::config
