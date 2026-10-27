#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/config.h"
#include "augusta/server_config.h"

// augustad.yaml's `debug` section, which only a Development build knows
// (ADR-0074); shipping_config_test.cpp has a Shipping build refusing it.
namespace {

using augusta::config::ConfigErrorCode;
using augusta::config::DescribeConfigError;
using augusta::config::DescribeServerConfigError;
using augusta::config::ParseServerConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

constexpr std::string_view kMinimalServerConfig =
    "base_dir: content\ncontent:\n  pack: a.apak\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\n";

std::string ServerConfigWith(std::string_view extra) { return std::string(kMinimalServerConfig) + std::string(extra); }

// A directory of the test's own under the temp folder, empty: ctest runs each
// test in a process of its own, possibly in parallel.
std::filesystem::path FreshDirectory() {
  const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
  const std::filesystem::path directory =
      std::filesystem::path(testing::TempDir()) / (std::string("augusta_server_config_debug_test_") + test.name());
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

// Requirements: US-21
TEST(ParseServerConfigTest, TakesNoReenactmentsByDefault) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->reenactments);
}

// Requirements: US-21
TEST(ParseServerConfigTest, ReadsWhetherReenactmentsAreTaken) {
  const auto on = ParseServerConfig(ServerConfigWith("debug:\n  reenactments: true\n"), kFileDir);
  const auto off = ParseServerConfig(ServerConfigWith("debug:\n  reenactments: false\n"), kFileDir);

  ASSERT_TRUE(on.has_value());
  EXPECT_TRUE(on->reenactments);
  ASSERT_TRUE(off.has_value());
  EXPECT_FALSE(off->reenactments);
}

TEST(ParseServerConfigTest, RejectsReenactmentsThatAreNeitherTrueNorFalse) {
  const auto config = ParseServerConfig(ServerConfigWith("debug:\n  reenactments: yes\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.reenactments");
  EXPECT_EQ(DescribeServerConfigError(config.error()), "'debug.reenactments' must be true or false");
}

// A replay server runs no Match, so it has no player to reenact.
TEST(ParseServerConfigTest, RejectsReenactmentsOnAReplayServer) {
  const auto config =
      ParseServerConfig(ServerConfigWith("debug:\n  reenactments: true\nreplay:\n  captures: captures\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.reenactments");
}

// Requirements: US-31
TEST(ParseServerConfigTest, WritesNoPcapngByDefault) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->network_pcapng.empty());
}

// Requirements: US-31
TEST(ParseServerConfigTest, ReadsThePcapngDirectoryUnderBaseDirAndCreatesIt) {
  const std::filesystem::path file_dir = FreshDirectory();

  const auto config = ParseServerConfig(ServerConfigWith("debug:\n  network:\n    pcapng: captures/net\n"), file_dir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->network_pcapng, file_dir / "content" / "captures" / "net");
  EXPECT_TRUE(std::filesystem::is_directory(file_dir / "content" / "captures" / "net"));
}

// A replay server's traffic is captured as a live server's is.
// Requirements: US-31
TEST(ParseServerConfigTest, ReadsThePcapngDirectoryOnAReplayServer) {
  const std::filesystem::path file_dir = FreshDirectory();

  const auto config = ParseServerConfig(
      ServerConfigWith("debug:\n  network:\n    pcapng: net\nreplay:\n  captures: captures\n"), file_dir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->network_pcapng, file_dir / "content" / "net");
}

// A directory beneath a file can never be created.
// Requirements: US-31
TEST(ParseServerConfigTest, RejectsAPcapngDirectoryThatCannotBeCreated) {
  const std::filesystem::path file_dir = FreshDirectory();
  std::filesystem::create_directories(file_dir / "content");
  std::ofstream(file_dir / "content" / "a-file") << "not a directory";

  const auto config = ParseServerConfig(ServerConfigWith("debug:\n  network:\n    pcapng: a-file/net\n"), file_dir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.network.pcapng");
  EXPECT_EQ(DescribeServerConfigError(config.error()),
            "'debug.network.pcapng' must name a directory that can be written");
}

// An existing directory no file can be written in is refused as one that
// can't be created: here a directory, which it can't remove, stands where the
// write probe's file would go. Without it, the same directory is taken.
// Requirements: US-31
TEST(ParseServerConfigTest, RejectsAnExistingPcapngDirectoryThatCannotBeWritten) {
  const std::filesystem::path file_dir = FreshDirectory();
  const std::filesystem::path directory = file_dir / "content" / "net";
  std::filesystem::create_directories(directory);
  const std::string yaml = ServerConfigWith("debug:\n  network:\n    pcapng: net\n");
  ASSERT_TRUE(ParseServerConfig(yaml, file_dir).has_value());
  std::filesystem::create_directories(directory / ".augusta-write-probe" / "in-the-way");

  const auto config = ParseServerConfig(yaml, file_dir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.network.pcapng");
  EXPECT_EQ(DescribeServerConfigError(config.error()),
            "'debug.network.pcapng' must name a directory that can be written");
}

TEST(ParseServerConfigTest, SendsNoShotJudgementsByDefault) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->shot_judgements);
}

TEST(ParseServerConfigTest, ReadsWhetherShotJudgementsAreSent) {
  const auto on = ParseServerConfig(ServerConfigWith("debug:\n  shot_judgements: true\n"), kFileDir);
  const auto off = ParseServerConfig(ServerConfigWith("debug:\n  shot_judgements: false\n"), kFileDir);

  ASSERT_TRUE(on.has_value());
  EXPECT_TRUE(on->shot_judgements);
  ASSERT_TRUE(off.has_value());
  EXPECT_FALSE(off->shot_judgements);
}

TEST(ParseServerConfigTest, RejectsShotJudgementsThatAreNeitherTrueNorFalse) {
  const auto config = ParseServerConfig(ServerConfigWith("debug:\n  shot_judgements: 1\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.shot_judgements");
}

// A replay server runs no Match, so no round of it is judged.
TEST(ParseServerConfigTest, RejectsShotJudgementsOnAReplayServer) {
  const auto config =
      ParseServerConfig(ServerConfigWith("debug:\n  shot_judgements: true\nreplay:\n  captures: captures\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.shot_judgements");
}

TEST(ParseServerConfigTest, RecordsNoPhysicsByDefault) {
  const auto config = ParseServerConfig(kMinimalServerConfig, kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->physics_recording);
}

TEST(ParseServerConfigTest, ReadsWhetherPhysicsIsRecorded) {
  const auto on = ParseServerConfig(ServerConfigWith("debug:\n  physics_recording: true\n"), kFileDir);
  const auto off = ParseServerConfig(ServerConfigWith("debug:\n  physics_recording: false\n"), kFileDir);

  ASSERT_TRUE(on.has_value());
  EXPECT_TRUE(on->physics_recording);
  ASSERT_TRUE(off.has_value());
  EXPECT_FALSE(off->physics_recording);
}

TEST(ParseServerConfigTest, RejectsAPhysicsRecordingThatIsNeitherTrueNorFalse) {
  const auto config = ParseServerConfig(ServerConfigWith("debug:\n  physics_recording: on\n"), kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.physics_recording");
}

}  // namespace
