#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/client_config.h"
#include "augusta/config.h"

// augustac.yaml's `debug` section, which only a Development build knows
// (ADR-0074); shipping_config_test.cpp has a Shipping build refusing it.
namespace {

using augusta::config::ConfigErrorCode;
using augusta::config::ParseClientConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

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

// One `debug.physics` switch: its key under the section, and where it is read to.
struct PhysicsSwitch {
  std::string_view key;
  bool augusta::config::ClientPhysicsDebugConfig::* read;
};

class PhysicsSwitchTest : public testing::TestWithParam<PhysicsSwitch> {
 protected:
  // A config whose `debug.physics` section sets this switch to value.
  [[nodiscard]] static auto ParseWith(std::string_view value) {
    return ParseClientConfig(
        std::format("base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  "
                    "public_key: k.pub\ndebug:\n  physics:\n    {}: {}\n",
                    GetParam().key, value),
        kFileDir);
  }
};

TEST_P(PhysicsSwitchTest, IsOffByDefault) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->debug.physics.*GetParam().read);
}

TEST_P(PhysicsSwitchTest, TrueTurnsItOn) {
  const auto config = ParseWith("true");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->debug.physics.*GetParam().read);
}

TEST_P(PhysicsSwitchTest, RejectsAValueThatIsNotTrueOrFalse) {
  const auto config = ParseWith("maybe");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, std::format("debug.physics.{}", GetParam().key));
}

INSTANTIATE_TEST_SUITE_P(
    ParseClientConfigTest, PhysicsSwitchTest,
    testing::Values(PhysicsSwitch{.key = "panel", .read = &augusta::config::ClientPhysicsDebugConfig::panel},
                    PhysicsSwitch{.key = "collision", .read = &augusta::config::ClientPhysicsDebugConfig::collision},
                    PhysicsSwitch{.key = "server_bodies",
                                  .read = &augusta::config::ClientPhysicsDebugConfig::server_bodies}),
    [](const testing::TestParamInfo<PhysicsSwitch>& info) { return std::string(info.param.key); });

}  // namespace
