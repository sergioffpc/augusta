#include "augusta/config.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/input.h"

// Unit tests for augusta_config's YAML schema and file loading (ADR-0034).
namespace {

using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::config::DescribeConfigError;
using augusta::config::LoadClientConfig;
using augusta::config::LoadServerConfig;
using augusta::config::ParseClientConfig;
using augusta::config::ParseServerConfig;
using augusta::config::ResolveConfigFile;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

// A relative path in a valid client/server config resolves under this one.
const std::filesystem::path kRoot = kFileDir / "content";

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::string Absolute(std::string_view name) { return (std::filesystem::absolute(name)).generic_string(); }

TEST(ParseClientConfigTest, ReadsEveryKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: packs/level.client.pack\n  "
      "public_key: keys/augusta.pub\nnetwork:\n  "
      "server_address: 10.0.0.5:27016\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.client.pack");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "augusta.pub");
  EXPECT_EQ(config->server_address, "10.0.0.5:27016");
}

TEST(ParseClientConfigTest, DefaultsTheServerAddress) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->server_address, augusta::config::kDefaultServerAddress);
}

TEST(ParseClientConfigTest, DefaultsTheLogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, augusta::config::kDefaultLogLevel);
}

TEST(ParseClientConfigTest, ReadsALogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nlogging:\n  level: trace\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, "trace");
}

TEST(ParseClientConfigTest, RejectsAnInvalidLogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nlogging:\n  level: verbose\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidLogLevel);
  EXPECT_EQ(config.error().subject, "logging.level");
}

TEST(ParseClientConfigTest, ResolvesRelativePathsAgainstAnAbsoluteBaseDir) {
  const auto config = ParseClientConfig(
      "base_dir: '" + Absolute("content") +
          "'\nplayer:\n  character: characters/player\ncontent:\n  pack: packs/a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("content").lexically_normal() / "packs" / "a.pack");
  EXPECT_EQ(config->public_key_path, std::filesystem::absolute("content").lexically_normal() / "k.pub");
}

TEST(ParseClientConfigTest, ResolvesARelativeBaseDirAgainstTheFilesDirectory) {
  const auto config = ParseClientConfig(
      "base_dir: ../content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::path("file") / "content" / "a.pack");
  EXPECT_EQ(config->public_key_path, std::filesystem::path("file") / "content" / "k.pub");
}

TEST(ParseClientConfigTest, ReadsNonAsciiPathsAsUtf8) {
  // "\xC3\xA7\xC3\xA3" is c-cedilla and a-tilde in UTF-8, as YAML text carries
  // them. Escapes rather than the letters themselves: without /utf-8, MSVC
  // reads this file's literals in the system codepage.
  const auto config = ParseClientConfig(
      "base_dir: pacotes_\xC3\xA7\xC3\xA3o\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  "
      "public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / std::filesystem::path(u8"pacotes_\u00e7\u00e3o") / "a.pack");
}

TEST(ParseClientConfigTest, ADotBaseDirMeansTheFilesDirectory) {
  const auto config = ParseClientConfig(
      "base_dir: .\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / "a.pack");
}

TEST(ParseClientConfigTest, KeepsAnAbsolutePathAsIs) {
  const auto config =
      ParseClientConfig("base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: '" +
                            Absolute("elsewhere/a.pack") + "'\n  public_key: k.pub\n",
                        kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("elsewhere/a.pack").lexically_normal());
  EXPECT_EQ(config->public_key_path, kRoot / "k.pub");
}

TEST(ParseClientConfigTest, NormalizesDotSegments) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: ./a/../b.pack\n  public_key: "
      "k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "b.pack");
}

TEST(ParseClientConfigTest, RejectsAMissingBaseDir) {
  const auto config = ParseClientConfig("content:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseClientConfigTest, RejectsAnEmptyBaseDir) {
  const auto config = ParseClientConfig(
      "base_dir: ''\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseClientConfigTest, RejectsAMissingRequiredKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsAnEmptyRequiredValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: ''\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsAnUnknownKey) {
  // A typo must not silently fall back to the default it was meant to set.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nserver_adress: x\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "server_adress");
}

TEST(ParseClientConfigTest, RejectsAKeyThatBelongsToTheServer) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nnetwork:\n  listen_address: x\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "network.listen_address");
}

TEST(ParseClientConfigTest, RejectsADuplicateKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  pack: b.pack\n  "
      "public_key: k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsANonScalarValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: [a.pack, b.pack]\n  public_key: "
      "k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsAKeyWithNoValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack:\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsATopLevelThatIsNotAMapping) {
  for (const auto text : {"- a\n- b\n", "just a string\n", ""}) {
    const auto config = ParseClientConfig(text, kFileDir);

    ASSERT_FALSE(config.has_value()) << text;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kNotAMapping) << text;
  }
}

TEST(ParseClientConfigTest, RejectsInvalidYaml) {
  const auto config = ParseClientConfig("content:\n  pack: [unterminated\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidYaml);
}

// The keys every client config needs, to append a keymap test's lines to.
constexpr std::string_view kClientBase =
    "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: k.pub\n";

std::expected<augusta::config::ClientConfig, ConfigError> ParseClient(std::string_view extra) {
  return ParseClientConfig(std::string(kClientBase) + std::string(extra), kFileDir);
}

augusta::input::Key BoundTo(const augusta::config::ClientConfig& config, augusta::input::Control control) {
  return config.input.keymap.at(static_cast<std::size_t>(control));
}

TEST(ParseClientConfigTest, WithoutKeysOrSensitivityTheInputDefaultsApply) {
  const auto config = ParseClient("");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap);
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, augusta::input::kDefaultMouseSensitivity);
}

TEST(ParseClientConfigTest, ReadsTheMouseSensitivity) {
  const auto config = ParseClient("input:\n  mouse_sensitivity: 0.004\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, 0.004F);
}

TEST(ParseClientConfigTest, RejectsASensitivityThatIsNotAFiniteNumberAboveZero) {
  for (const auto value : {"0", "-1", "fast", "inf"}) {
    const auto config = ParseClient(std::string("input:\n  mouse_sensitivity: ") + value + "\n");

    ASSERT_FALSE(config.has_value()) << value;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidNumber) << value;
    EXPECT_EQ(config.error().subject, "input.mouse_sensitivity") << value;
  }
}

TEST(ParseClientConfigTest, AKeysSectionRebindsOnlyTheControlsItNames) {
  const auto config = ParseClient("input:\n  keys:\n    move_forward: Up\n    prone: MouseMiddle\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveForward), augusta::input::Key::kUp);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kProne), augusta::input::Key::kMouseMiddle);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveBack), augusta::input::Key::kS);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kSprint), augusta::input::Key::kLeftShift);
}

TEST(ParseClientConfigTest, AControlMayMoveToAKeyItsOwnDefaultFreesUp) {
  // Swapping W and S is two rebinds whose keys only clash with each other's defaults.
  const auto config = ParseClient("input:\n  keys:\n    move_forward: S\n    move_back: W\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveForward), augusta::input::Key::kS);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveBack), augusta::input::Key::kW);
}

TEST(ParseClientConfigTest, RejectsAnUnknownControl) {
  const auto config = ParseClient("input:\n  keys:\n    jump: Space\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownControl);
  EXPECT_EQ(config.error().subject, "input.keys.jump");
  EXPECT_TRUE(Contains(DescribeConfigError(config.error()), "move_forward, move_back"));
}

TEST(ParseClientConfigTest, RejectsAnUnknownKeyName) {
  const auto config = ParseClient("input:\n  keys:\n    sprint: Shift\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidKeyName);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
}

TEST(ParseClientConfigTest, RejectsAKeyBoundToTwoControls) {
  // Sprint moves onto W, which move_forward still has by default.
  const auto config = ParseClient("input:\n  keys:\n    sprint: W\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kKeyBoundTwice);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
}

TEST(ParseClientConfigTest, RejectsBindingTheKeyThatReleasesTheCursor) {
  const auto config = ParseClient("input:\n  keys:\n    crouch: Escape\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kReservedKey);
  EXPECT_EQ(config.error().subject, "input.keys.crouch");
}

TEST(ParseClientConfigTest, RejectsAControlNamedTwice) {
  const auto config = ParseClient("input:\n  keys:\n    sprint: Space\n    sprint: Tab\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
}

TEST(ParseClientConfigTest, RejectsAKeysValueThatIsNotAMapping) {
  for (const auto value : {"input:\n  keys: W\n", "input:\n  keys: [W, S]\n"}) {
    const auto config = ParseClient(value);

    ASSERT_FALSE(config.has_value()) << value;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kNotASection) << value;
    EXPECT_EQ(config.error().subject, "input.keys") << value;
  }
}

TEST(ParseClientConfigTest, FireMayMoveToAnotherMouseButton) {
  const auto config = ParseClient("input:\n  keys:\n    fire: MouseMiddle\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kFire), augusta::input::Key::kMouseMiddle);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kAds), augusta::input::Key::kMouseRight);
}

TEST(ParseClientConfigTest, FireOnTheButtonAdsStillHasIsBoundTwice) {
  const auto config = ParseClient("input:\n  keys:\n    fire: MouseRight\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kKeyBoundTwice);
  EXPECT_EQ(config.error().subject, "input.keys.fire");
}

TEST(ParseClientConfigTest, AnEmptySectionSetsNothing) {
  // A section written with every entry left out or commented out.
  for (const auto value : {"input:\n", "input:\n  keys:\n", "network:\nlogging:\n"}) {
    const auto config = ParseClient(value);

    ASSERT_TRUE(config.has_value()) << value;
    EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap) << value;
    EXPECT_EQ(config->server_address, augusta::config::kDefaultServerAddress) << value;
  }
}

TEST(ParseClientConfigTest, RejectsADottedNameInPlaceOfASection) {
  // Otherwise `content.pack: b` would stand in for (or silently shadow) `content: {pack: b}`.
  for (const auto value : {"network.server_address: x\n", "input:\n  keys.sprint: Space\n"}) {
    const auto config = ParseClient(value);

    ASSERT_FALSE(config.has_value()) << value;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey) << value;
  }
}

TEST(ParseClientConfigTest, RejectsABindingThatIsNotAString) {
  const auto config = ParseClient("input:\n  keys:\n    sprint: [Space, Tab]\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
}

TEST(ParseServerConfigTest, InputIsNotAServerSection) {
  const auto config = ParseServerConfig(
      "base_dir: content\ncontent:\n  pack: a.pack\n  public_key: k.pub\nsimulation:\n  tick_rate_hz: 60\ninput:\n  "
      "keys:\n    sprint: Space\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "input");
}

TEST(ParseClientConfigTest, RejectsASectionWrittenAsAPlainValue) {
  const auto config = ParseClient("network: 127.0.0.1:27015\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNotASection);
  EXPECT_EQ(config.error().subject, "network");
}

TEST(ParseClientConfigTest, RejectsAnUnknownKeyInsideAKnownSection) {
  const auto config = ParseClient("network:\n  server_adress: x\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "network.server_adress");
}

TEST(ParseClientConfigTest, RejectsASectionWrittenTwice) {
  const auto config = ParseClient("logging:\n  level: info\nlogging:\n  level: warn\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "logging");
}

TEST(ParseClientConfigTest, AKeyMovedOutOfItsSectionIsUnknown) {
  // The flat layout from before sections: every key now lives in one.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(DescribeConfigErrorTest, SaysWhatIsWrongWithABinding) {
  EXPECT_TRUE(
      Contains(DescribeConfigError({.code = ConfigErrorCode::kInvalidKeyName, .subject = "keys.sprint", .file = {}}),
               "keys.sprint"));
  EXPECT_TRUE(
      Contains(DescribeConfigError({.code = ConfigErrorCode::kKeyBoundTwice, .subject = "keys.sprint", .file = {}}),
               "another control"));
  EXPECT_TRUE(Contains(
      DescribeConfigError({.code = ConfigErrorCode::kReservedKey, .subject = "keys.crouch", .file = {}}), "Escape"));
  EXPECT_TRUE(
      Contains(DescribeConfigError({.code = ConfigErrorCode::kNotASection, .subject = "keys", .file = {}}), "mapping"));
}

TEST(ExampleConfigTest, TheExampleClientConfigLoadsWithTheDefaultControls) {
  const auto config = LoadClientConfig(AUGUSTA_EXAMPLE_CLIENT_CONFIG);

  ASSERT_TRUE(config.has_value()) << DescribeConfigError(config.error());
  EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap);
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, augusta::input::kDefaultMouseSensitivity);
}

TEST(ExampleConfigTest, TheExampleServerConfigLoads) {
  const auto config = LoadServerConfig(AUGUSTA_EXAMPLE_SERVER_CONFIG);

  ASSERT_TRUE(config.has_value()) << DescribeConfigError(config.error());
  EXPECT_EQ(config->tick_rate_hz, 60);
}

TEST(ParseClientConfigTest, ReadsTheChosenCharacter) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/sniper\ncontent:\n  pack: a.pack\n  public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->character, "characters/sniper");
}

TEST(ParseClientConfigTest, RejectsAMissingCharacter) {
  // No default: a player never silently plays a character they did not pick (ADR-0042).
  const auto config = ParseClientConfig("base_dir: content\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "player.character");
}

TEST(ParseClientConfigTest, RejectsAnEmptyCharacter) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: ''\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "player.character");
}

TEST(ParseServerConfigTest, ReadsEveryKey) {
  const auto config = ParseServerConfig(
      "base_dir: content\ncontent:\n  pack: packs/level.server.pack\n  public_key: keys/augusta.pub\nsimulation:\n  "
      "tick_rate_hz: 30\nnetwork:\n  listen_address: 0.0.0.0:27016\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.server.pack");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "augusta.pub");
  EXPECT_EQ(config->tick_rate_hz, 30);
  EXPECT_EQ(config->listen_address, "0.0.0.0:27016");
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

TEST(ParseServerConfigTest, RejectsAStaminaKeyLeftInTheFileAsUnknown) {
  // The stamina rules live in the Parameters script (ADR-0039), not here.
  for (const char* key : {"stamina_deplete_per_second", "stamina_regen_per_second", "stamina_forced_walk_below"}) {
    const auto config = ParseServerConfig(ServerConfigWith(std::string(key) + ": 0.1\n"), kFileDir);

    ASSERT_FALSE(config.has_value()) << key;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey) << key;
    EXPECT_EQ(config.error().subject, key);
  }
}

TEST(ParseClientConfigTest, TheTickRateIsNotAClientKey) {
  // The server decides it and tells each client when it joins (ADR-0039), so
  // the client has no simulation section at all.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nsimulation:\n  tick_rate_hz: 60\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "simulation");
}

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

std::expected<std::filesystem::path, ConfigError> Resolve(std::vector<const char*> args) {
  args.insert(args.begin(), "augustac");
  return ResolveConfigFile(static_cast<int>(args.size()), args.data(), "augustac", "augustac.yaml");
}

TEST(ResolveConfigFileTest, WithoutArgumentsUsesTheDefaultFileNextToTheExecutable) {
  const auto file = Resolve({});

  ASSERT_TRUE(file.has_value());
  EXPECT_EQ(file->filename(), "augustac.yaml");
  EXPECT_TRUE(std::filesystem::is_directory(file->parent_path())) << file->string();
}

TEST(ResolveConfigFileTest, ConfigArgumentNamesTheFileAsGiven) {
  const auto file = Resolve({"--config", "other/dir/my.yaml"});

  ASSERT_TRUE(file.has_value());
  EXPECT_EQ(*file, std::filesystem::path("other") / "dir" / "my.yaml");
}

TEST(ResolveConfigFileTest, RejectsConfigWithoutAFile) {
  const auto file = Resolve({"--config"});

  ASSERT_FALSE(file.has_value());
  EXPECT_EQ(file.error().code, ConfigErrorCode::kInvalidArguments);
  EXPECT_TRUE(Contains(file.error().subject, "usage: augustac [--config <file>]")) << file.error().subject;
}

TEST(ResolveConfigFileTest, RejectsAnEmptyFileName) { ASSERT_FALSE(Resolve({"--config", ""}).has_value()); }

TEST(ResolveConfigFileTest, RejectsAPositionalArgument) {
  // The old `augustac <pack> <key>` invocation must fail, not be half-honored.
  ASSERT_FALSE(Resolve({"level.pack", "k.pub"}).has_value());
  ASSERT_FALSE(Resolve({"my.yaml"}).has_value());
}

TEST(ResolveConfigFileTest, RejectsAnUnknownOptionAndExtraArguments) {
  ASSERT_FALSE(Resolve({"--conf", "my.yaml"}).has_value());
  ASSERT_FALSE(Resolve({"--config", "a.yaml", "b.yaml"}).has_value());
}

TEST(ResolveConfigFileTest, UsageNamesTheProgramAndTheDefaultFile) {
  const char* const args[] = {"augustad", "nope"};
  const auto file = ResolveConfigFile(2, args, "augustad", "augustad.yaml");

  ASSERT_FALSE(file.has_value());
  EXPECT_EQ(file.error().code, ConfigErrorCode::kInvalidArguments);
  EXPECT_TRUE(Contains(file.error().subject, "usage: augustad [--config <file>]")) << file.error().subject;
  EXPECT_TRUE(Contains(file.error().subject, "augustad.yaml")) << file.error().subject;
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

TEST(DescribeConfigErrorTest, SaysWhatATickRateMustBe) {
  const auto message =
      DescribeConfigError({.code = ConfigErrorCode::kInvalidNumber, .subject = "simulation.tick_rate_hz", .file = {}});

  EXPECT_EQ(message, "'simulation.tick_rate_hz' must be an integer from 1 to 255");
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

class LoadConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() / "augusta_config_test";
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

TEST_F(LoadConfigTest, ResolvesBaseDirAgainstTheFilesDirectory) {
  const auto file = Write("augustac.yaml",
                          "base_dir: content\nplayer:\n  character: characters/player\ncontent:\n  pack: level.pack\n  "
                          "public_key: keys/k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "content" / "level.pack");
  EXPECT_EQ(config->public_key_path, directory_ / "content" / "keys" / "k.pub");
}

TEST_F(LoadConfigTest, LoadsAServerConfig) {
  const auto file = Write("augustad.yaml",
                          "base_dir: .\ncontent:\n  pack: level.pack\n  public_key: k.pub\nsimulation:\n  "
                          "tick_rate_hz: 60\nnetwork:\n  listen_address: 0.0.0.0:1\n");

  const auto config = LoadServerConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "level.pack");
  EXPECT_EQ(config->listen_address, "0.0.0.0:1");
}

TEST_F(LoadConfigTest, NamesTheFileWhenItCannotBeOpened) {
  const auto file = directory_ / "missing.yaml";

  const auto config = LoadClientConfig(file);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kCannotOpenFile);
  EXPECT_EQ(config.error().file, file);
}

TEST_F(LoadConfigTest, NamesTheFileWhenItsContentsAreInvalid) {
  const auto file = Write("augustac.yaml", "base_dir: .\ncontent:\n  public_key: k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().file, file);
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

}  // namespace
