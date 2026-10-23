#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/server_config.h"

// Compiled only in a Shipping build (AUGUSTA_DEBUG_TOOLS off, ADR-0074): the
// config readers know no `debug` section there, so a config written for a
// Development build fails to load, naming the key, rather than running half
// configured.
namespace {

using augusta::config::ConfigErrorCode;
using augusta::config::ParseClientConfig;
using augusta::config::ParseServerConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

const std::string kServerConfig =
    "base_dir: content\ncontent:\n  pack: a.apak\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\n";
const std::string kClientConfig =
    "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n";

TEST(ShippingConfigTest, TheServerConfigRefusesADebugSectionAsAnUnknownKey) {
  ASSERT_TRUE(ParseServerConfig(kServerConfig, kFileDir).has_value());

  const auto config = ParseServerConfig(kServerConfig + "debug:\n  reenactments: true\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "debug");
}

TEST(ShippingConfigTest, TheClientConfigRefusesADebugSectionAsAnUnknownKey) {
  ASSERT_TRUE(ParseClientConfig(kClientConfig, kFileDir).has_value());

  const auto config = ParseClientConfig(kClientConfig + "debug:\n  hud: true\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "debug");
}

TEST(ShippingConfigTest, NeitherConfigKnowsThePhysicsRecordingKey) {
  const auto server = ParseServerConfig(kServerConfig + "debug:\n  physics_recording: true\n", kFileDir);
  const auto client = ParseClientConfig(kClientConfig + "debug:\n  physics_recording: true\n", kFileDir);

  ASSERT_FALSE(server.has_value());
  EXPECT_EQ(server.error().code, ConfigErrorCode::kUnknownKey);
  ASSERT_FALSE(client.has_value());
  EXPECT_EQ(client.error().code, ConfigErrorCode::kUnknownKey);
}

TEST(ShippingConfigTest, TheClientConfigKnowsNoPhysicsDebugKey) {
  for (const char* key : {"panel", "collision", "server_bodies", "hitboxes", "shot_judgements"}) {
    const auto config = ParseClientConfig(kClientConfig + "debug:\n  physics:\n    " + key + ": true\n", kFileDir);

    ASSERT_FALSE(config.has_value()) << key;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey) << key;
    EXPECT_EQ(config.error().subject, "debug") << key;
  }
}

}  // namespace
