#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/client_config.h"
#include "augusta/config.h"

// augustac.yaml's `debug` section, which only a Development build knows
// (ADR-0074); shipping_config_test.cpp has a Shipping build refusing it.
namespace {

using augusta::config::ConfigErrorCode;
using augusta::config::DescribeConfigError;
using augusta::config::ParseClientConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

constexpr std::string_view kMinimalClientConfig =
    "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n";

std::string ClientConfigWith(std::string_view extra) { return std::string(kMinimalClientConfig) + std::string(extra); }

// A directory of the test's own under the temp folder, empty: ctest runs each
// test in a process of its own, possibly in parallel.
std::filesystem::path FreshDirectory() {
  const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
  const std::filesystem::path directory =
      std::filesystem::path(testing::TempDir()) / (std::string("augusta_client_config_debug_test_") + test.name());
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

TEST(ParseClientConfigTest, TheDebugHudIsOnByDefault) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->debug.hud);
}

TEST(ParseClientConfigTest, DebugHudFalseTurnsTheHudOff) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\ndebug:\n  hud: false\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->debug.hud);
}

TEST(ParseClientConfigTest, RejectsADebugHudThatIsNotTrueOrFalse) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\ndebug:\n  hud: maybe\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.hud");
}

TEST(ParseClientConfigTest, TheProfilingPanelIsOffByDefault) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->debug.profiling_panel);
}

TEST(ParseClientConfigTest, DebugProfilingPanelTrueTurnsThePanelOn) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\ndebug:\n  profiling:\n    panel: true\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->debug.profiling_panel);
}

TEST(ParseClientConfigTest, RejectsADebugProfilingPanelThatIsNotTrueOrFalse) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\ndebug:\n  profiling:\n    panel: maybe\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.profiling.panel");
}

TEST(ParseClientConfigTest, WritesNoPcapngByDefault) {
  const auto config = ParseClientConfig(kMinimalClientConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->debug.network_pcapng.empty());
}

TEST(ParseClientConfigTest, ReadsThePcapngDirectoryUnderBaseDirAndCreatesIt) {
  const std::filesystem::path file_dir = FreshDirectory();

  const auto config = ParseClientConfig(ClientConfigWith("debug:\n  network:\n    pcapng: captures/net\n"), file_dir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->debug.network_pcapng, file_dir / "content" / "captures" / "net");
  EXPECT_TRUE(std::filesystem::is_directory(file_dir / "content" / "captures" / "net"));
}

// A directory beneath a file can never be created.
TEST(ParseClientConfigTest, RejectsAPcapngDirectoryThatCannotBeCreated) {
  const std::filesystem::path file_dir = FreshDirectory();
  std::filesystem::create_directories(file_dir / "content");
  std::ofstream(file_dir / "content" / "a-file") << "not a directory";

  const auto config = ParseClientConfig(ClientConfigWith("debug:\n  network:\n    pcapng: a-file/net\n"), file_dir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.network.pcapng");
  EXPECT_EQ(DescribeConfigError(config.error()), "'debug.network.pcapng' must name a directory that can be written");
}

// An existing directory no file can be written in is refused as one that
// can't be created: here a directory, which it can't remove, stands where the
// write probe's file would go. Without it, the same directory is taken.
TEST(ParseClientConfigTest, RejectsAnExistingPcapngDirectoryThatCannotBeWritten) {
  const std::filesystem::path file_dir = FreshDirectory();
  const std::filesystem::path directory = file_dir / "content" / "net";
  std::filesystem::create_directories(directory);
  const std::string yaml = ClientConfigWith("debug:\n  network:\n    pcapng: net\n");
  ASSERT_TRUE(ParseClientConfig(yaml, file_dir).has_value());
  std::filesystem::create_directories(directory / ".augusta-write-probe" / "in-the-way");

  const auto config = ParseClientConfig(yaml, file_dir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.network.pcapng");
  EXPECT_EQ(DescribeConfigError(config.error()), "'debug.network.pcapng' must name a directory that can be written");
}

}  // namespace
