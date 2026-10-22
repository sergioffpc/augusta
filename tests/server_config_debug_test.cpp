#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/config.h"
#include "augusta/server_config.h"

// augustad.yaml's `debug` section, which only a Development build knows
// (ADR-0074); shipping_config_test.cpp has a Shipping build refusing it.
namespace {

using augusta::config::ConfigErrorCode;
using augusta::config::DescribeServerConfigError;
using augusta::config::ParseServerConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

constexpr std::string_view kMinimalServerConfig =
    "base_dir: content\ncontent:\n  pack: a.apak\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\n";

std::string ServerConfigWith(std::string_view extra) { return std::string(kMinimalServerConfig) + std::string(extra); }

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

}  // namespace
