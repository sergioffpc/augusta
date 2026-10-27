#include "augusta/client_config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/config.h"
#include "augusta/input.h"
#include "augusta/window_events.h"

// Unit tests for augusta_client_config: augustac.yaml's schema, its key
// bindings, and loading it from a file (ADR-0034).
namespace {

using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::config::DescribeConfigError;
using augusta::config::LoadClientConfig;
using augusta::config::ParseClientConfig;
using augusta::config::ReadClientRun;
using augusta::config::ReadReenactArguments;
using augusta::config::ReenactArguments;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

// A relative path in a valid config resolves under this one.
const std::filesystem::path kRoot = kFileDir / "content";

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::string Absolute(std::string_view name) { return (std::filesystem::absolute(name)).generic_string(); }

augusta::config::CommandLine WithOptions(std::map<std::string, std::string, std::less<>> options) {
  return augusta::config::CommandLine{.config_file = "augustac.yaml",
                                      .message = {},
                                      .options = std::move(options),
                                      .action = augusta::config::CommandLineAction::kRun};
}

// Requirements: US-21
TEST(ReadClientRunTest, WithoutAReplayOptionTheClientPlays) {
  const auto run = ReadClientRun(WithOptions({}));

  ASSERT_TRUE(run.has_value());
  EXPECT_EQ(run->mode, augusta::config::ClientMode::kPlay);
}

// Requirements: US-21
TEST(ReadClientRunTest, ReplaysListsTheServersCapturesAndReplayWatchesOne) {
  const auto listing = ReadClientRun(WithOptions({{"replays", ""}}));
  const auto watching = ReadClientRun(WithOptions({{"replay", "20261009T101500123Z-0001.acap"}}));

  ASSERT_TRUE(listing.has_value());
  EXPECT_EQ(listing->mode, augusta::config::ClientMode::kListReplays);
  ASSERT_TRUE(watching.has_value());
  EXPECT_EQ(watching->mode, augusta::config::ClientMode::kWatchReplay);
  EXPECT_EQ(watching->capture, "20261009T101500123Z-0001.acap");
}

// Requirements: US-21, US-28
TEST(ReadClientRunTest, ListingAndWatchingAtOnceOrWatchingNothingIsRejected) {
  const auto both = ReadClientRun(WithOptions({{"replays", ""}, {"replay", "a.acap"}}));
  const auto nothing = ReadClientRun(WithOptions({{"replay", ""}}));

  ASSERT_FALSE(both.has_value());
  EXPECT_EQ(both.error().code, ConfigErrorCode::kInvalidArguments);
  ASSERT_FALSE(nothing.has_value());
  EXPECT_EQ(nothing.error().code, ConfigErrorCode::kInvalidArguments);
}

// Requirements: US-21
TEST(ReadClientRunTest, AReenactmentAloneIsPlayed) {
  const auto run = ReadClientRun(WithOptions({{"reenact", "a.acap"}, {"player", "1"}}));

  ASSERT_TRUE(run.has_value());
  EXPECT_EQ(run->mode, augusta::config::ClientMode::kPlay);
}

// Requirements: US-21
TEST(ReadClientRunTest, TheClientsOptionsAreItsReplayOnes) {
  const std::array<const char*, 3> args = {"augustac", "--replay", "a.acap"};
  const auto command_line =
      augusta::config::ParseCommandLine(static_cast<int>(args.size()), args.data(), "augustac", "augustac.yaml",
                                        "1.2.3", augusta::config::kClientOptions);

  ASSERT_TRUE(command_line.has_value());
  EXPECT_EQ(ReadClientRun(*command_line)->capture, "a.acap");
}

// Requirements: US-28
TEST(ParseClientConfigTest, ReadsEveryKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: packs/level.client.apak\n  "
      "public_key: keys/signing.pub\nnetwork:\n  "
      "server_address: 10.0.0.5:27016\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.client.apak");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "signing.pub");
  EXPECT_EQ(config->server_address, "10.0.0.5:27016");
}

// Requirements: US-28
TEST(ParseClientConfigTest, DefaultsTheServerAddress) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->server_address, augusta::config::kDefaultServerAddress);
}

TEST(ParseClientConfigTest, DefaultsTheLogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, augusta::config::kDefaultLogLevel);
}

TEST(ParseClientConfigTest, ReadsALogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\nlogging:\n  level: trace\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log_level, "trace");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAnInvalidLogLevel) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\nlogging:\n  level: verbose\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidLogLevel);
  EXPECT_EQ(config.error().subject, "logging.level");
}

// Requirements: US-28
TEST(ParseClientConfigTest, ResolvesRelativePathsAgainstAnAbsoluteBaseDir) {
  const auto config =
      ParseClientConfig("base_dir: '" + Absolute("content") +
                            "'\nplayer:\n  character: soldier\ncontent:\n  pack: packs/a.apak\n  public_key: k.pub\n",
                        kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("content").lexically_normal() / "packs" / "a.apak");
  EXPECT_EQ(config->public_key_path, std::filesystem::absolute("content").lexically_normal() / "k.pub");
}

// Requirements: US-28
TEST(ParseClientConfigTest, ResolvesARelativeBaseDirAgainstTheFilesDirectory) {
  const auto config = ParseClientConfig(
      "base_dir: ../content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::path("file") / "content" / "a.apak");
  EXPECT_EQ(config->public_key_path, std::filesystem::path("file") / "content" / "k.pub");
}

// Requirements: US-28
TEST(ParseClientConfigTest, ReadsNonAsciiPathsAsUtf8) {
  // "\xC3\xA7\xC3\xA3" is c-cedilla and a-tilde in UTF-8, as YAML text carries
  // them. Escapes rather than the letters themselves: without /utf-8, MSVC
  // reads this file's literals in the system codepage.
  const auto config = ParseClientConfig(
      "base_dir: pacotes_\xC3\xA7\xC3\xA3o\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  "
      "public_key: k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / std::filesystem::path(u8"pacotes_\u00e7\u00e3o") / "a.apak");
}

// Requirements: US-28
TEST(ParseClientConfigTest, ADotBaseDirMeansTheFilesDirectory) {
  const auto config = ParseClientConfig(
      "base_dir: .\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / "a.apak");
}

// Requirements: US-28
TEST(ParseClientConfigTest, KeepsAnAbsolutePathAsIs) {
  const auto config = ParseClientConfig("base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: '" +
                                            Absolute("elsewhere/a.apak") + "'\n  public_key: k.pub\n",
                                        kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("elsewhere/a.apak").lexically_normal());
  EXPECT_EQ(config->public_key_path, kRoot / "k.pub");
}

// Requirements: US-28
TEST(ParseClientConfigTest, NormalizesDotSegments) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: ./a/../b.apak\n  public_key: "
      "k.pub\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "b.apak");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAMissingBaseDir) {
  const auto config = ParseClientConfig("content:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "base_dir");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAnEmptyBaseDir) {
  const auto config = ParseClientConfig(
      "base_dir: ''\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "base_dir");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAMissingRequiredKey) {
  const auto config =
      ParseClientConfig("base_dir: content\nplayer:\n  character: soldier\ncontent:\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAnEmptyRequiredValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: ''\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAnUnknownKey) {
  // A typo must not silently fall back to the default it was meant to set.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\nserver_adress: x\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "server_adress");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAKeyThatBelongsToTheServer) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
      "k.pub\nnetwork:\n  listen_address: x\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "network.listen_address");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsADuplicateKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  pack: b.apak\n  "
      "public_key: k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsANonScalarValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: [a.apak, b.apak]\n  public_key: "
      "k.pub\n",
      kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAKeyWithNoValue) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack:\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "content.pack");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsATopLevelThatIsNotAMapping) {
  for (const auto* const text : {"- a\n- b\n", "just a string\n", ""}) {
    const auto config = ParseClientConfig(text, kFileDir);

    ASSERT_FALSE(config.has_value()) << text;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kNotAMapping) << text;
  }
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsInvalidYaml) {
  const auto config = ParseClientConfig("content:\n  pack: [unterminated\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidYaml);
}

// The keys every client config needs, to append a keymap test's lines to.
constexpr std::string_view kClientBase =
    "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: k.pub\n";

std::expected<augusta::config::ClientConfig, ConfigError> ParseClient(std::string_view extra) {
  return ParseClientConfig(std::string(kClientBase) + std::string(extra), kFileDir);
}

augusta::window::Key BoundTo(const augusta::config::ClientConfig& config, augusta::input::Control control) {
  return config.input.keymap.at(static_cast<std::size_t>(control));
}

// Requirements: US-20
TEST(ParseClientConfigTest, WithoutKeysOrSensitivityTheInputDefaultsApply) {
  const auto config = ParseClient("");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap);
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, augusta::input::kDefaultMouseSensitivity);
}

// Requirements: US-28
TEST(ParseClientConfigTest, ReadsTheMouseSensitivity) {
  const auto config = ParseClient("input:\n  mouse_sensitivity: 0.004\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, 0.004F);
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsASensitivityThatIsNotAFiniteNumberAboveZero) {
  for (const auto* const value : {"0", "-1", "fast", "inf"}) {
    const auto config = ParseClient(std::string("input:\n  mouse_sensitivity: ") + value + "\n");

    ASSERT_FALSE(config.has_value()) << value;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidNumber) << value;
    EXPECT_EQ(config.error().subject, "input.mouse_sensitivity") << value;
  }
}

// Requirements: US-20
TEST(ParseClientConfigTest, AKeysSectionRebindsOnlyTheControlsItNames) {
  const auto config = ParseClient("input:\n  keys:\n    move_forward: Up\n    prone: MouseMiddle\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveForward), augusta::window::Key::kUp);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kProne), augusta::window::Key::kMouseMiddle);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveBack), augusta::window::Key::kS);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kSprint), augusta::window::Key::kLeftShift);
}

// Requirements: US-20
TEST(ParseClientConfigTest, AControlMayMoveToAKeyItsOwnDefaultFreesUp) {
  // Swapping W and S is two rebinds whose keys only clash with each other's defaults.
  const auto config = ParseClient("input:\n  keys:\n    move_forward: S\n    move_back: W\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveForward), augusta::window::Key::kS);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kMoveBack), augusta::window::Key::kW);
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsAnUnknownControl) {
  const auto config = ParseClient("input:\n  keys:\n    jump: Space\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "input.keys.jump");
  EXPECT_TRUE(Contains(DescribeConfigError(config.error()), "'input.keys.jump' names no control"));
  EXPECT_TRUE(Contains(config.error().reason, "move_forward, move_back")) << config.error().reason;
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsAnUnknownKeyName) {
  const auto config = ParseClient("input:\n  keys:\n    sprint: Shift\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
  EXPECT_TRUE(Contains(config.error().reason, "must name a key")) << config.error().reason;
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsAKeyBoundToTwoControls) {
  // Sprint moves onto W, which move_forward still has by default.
  const auto config = ParseClient("input:\n  keys:\n    sprint: W\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
  EXPECT_TRUE(Contains(config.error().reason, "another control")) << config.error().reason;
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsBindingTheKeyThatReleasesTheCursor) {
  const auto config = ParseClient("input:\n  keys:\n    crouch: Escape\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "input.keys.crouch");
  EXPECT_TRUE(Contains(config.error().reason, "Escape")) << config.error().reason;
}

// Reserved in a Shipping build too, so one config loads in both kinds of build
// (ADR-0074).
// Requirements: US-20
TEST(ParseClientConfigTest, RejectsBindingTheKeyReservedForTheDebugTools) {
  const auto config = ParseClient("input:\n  keys:\n    crouch: F1\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "input.keys.crouch");
  EXPECT_TRUE(Contains(config.error().reason, "F1")) << config.error().reason;
  EXPECT_TRUE(Contains(config.error().reason, "reserved")) << config.error().reason;
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsAControlNamedTwice) {
  const auto config = ParseClient("input:\n  keys:\n    sprint: Space\n    sprint: Tab\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsAKeysValueThatIsNotAMapping) {
  for (const auto* const value : {"input:\n  keys: W\n", "input:\n  keys: [W, S]\n"}) {
    const auto config = ParseClient(value);

    ASSERT_FALSE(config.has_value()) << value;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kNotASection) << value;
    EXPECT_EQ(config.error().subject, "input.keys") << value;
  }
}

// Requirements: US-20
TEST(ParseClientConfigTest, FireMayMoveToAnotherMouseButton) {
  const auto config = ParseClient("input:\n  keys:\n    fire: MouseMiddle\n");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kFire), augusta::window::Key::kMouseMiddle);
  EXPECT_EQ(BoundTo(*config, augusta::input::Control::kAds), augusta::window::Key::kMouseRight);
}

// Requirements: US-20
TEST(ParseClientConfigTest, FireOnTheButtonAdsStillHasIsBoundTwice) {
  const auto config = ParseClient("input:\n  keys:\n    fire: MouseRight\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidEntry);
  EXPECT_EQ(config.error().subject, "input.keys.fire");
  EXPECT_TRUE(Contains(config.error().reason, "another control")) << config.error().reason;
}

// Requirements: US-28
TEST(ParseClientConfigTest, AnEmptySectionSetsNothing) {
  // A section written with every entry left out or commented out.
  for (const auto* const value : {"input:\n", "input:\n  keys:\n", "network:\nlogging:\n"}) {
    const auto config = ParseClient(value);

    ASSERT_TRUE(config.has_value()) << value;
    EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap) << value;
    EXPECT_EQ(config->server_address, augusta::config::kDefaultServerAddress) << value;
  }
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsADottedNameInPlaceOfASection) {
  // Otherwise `content.pack: b` would stand in for (or silently shadow) `content: {pack: b}`.
  for (const auto* const value : {"network.server_address: x\n", "input:\n  keys.sprint: Space\n"}) {
    const auto config = ParseClient(value);

    ASSERT_FALSE(config.has_value()) << value;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey) << value;
  }
}

// Requirements: US-20
TEST(ParseClientConfigTest, RejectsABindingThatIsNotAString) {
  const auto config = ParseClient("input:\n  keys:\n    sprint: [Space, Tab]\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "input.keys.sprint");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsASectionWrittenAsAPlainValue) {
  const auto config = ParseClient("network: 127.0.0.1:27015\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNotASection);
  EXPECT_EQ(config.error().subject, "network");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsAnUnknownKeyInsideAKnownSection) {
  const auto config = ParseClient("network:\n  server_adress: x\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "network.server_adress");
}

// Requirements: US-28
TEST(ParseClientConfigTest, RejectsASectionWrittenTwice) {
  const auto config = ParseClient("logging:\n  level: info\nlogging:\n  level: warn\n");

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "logging");
}

// Requirements: US-28
TEST(ParseClientConfigTest, AKeyMovedOutOfItsSectionIsUnknown) {
  // The flat layout from before sections: every key now lives in one.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\npack: a.apak\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "pack");
}

// Requirements: US-20
TEST(ExampleConfigTest, TheExampleClientConfigLoadsWithTheDefaultControls) {
  const auto config = LoadClientConfig(AUGUSTA_EXAMPLE_CLIENT_CONFIG);

  ASSERT_TRUE(config.has_value()) << DescribeConfigError(config.error());
  EXPECT_EQ(config->input.keymap, augusta::input::kDefaultKeymap);
  EXPECT_FLOAT_EQ(config->input.mouse_sensitivity, augusta::input::kDefaultMouseSensitivity);
}

// Requirements: US-16
TEST(ParseClientConfigTest, ReadsTheChosenCharacter) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: sniper\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->character, "sniper");
}

// Requirements: US-16
TEST(ParseClientConfigTest, RejectsAMissingCharacter) {
  // No default: a player never silently plays a character they did not pick (ADR-0042).
  const auto config = ParseClientConfig("base_dir: content\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "player.character");
}

// Requirements: US-16
TEST(ParseClientConfigTest, RejectsAnEmptyCharacter) {
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: ''\ncontent:\n  pack: a.apak\n  public_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "player.character");
}

// Requirements: US-28
TEST(ParseClientConfigTest, TheTickRateIsNotAClientKey) {
  // The server decides it and tells each client when it joins (ADR-0039), so
  // the client has no simulation section at all.
  const auto config = ParseClientConfig(
      "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: a.apak\n  public_key: "
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

  [[nodiscard]] std::filesystem::path Write(std::string_view name, std::string_view contents) const {
    const auto path = directory_ / name;
    std::ofstream(path, std::ios::binary) << contents;
    return path;
  }

  std::filesystem::path directory_;
};

// Requirements: US-28
TEST_F(LoadConfigTest, ResolvesBaseDirAgainstTheFilesDirectory) {
  const auto file = Write("augustac.yaml",
                          "base_dir: content\nplayer:\n  character: soldier\ncontent:\n  pack: level.apak\n  "
                          "public_key: keys/k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "content" / "level.apak");
  EXPECT_EQ(config->public_key_path, directory_ / "content" / "keys" / "k.pub");
}

// Requirements: US-28
TEST_F(LoadConfigTest, NamesTheFileWhenItCannotBeOpened) {
  const auto file = directory_ / "missing.yaml";

  const auto config = LoadClientConfig(file);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kCannotOpenFile);
  EXPECT_EQ(config.error().file, file);
}

// Requirements: US-28
TEST_F(LoadConfigTest, NamesTheFileWhenItsContentsAreInvalid) {
  const auto file = Write("augustac.yaml", "base_dir: .\ncontent:\n  public_key: k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().file, file);
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "content.pack");
}

// augustac's command line as main reads it, with its own options (ADR-0050).
std::expected<augusta::config::CommandLine, ConfigError> ParseClientCommandLine(std::vector<const char*> args) {
  args.insert(args.begin(), "augustac");
  return augusta::config::ParseCommandLine(static_cast<int>(args.size()), args.data(), "augustac", "augustac.yaml",
                                           "1.2.3", augusta::config::kClientOptions);
}

// What augustac's command line asks to reenact, read.
std::expected<std::optional<ReenactArguments>, ConfigError> Reenact(std::vector<const char*> args) {
  const auto command_line = ParseClientCommandLine(std::move(args));
  EXPECT_TRUE(command_line.has_value());
  return ReadReenactArguments(command_line.value_or(augusta::config::CommandLine{}));
}

// Requirements: US-21
TEST(ReadReenactArgumentsTest, WithoutReenactTheClientPlaysAsAPerson) {
  const auto reenact = Reenact({"--config", "my.yaml"});

  ASSERT_TRUE(reenact.has_value());
  EXPECT_FALSE(reenact->has_value());
}

// Requirements: US-21
TEST(ReadReenactArgumentsTest, ReenactAndPlayerNameTheCaptureAsGivenAndThePlayersNumber) {
  const auto reenact = Reenact({"--reenact", "captures/playtest.acap", "--player", "3"});

  ASSERT_TRUE(reenact.has_value());
  ASSERT_TRUE(reenact->has_value());
  EXPECT_EQ((*reenact)->capture, std::filesystem::path("captures") / "playtest.acap");
  EXPECT_EQ((*reenact)->player, 3);
}

// Requirements: US-21
TEST(ReadReenactArgumentsTest, OneWithoutTheOtherIsRefused) {
  for (const auto& args :
       {std::vector<const char*>{"--reenact", "a.acap"}, std::vector<const char*>{"--player", "1"}}) {
    const auto reenact = Reenact(args);

    ASSERT_FALSE(reenact.has_value());
    EXPECT_EQ(reenact.error().code, ConfigErrorCode::kInvalidArguments);
    EXPECT_TRUE(Contains(reenact.error().subject, "--reenact <capture> --player <n>")) << reenact.error().subject;
  }
}

// Requirements: US-21
TEST(ReadReenactArgumentsTest, APlayerThatIsNotANumberFromOneTo255IsRefused) {
  for (const char* player : {"0", "256", "-1", "one", "1.5", ""}) {
    const auto reenact = Reenact({"--reenact", "a.acap", "--player", player});

    ASSERT_FALSE(reenact.has_value()) << player;
    EXPECT_EQ(reenact.error().code, ConfigErrorCode::kInvalidArguments) << player;
  }
}

// Requirements: US-21
TEST(ReadReenactArgumentsTest, AReenactmentIsNeitherAReplayNorTheReplayList) {
  for (const auto& args : {std::vector<const char*>{"--reenact", "a.acap", "--player", "1", "--replays"},
                           std::vector<const char*>{"--reenact", "a.acap", "--player", "1", "--replay", "b"}}) {
    const auto reenact = Reenact(args);

    ASSERT_FALSE(reenact.has_value());
    EXPECT_EQ(reenact.error().code, ConfigErrorCode::kInvalidArguments);
  }
}

// Requirements: US-21, US-28
TEST(ReadReenactArgumentsTest, AnEmptyCaptureIsRefused) {
  const auto reenact = Reenact({"--reenact", "", "--player", "1"});

  ASSERT_FALSE(reenact.has_value());
  EXPECT_EQ(reenact.error().code, ConfigErrorCode::kInvalidArguments);
}

// Requirements: US-21
TEST(ReadReenactArgumentsTest, TheUsageSaysHowToReenact) {
  const auto command_line = ParseClientCommandLine({"--help"});

  ASSERT_TRUE(command_line.has_value());
  EXPECT_TRUE(Contains(command_line->message, "[--reenact <capture>] [--player <n>]")) << command_line->message;
}

}  // namespace
