#include "augusta/server_config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/config.h"

// Unit tests for augusta_server_config: augustad.yaml's schema and loading it
// from a file (ADR-0034). Links no client module: the headless server's config
// has no client Input in it.
namespace {

using augusta::config::ConfigErrorCode;
using augusta::config::DescribeServerConfigError;
using augusta::config::LoadServerConfig;
using augusta::config::ParseServerConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

// A relative path in a valid config resolves under this one.
const std::filesystem::path kRoot = kFileDir / "content";

TEST(ParseServerConfigTest, InputIsNotAServerSection) {
  const auto config = ParseServerConfig(
      "base_dir: content\ncontent:\n  pack: a.pack\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\ninput:\n  "
      "keys:\n    sprint: Space\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "input");
}

TEST(ExampleConfigTest, TheExampleServerConfigLoads) {
  const auto config = LoadServerConfig(AUGUSTA_EXAMPLE_SERVER_CONFIG);

  ASSERT_TRUE(config.has_value()) << DescribeServerConfigError(config.error());
  EXPECT_EQ(config->tick_rate_hz, 60);
}

TEST(ParseServerConfigTest, ReadsEveryKey) {
  const auto config = ParseServerConfig(
      "base_dir: content\ncontent:\n  pack: packs/level.server.pack\n  public_key: keys/signing.pub\nsimulation:\n  "
      "tick_rate_hz: 30\nnetwork:\n  listen_address: 0.0.0.0:27016\nmetrics:\n  port: 9100\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.server.pack");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "signing.pub");
  EXPECT_EQ(config->tick_rate_hz, 30);
  EXPECT_EQ(config->listen_address, "0.0.0.0:27016");
  EXPECT_EQ(config->metrics_port, 9100);
}

// Everything a server config needs but the tick rate, so a test can set that itself.
constexpr std::string_view kServerConfigWithoutTickRate =
    "base_dir: content\ncontent:\n  pack: a.pack\n  public_key: k.pub\n";

constexpr std::string_view kMinimalServerConfig =
    "base_dir: content\ncontent:\n  pack: a.pack\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\n";

std::string ServerConfigWith(std::string_view extra) { return std::string(kMinimalServerConfig) + std::string(extra); }

TEST(ParseServerConfigTest, DefaultsTheListenAddress) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->listen_address, augusta::config::kDefaultListenAddress);
}

// Requirements: US-21
TEST(ParseServerConfigTest, RecordsNoMatchByDefault) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->recording_path.empty());
}

// Requirements: US-21
TEST(ParseServerConfigTest, ReadsARecordingPathRelativeToTheBaseDir) {
  const auto config = ParseServerConfig(
      std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: 60\n  recording: logs/match.rec\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->recording_path, kRoot / "logs" / "match.rec");
}

TEST(ParseServerConfigTest, RejectsAnEmptyRecordingPath) {
  const auto config = ParseServerConfig(
      std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: 60\n  recording: \"\"\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "simulation.recording");
}

TEST(ParseServerConfigTest, RecordsOptionallyByDefault) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->strict_recording);
}

TEST(ParseServerConfigTest, ReadsEitherRecordingMode) {
  const auto strict = ParseServerConfig(
      std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: 60\n  recording_mode: strict\n",
      kFileDir);
  const auto optional = ParseServerConfig(
      std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: 60\n  recording_mode: optional\n",
      kFileDir);

  ASSERT_TRUE(strict.has_value());
  EXPECT_TRUE(strict->strict_recording);
  ASSERT_TRUE(optional.has_value());
  EXPECT_FALSE(optional->strict_recording);
}

TEST(ParseServerConfigTest, RejectsARecordingModeThatIsNeitherOptionalNorStrict) {
  const auto config = ParseServerConfig(
      std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: 60\n  recording_mode: required\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "simulation.recording_mode");
  EXPECT_EQ(DescribeServerConfigError(config.error()), "'simulation.recording_mode' must be optional or strict");
}

TEST(ParseServerConfigTest, DefaultsTheLogLevel) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, augusta::config::kDefaultLogLevel);
}

TEST(ParseServerConfigTest, ReadsALogLevel) {
  const auto config = ParseServerConfig(ServerConfigWith("logging:\n  level: trace\n"), kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, "trace");
}

TEST(ParseServerConfigTest, RejectsAnInvalidLogLevel) {
  const auto config = ParseServerConfig(ServerConfigWith("logging:\n  level: verbose\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidLogLevel);
  EXPECT_EQ(config.error().subject, "logging.level");
}

TEST(ParseServerConfigTest, RejectsAMissingTickRate) {
  const auto config = ParseServerConfig(kServerConfigWithoutTickRate, kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "simulation.tick_rate_hz");
}

TEST(ParseServerConfigTest, AcceptsIntegerTickRatesFromOneTo255) {
  for (const char* rate : {"1", "30", "60", "240", "255"}) {
    const auto config = ParseServerConfig(
        std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: " + std::string(rate) + "\n",
        kFileDir);

    ASSERT_TRUE(config.has_value()) << rate;
    EXPECT_EQ(config->tick_rate_hz, std::stoi(rate)) << rate;
  }
}

TEST(ParseServerConfigTest, RejectsATickRateThatIsNotAnIntegerFromOneTo255) {
  for (const char* rate : {"0", "-60", "256", "59.94", "0.5", "1e2", "abc", "60hz", "6 0", ".inf", ".nan", "0x10"}) {
    const auto config = ParseServerConfig(
        std::string(kServerConfigWithoutTickRate) + "simulation:\n  tick_rate_hz: '" + std::string(rate) + "'\n",
        kFileDir);

    ASSERT_FALSE(config.has_value()) << rate;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidNumber) << rate;
    EXPECT_EQ(config.error().subject, "simulation.tick_rate_hz") << rate;
  }
}

TEST(ParseServerConfigTest, DefaultsTheMetricsPort) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->metrics_port, augusta::config::kDefaultMetricsPort);
}

TEST(ParseServerConfigTest, AcceptsMetricsPortsFromOneTo65535) {
  for (const char* port : {"1", "9100", "9464", "65535"}) {
    const auto config =
        ParseServerConfig(ServerConfigWith("metrics:\n  port: '" + std::string(port) + "'\n"), kFileDir);

    ASSERT_TRUE(config.has_value()) << port;
    EXPECT_EQ(config->metrics_port, std::stoi(port)) << port;
  }
}

TEST(ParseServerConfigTest, RejectsAMetricsPortThatIsNotAnIntegerFromOneTo65535) {
  for (const char* port : {"0", "-1", "65536", "9464.5", "abc", "", "0x2508"}) {
    const auto config =
        ParseServerConfig(ServerConfigWith("metrics:\n  port: '" + std::string(port) + "'\n"), kFileDir);

    ASSERT_FALSE(config.has_value()) << port;
    EXPECT_EQ(config.error().subject, "metrics.port") << port;
  }
}

TEST(ParseServerConfigTest, RejectsAStaminaKeyLeftInTheFileAsUnknown) {
  // The stamina rules live in the Parameters script (ADR-0039), not here.
  for (const char* key : {"stamina_deplete_per_second", "stamina_regen_per_second", "stamina_forced_walk_below"}) {
    const auto config = ParseServerConfig(ServerConfigWith(std::string(key) + ": 0.1\n"), kFileDir);

    ASSERT_FALSE(config.has_value()) << key;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey) << key;
    EXPECT_EQ(config.error().subject, key);
  }
}

// Requirements: US-22
TEST(ParseServerConfigTest, AParametersKeyLeftInTheFileIsUnknown) {
  // The Parameters script is the scenario's, cooked into its server pack (ADR-0039),
  // so the config no longer names one.
  const auto config = ParseServerConfig(ServerConfigWith("parameters: p.lua\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "parameters");
}

TEST(ParseServerConfigTest, RejectsAMissingBaseDir) {
  const auto config =
      ParseServerConfig("content:\n  pack: a.pack\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseServerConfigTest, RejectsAKeyThatBelongsToTheClient) {
  const auto config = ParseServerConfig(ServerConfigWith("network:\n  server_address: x\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "network.server_address");
}

TEST(DescribeServerConfigErrorTest, SaysWhatATickRateMustBe) {
  const auto message = DescribeServerConfigError(
      {.code = ConfigErrorCode::kInvalidNumber, .subject = "simulation.tick_rate_hz", .reason = {}, .file = {}});

  EXPECT_EQ(message, "'simulation.tick_rate_hz' must be an integer from 1 to 255");
}

TEST(DescribeServerConfigErrorTest, SaysWhatAMetricsPortMustBe) {
  const auto message = DescribeServerConfigError(
      {.code = ConfigErrorCode::kInvalidNumber, .subject = "metrics.port", .reason = {}, .file = {}});

  EXPECT_EQ(message, "'metrics.port' must be an integer from 1 to 65535");
}

class LoadServerConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Named after the running test: ctest runs every test in a process of its
    // own, possibly in parallel, so no other test removes it while in use.
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string("augusta_server_config_test_") + test.test_suite_name() + "_" + test.name();
    std::ranges::replace(name, '/', '_');
    directory_ = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path Write(std::string_view name, std::string_view contents) const {
    const auto path = directory_ / name;
    std::ofstream(path, std::ios::binary) << contents;
    return path;
  }

  std::filesystem::path directory_;
};

TEST_F(LoadServerConfigTest, ReadsTheFileAndResolvesBaseDirAgainstItsDirectory) {
  const auto file = Write("augustad.yaml",
                          "base_dir: .\ncontent:\n  pack: level.pack\n  public_key: k.pub\nsimulation:\n  "
                          "tick_rate_hz: 60\nnetwork:\n  listen_address: 0.0.0.0:1\n");

  const auto config = LoadServerConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "level.pack");
  EXPECT_EQ(config->listen_address, "0.0.0.0:1");
}

}  // namespace
