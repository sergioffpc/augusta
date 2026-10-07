#include "augusta/config.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

// Unit tests for augusta_config's shared mechanism: the command line and how
// an error reads (ADR-0034). Each side's own file is client_config_test.cpp's
// and server_config_test.cpp's.
namespace {

using augusta::config::CommandLine;
using augusta::config::CommandLineAction;
using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::config::DescribeConfigError;
using augusta::config::ParseCommandLine;

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::expected<CommandLine, ConfigError> Parse(std::vector<const char*> args) {
  args.insert(args.begin(), "augustac");
  return ParseCommandLine(static_cast<int>(args.size()), args.data(), "augustac", "augustac.yaml", "1.2.3");
}

TEST(ParseCommandLineTest, WithoutArgumentsRunsWithTheDefaultFileNextToTheExecutable) {
  const auto command_line = Parse({});

  ASSERT_TRUE(command_line.has_value());
  EXPECT_EQ(command_line->action, CommandLineAction::kRun);
  EXPECT_EQ(command_line->config_file.filename(), "augustac.yaml");
  EXPECT_TRUE(std::filesystem::is_directory(command_line->config_file.parent_path()))
      << command_line->config_file.string();
}

TEST(ParseCommandLineTest, ConfigArgumentNamesTheFileAsGiven) {
  const auto command_line = Parse({"--config", "other/dir/my.yaml"});

  ASSERT_TRUE(command_line.has_value());
  EXPECT_EQ(command_line->action, CommandLineAction::kRun);
  EXPECT_EQ(command_line->config_file, std::filesystem::path("other") / "dir" / "my.yaml");
}

TEST(ParseCommandLineTest, HelpAsksForTheUsage) {
  const auto command_line = Parse({"--help"});

  ASSERT_TRUE(command_line.has_value());
  EXPECT_EQ(command_line->action, CommandLineAction::kShowHelp);
  EXPECT_TRUE(Contains(command_line->message, "usage: augustac [--config <file>]")) << command_line->message;
  EXPECT_TRUE(Contains(command_line->message, "--help")) << command_line->message;
  EXPECT_TRUE(Contains(command_line->message, "--version")) << command_line->message;
}

TEST(ParseCommandLineTest, VersionAsksForTheVersion) {
  const auto command_line = Parse({"--version"});

  ASSERT_TRUE(command_line.has_value());
  EXPECT_EQ(command_line->action, CommandLineAction::kShowVersion);
  EXPECT_EQ(command_line->message, "augustac 1.2.3");
}

TEST(ParseCommandLineTest, HelpWinsOverConfigAndVersion) {
  // As GNU programs do: --help answers whatever else is asked, even a --config
  // that would be rejected on its own.
  for (const auto& args :
       {std::vector<const char*>{"--config", "my.yaml", "--help"}, std::vector<const char*>{"--help", "--config", ""},
        std::vector<const char*>{"--version", "--help"}}) {
    const auto command_line = Parse(args);

    ASSERT_TRUE(command_line.has_value()) << DescribeConfigError(command_line.error());
    EXPECT_EQ(command_line->action, CommandLineAction::kShowHelp);
  }
}

TEST(ParseCommandLineTest, VersionWinsOverConfig) {
  const auto command_line = Parse({"--config", "my.yaml", "--version"});

  ASSERT_TRUE(command_line.has_value());
  EXPECT_EQ(command_line->action, CommandLineAction::kShowVersion);
}

TEST(ParseCommandLineTest, RejectsConfigWithoutAFile) {
  const auto command_line = Parse({"--config"});

  ASSERT_FALSE(command_line.has_value());
  EXPECT_EQ(command_line.error().code, ConfigErrorCode::kInvalidArguments);
  EXPECT_TRUE(Contains(command_line.error().subject, "usage: augustac [--config <file>]"))
      << command_line.error().subject;
}

TEST(ParseCommandLineTest, RejectsAnEmptyFileName) { ASSERT_FALSE(Parse({"--config", ""}).has_value()); }

TEST(ParseCommandLineTest, RejectsAPositionalArgument) {
  // The old `augustac <pack> <key>` invocation must fail, not be half-honored.
  ASSERT_FALSE(Parse({"level.pack", "k.pub"}).has_value());
  ASSERT_FALSE(Parse({"my.yaml"}).has_value());
}

TEST(ParseCommandLineTest, RejectsAnUnknownOptionAndExtraArguments) {
  ASSERT_FALSE(Parse({"--conf", "my.yaml"}).has_value());
  ASSERT_FALSE(Parse({"--config", "a.yaml", "b.yaml"}).has_value());
  ASSERT_FALSE(Parse({"--help", "extra"}).has_value());
  ASSERT_FALSE(Parse({"--vers"}).has_value());
}

TEST(ParseCommandLineTest, UsageNamesTheProgramAndTheDefaultFile) {
  const char* const args[] = {"augustad", "nope"};
  const auto command_line = ParseCommandLine(2, args, "augustad", "augustad.yaml", "1.2.3");

  ASSERT_FALSE(command_line.has_value());
  EXPECT_EQ(command_line.error().code, ConfigErrorCode::kInvalidArguments);
  EXPECT_TRUE(Contains(command_line.error().subject, "usage: augustad [--config <file>]"))
      << command_line.error().subject;
  EXPECT_TRUE(Contains(command_line.error().subject, "augustad.yaml")) << command_line.error().subject;
}

TEST(DescribeConfigErrorTest, NamesTheKeyAndTheFile) {
  const ConfigError error{.code = ConfigErrorCode::kMissingKey, .subject = "pack", .file = "dir/augustac.yaml"};

  const auto message = DescribeConfigError(error);

  EXPECT_TRUE(Contains(message, "missing required key 'pack'")) << message;
  EXPECT_TRUE(Contains(message, std::filesystem::path("dir/augustac.yaml").string())) << message;
}

TEST(DescribeConfigErrorTest, SaysWhatANumberMustBe) {
  const auto message =
      DescribeConfigError({.code = ConfigErrorCode::kInvalidNumber, .subject = "input.mouse_sensitivity", .file = {}});

  EXPECT_EQ(message, "'input.mouse_sensitivity' must be a finite number above zero");
}

TEST(DescribeConfigErrorTest, SaysWhatALogLevelMustBe) {
  const auto message =
      DescribeConfigError({.code = ConfigErrorCode::kInvalidLogLevel, .subject = "log_level", .file = {}});

  EXPECT_EQ(message, "'log_level' must be one of trace, debug, info, warn, error, critical");
}

TEST(DescribeConfigErrorTest, OmitsTheFileWhenThereIsNone) {
  const auto message = DescribeConfigError({.code = ConfigErrorCode::kUnknownKey, .subject = "typo", .file = {}});

  EXPECT_EQ(message, "unknown key 'typo'");
}

TEST(DescribeConfigErrorTest, TakesAPhraseInPlaceOfTheCodesOwn) {
  const ConfigError error{.code = ConfigErrorCode::kInvalidNumber, .subject = "n", .file = "dir/a.yaml"};

  const auto message = DescribeConfigError(error, "'n' must be odd");

  EXPECT_EQ(message, std::filesystem::path("dir/a.yaml").string() + ": 'n' must be odd");
  EXPECT_EQ(DescribeConfigError({.code = ConfigErrorCode::kInvalidNumber, .subject = "n", .file = {}}, "odd"), "odd");
}

}  // namespace
