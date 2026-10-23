#include <filesystem>

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

TEST(ParseClientConfigTest, TheNetworkPanelIsOffByDefault) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->debug.network_panel);
}

TEST(ParseClientConfigTest, DebugNetworkPanelTrueTurnsThePanelOn) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\ndebug:\n  network:\n    panel: true\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->debug.network_panel);
}

TEST(ParseClientConfigTest, RejectsADebugNetworkPanelThatIsNotTrueOrFalse) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\ndebug:\n  network:\n    panel: maybe\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "debug.network.panel");
}

}  // namespace
