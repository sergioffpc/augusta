#include "augusta/client_config.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/config.h"
#include "augusta/input.h"

// Unit tests for augusta_client_config: augustac.yaml's schema, its key
// bindings, and loading it from a file (ADR-0034).
namespace {

using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::config::DescribeClientConfigError;
using augusta::config::LoadClientConfig;
using augusta::config::ParseClientConfig;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

// A relative path in a valid config resolves under this one.
const std::filesystem::path kRoot = kFileDir / "content";

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::string Absolute(std::string_view name) { return (std::filesystem::absolute(name)).generic_string(); }

TEST(ParseClientConfigTest, ReadsEveryKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: packs/level.client.pack\n  "
      "public_key: keys/signing.pub\nnetwork:\n  "
      "server_address: 10.0.0.5:27016\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.client.pack");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "signing.pub");
  EXPECT_EQ(config->server_address, "10.0.0.5:27016");
}

TEST(ParseClientConfigTest, DefaultsTheServerAddress) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->server_address, augusta::config::kDefaultServerAddress);
}

TEST(ParseClientConfigTest, DefaultsTheLogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, augusta::config::kDefaultLogLevel);
}

TEST(ParseClientConfigTest, ReadsALogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nlogging:\n  level: trace\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, "trace");
}

TEST(ParseClientConfigTest, RejectsAnInvalidLogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nlogging:\n  level: verbose\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidLogLevel);
  EXPECT_EQ(config.error().subject, "logging.level");
}

TEST(ParseClientConfigTest, ResolvesRelativePathsAgainstAnAbsoluteBaseDir) {
  const auto config =
      ParseClientConfig("base_dir: '" + Absolute("content") +
                            "'\nplayer:\n  character: soldier\ncontent:\n  pack: packs/a.pack\n  public_key: k.pub\n",
                        kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("content").lexically_normal() / "packs" / "a.pack");
  EXPECT_EQ(config->public_key_path, std::filesystem::absolute("content").lexically_normal() / "k.pub");
}

TEST(ParseClientConfigTest, ResolvesARelativeBaseDirAgainstTheFilesDirectory) {
  const auto config = ParseClientConfig(
      "base_dir: ../content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::path("file") / "content" / "a.pack");
  EXPECT_EQ(config->public_key_path, std::filesystem::path("file") / "content" / "k.pub");
}

TEST(ParseClientConfigTest, ReadsNonAsciiPathsAsUtf8) {
  // "\xC3\xA7\xC3\xA3" is c-cedilla and a-tilde in UTF-8, as YAML text carries
  // them. Escapes rather than the letters themselves: without /utf-8, MSVC
  // reads this file's literals in the system codepage.
  const auto config = ParseClientConfig(
      "base_dir: pacotes_\xC3\xA7\xC3\xA3o\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  "
      "public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / std::filesystem::path(u8"pacotes_\u00e7\u00e3o") / "a.pack");
}

TEST(ParseClientConfigTest, ADotBaseDirMeansTheFilesDirectory) {
  const auto config = ParseClientConfig(
      "base_dir: .\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / "a.pack");
}

TEST(ParseClientConfigTest, KeepsAnAbsolutePathAsIs) {
  const auto config = ParseClientConfig("base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: '" +
                                            Absolute("elsewhere/a.pack") + "'\n  public_key: k.pub\n",
                                        kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("elsewhere/a.pack").lexically_normal());
  EXPECT_EQ(config->public_key_path, kRoot / "k.pub");
}

TEST(ParseClientConfigTest, NormalizesDotSegments) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: ./a/../b.pack\n  public_key: "
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
      "base_dir: ''\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseClientConfigTest, RejectsAMissingRequiredKey) {
  const auto config =
      ParseClientConfig("base_dir: content\nplayer:\n  character: soldier\ncontent:\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsAnEmptyRequiredValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: ''\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsAnUnknownKey) {
  // A typo must not silently fall back to the default it was meant to set.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nserver_adress: x\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "server_adress");
}

TEST(ParseClientConfigTest, RejectsAKeyThatBelongsToTheServer) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nnetwork:\n  listen_address: x\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "network.listen_address");
}

TEST(ParseClientConfigTest, RejectsADuplicateKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  pack: b.pack\n  "
      "public_key: k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsANonScalarValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: [a.pack, b.pack]\n  public_key: "
      "k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

TEST(ParseClientConfigTest, RejectsAKeyWithNoValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack:\n  public_key: k.pub\n", kFileDir);

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
    "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: k.pub\n";

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
  EXPECT_TRUE(Contains(DescribeClientConfigError(config.error()), "move_forward, move_back"));
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
      "base_dir: content\nplayer:\n  character: soldier\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(DescribeClientConfigErrorTest, SaysWhatIsWrongWithABinding) {
  EXPECT_TRUE(Contains(
      DescribeClientConfigError({.code = ConfigErrorCode::kInvalidKeyName, .subject = "keys.sprint", .file = {}}),
      "keys.sprint"));
  EXPECT_TRUE(Contains(
      DescribeClientConfigError({.code = ConfigErrorCode::kKeyBoundTwice, .subject = "keys.sprint", .file = {}}),
      "another control"));
  EXPECT_TRUE(
      Contains(DescribeClientConfigError({.code = ConfigErrorCode::kReservedKey, .subject = "keys.crouch", .file = {}}),
               "Escape"));
  EXPECT_TRUE(Contains(
      DescribeClientConfigError({.code = ConfigErrorCode::kNotASection, .subject = "keys", .file = {}}), "mapping"));
}

TEST(ExampleConfigTest, TheExampleClientConfigLoadsWithTheDefaultControls) {
  const auto config = LoadClientConfig(AUGUSTA_EXAMPLE_CLIENT_CONFIG);

  ASSERT_TRUE(config.has_value()) << DescribeClientConfigError(config.error());
  EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap);
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, augusta::input::kDefaultMouseSensitivity);
}

TEST(ParseClientConfigTest, ReadsTheChosenCharacter) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: sniper\ncontent:\n  pack: a.pack\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->character, "sniper");
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

TEST(ParseClientConfigTest, TheTickRateIsNotAClientKey) {
  // The server decides it and tells each client when it joins (ADR-0039), so
  // the client has no simulation section at all.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.pack\n  public_key: "
      "k.pub\nsimulation:\n  tick_rate_hz: 60\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "simulation");
}

class LoadConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Named after the running test: ctest runs every test in a process of its
    // own, possibly in parallel, so no other test removes it while in use.
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string("augusta_client_config_test_") + test.test_suite_name() + "_" + test.name();
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

TEST_F(LoadConfigTest, ResolvesBaseDirAgainstTheFilesDirectory) {
  const auto file = Write("augustac.yaml",
                          "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: level.pack\n  "
                          "public_key: keys/k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "content" / "level.pack");
  EXPECT_EQ(config->public_key_path, directory_ / "content" / "keys" / "k.pub");
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
