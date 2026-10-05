#include "settings.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/config.h"

// augusta-loadtest.yaml's keys, read under ADR-0034's rules.
namespace {

using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::loadtest::DescribeSettingsError;
using augusta::loadtest::LoadSettings;
using augusta::loadtest::ParseSettings;
using augusta::loadtest::Settings;

// The directory of the (imaginary) settings file: what a relative base_dir is relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";
// A relative path in the settings below resolves under this one.
const std::filesystem::path kRoot = kFileDir / "content";

// Every required key outside the run section, before what a test adds.
constexpr std::string_view kBase =
    "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n";

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::expected<Settings, ConfigError> Parse(std::string_view rest) {
  return ParseSettings(std::string(kBase) + std::string(rest), kFileDir);
}

TEST(ParseSettingsTest, ReadsEveryKey) {
  const auto settings = Parse(
      "network:\n  server_address: 10.0.0.5:27016\nlogging:\n  level: warn\nrun:\n  matches: '3'\n  "
      "timeout_seconds: '90.5'\n  seed: '42'\n");

  ASSERT_TRUE(settings.has_value()) << DescribeSettingsError(settings.error());
  EXPECT_EQ(settings->pack_path, kRoot / "a.pack");
  EXPECT_EQ(settings->public_key_path, kRoot / "k.pub");
  EXPECT_EQ(settings->character, "characters/player");
  EXPECT_EQ(settings->server_address, "10.0.0.5:27016");
  EXPECT_EQ(settings->log_level, "warn");
  EXPECT_EQ(settings->matches, 3);
  EXPECT_FLOAT_EQ(settings->timeout_seconds, 90.5F);
  EXPECT_EQ(settings->seed, 42U);
}

TEST(ParseSettingsTest, DefaultsTheServerAddressLogLevelAndSeed) {
  const auto settings = Parse("run:\n  matches: '1'\n  timeout_seconds: '60'\n");

  ASSERT_TRUE(settings.has_value()) << DescribeSettingsError(settings.error());
  EXPECT_EQ(settings->server_address, augusta::config::kDefaultServerAddress);
  EXPECT_EQ(settings->log_level, augusta::config::kDefaultLogLevel);
  EXPECT_EQ(settings->seed, augusta::loadtest::kDefaultSeed);
}

TEST(ParseSettingsTest, RequiresTheMatchesAndTheTimeout) {
  const auto no_matches = Parse("run:\n  timeout_seconds: '60'\n");
  const auto no_timeout = Parse("run:\n  matches: '1'\n");

  ASSERT_FALSE(no_matches.has_value());
  EXPECT_EQ(no_matches.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(no_matches.error().subject, "run.matches");
  ASSERT_FALSE(no_timeout.has_value());
  EXPECT_EQ(no_timeout.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(no_timeout.error().subject, "run.timeout_seconds");
}

TEST(ParseSettingsTest, RejectsMatchesThatAreNotAnIntegerFromOneTo255) {
  for (const std::string_view matches : {"0", "256", "1.5", "-1", "two"}) {
    const auto settings =
        Parse(std::string("run:\n  matches: '") + std::string(matches) + "'\n  timeout_seconds: '60'\n");

    ASSERT_FALSE(settings.has_value()) << matches;
    EXPECT_EQ(settings.error().code, ConfigErrorCode::kInvalidNumber) << matches;
    EXPECT_EQ(settings.error().subject, "run.matches") << matches;
    EXPECT_TRUE(Contains(DescribeSettingsError(settings.error()), "integer from 1 to 255")) << matches;
  }
}

TEST(ParseSettingsTest, RejectsATimeoutThatIsNotAFiniteNumberAboveZero) {
  for (const std::string_view timeout : {"0", "-5", "inf", "soon"}) {
    const auto settings =
        Parse(std::string("run:\n  matches: '1'\n  timeout_seconds: '") + std::string(timeout) + "'\n");

    ASSERT_FALSE(settings.has_value()) << timeout;
    EXPECT_EQ(settings.error().code, ConfigErrorCode::kInvalidNumber) << timeout;
    EXPECT_EQ(settings.error().subject, "run.timeout_seconds") << timeout;
    EXPECT_TRUE(Contains(DescribeSettingsError(settings.error()), "finite number above zero")) << timeout;
  }
}

TEST(ParseSettingsTest, AcceptsAnyUnsigned32BitSeed) {
  for (const std::string_view seed : {"0", "4294967295"}) {
    const auto settings =
        Parse(std::string("run:\n  matches: '1'\n  timeout_seconds: '60'\n  seed: '") + std::string(seed) + "'\n");

    ASSERT_TRUE(settings.has_value()) << seed;
  }
}

TEST(ParseSettingsTest, RejectsASeedThatIsNotAnUnsigned32BitInteger) {
  for (const std::string_view seed : {"4294967296", "-1", "1.5", "random"}) {
    const auto settings =
        Parse(std::string("run:\n  matches: '1'\n  timeout_seconds: '60'\n  seed: '") + std::string(seed) + "'\n");

    ASSERT_FALSE(settings.has_value()) << seed;
    EXPECT_EQ(settings.error().code, ConfigErrorCode::kInvalidNumber) << seed;
    EXPECT_EQ(settings.error().subject, "run.seed") << seed;
    EXPECT_TRUE(Contains(DescribeSettingsError(settings.error()), "integer from 0 to 4294967295")) << seed;
  }
}

TEST(ParseSettingsTest, RejectsAKeyItDoesNotHaveSuchAsTheClientsInput) {
  const auto settings = Parse("run:\n  matches: '1'\n  timeout_seconds: '60'\ninput:\n  mouse_sensitivity: '1'\n");

  ASSERT_FALSE(settings.has_value());
  EXPECT_EQ(settings.error().code, ConfigErrorCode::kUnknownKey);
}

TEST(LoadSettingsTest, NamesTheFileWhenItCannotBeOpened) {
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "augusta-loadtest-missing.yaml";

  const auto settings = LoadSettings(file);

  ASSERT_FALSE(settings.has_value());
  EXPECT_EQ(settings.error().code, ConfigErrorCode::kCannotOpenFile);
  EXPECT_EQ(settings.error().file, file);
}

// The example is what a new setup is copied from, so a test keeps it loading.
TEST(LoadSettingsTest, TheExampleSettingsLoad) {
  const auto settings = LoadSettings(AUGUSTA_LOADTEST_EXAMPLE_SETTINGS);

  ASSERT_TRUE(settings.has_value()) << DescribeSettingsError(settings.error());
}

}  // namespace
