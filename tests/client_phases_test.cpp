#include "client_phases.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <boost/uuid/uuid.hpp>
#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/capture.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/reenactment.h"
#include "client_character_loader.h"

// augustac's application boundary (ADR-0033): whatever its startup or its
// runtime fails with, the client ends on one classified failure. Its runtime
// needs a window and a GPU, so what is tested here is each phase's
// classification; application_test.cpp tests the boundary that runs them.
namespace {

using augusta::client::CharacterError;
using augusta::client::CharacterErrorCode;
using augusta::client::CheckReenactmentPack;
using augusta::client::ClassifyCharacterError;
using augusta::client::ClassifySessionFailure;
using augusta::client::LoadClient;
using augusta::client::ReadClientConfig;
using augusta::client::ReadReenactment;
using augusta::config::ClientConfig;
using augusta::config::CommandLine;
using augusta::config::CommandLineAction;
using augusta::failure::Code;
using augusta::failure::Failure;
using augusta::harness::FailureKind;
using augusta::harness::JoinRefusal;

// A Session ID, a UUIDv7, told apart by its last byte.
constexpr boost::uuids::uuid Session(std::uint8_t last) {
  return boost::uuids::uuid(
      {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, last});
}

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};

// The value of the failure's context under key, or empty if it has none.
std::string ContextOf(const Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
      return field.value;
    }
  }
  return {};
}

// Requirements: US-28
TEST(ClientPhasesTest, AConfigFileThatCannotBeReadIsAConfigurationFailure) {
  const auto config = ReadClientConfig(CommandLine{
      .config_file = "no/such/augustac.yaml", .message = {}, .options = {}, .action = CommandLineAction::kRun});

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
  EXPECT_FALSE(config.error().detail.empty());
}

// Requirements: NFR-08
TEST(ClientPhasesTest, APackThatDoesNotVerifyIsAContentFailure) {
  ClientConfig config;
  config.pack_path = (kPacks / "missing").replace_extension(augusta::assets::kPackExtension);
  config.public_key_path = kPacks / "test.pub";
  config.character = "soldier";

  const auto loaded = LoadClient(config);

  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(loaded.error(), "path"), config.pack_path.string());
}

TEST(ClientPhasesTest, AnUnknownCharacterInAVerifiedPackIsAContentFailure) {
  ClientConfig config;
  config.pack_path = (kPacks / "client").replace_extension(augusta::assets::kPackExtension);
  config.public_key_path = kPacks / "test.pub";
  config.character = "ghost";

  const auto loaded = LoadClient(config);

  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(loaded.error(), "path"), config.pack_path.string());
  EXPECT_EQ(ContextOf(loaded.error(), "character"), "ghost");
  EXPECT_FALSE(loaded.error().detail.empty());
}

// A pack that verifies but holds the server's content, not a client's.
TEST(ClientPhasesTest, APackWithoutClientContentIsAContentFailure) {
  ClientConfig config;
  config.pack_path = (kPacks / "server").replace_extension(augusta::assets::kPackExtension);
  config.public_key_path = kPacks / "test.pub";
  config.character = "soldier";

  const auto loaded = LoadClient(config);

  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(loaded.error(), "path"), config.pack_path.string());
  EXPECT_FALSE(loaded.error().detail.empty());
}

TEST(ClientPhasesTest, ARefusedJoinEndsTheClientsSession) {
  const Failure failure = ClassifySessionFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kLobbyFull});

  EXPECT_EQ(failure.code, Code::kJoinRefused);
  EXPECT_FALSE(failure.detail.empty());
}

TEST(ClientPhasesTest, AServerThatNeverAnsweredIsUnreachable) {
  const Failure failure = ClassifySessionFailure({.kind = FailureKind::kServerUnreachable, .refusal = {}});

  EXPECT_EQ(failure.code, Code::kServerUnreachable);
}

TEST(ClientPhasesTest, AConnectionLostAfterAdmissionIsALostPeerConnection) {
  const Failure failure = ClassifySessionFailure({.kind = FailureKind::kConnectionLost, .refusal = {}});

  EXPECT_EQ(failure.code, Code::kPeerConnectionLost);
}

TEST(ClientPhasesTest, ACharacterThatCannotBeLoadedIsAContentFailure) {
  const Failure failure = ClassifyCharacterError(CharacterError{
      .code = CharacterErrorCode::kUnknownCharacter, .character = "ghost", .subject = {}, .resolve_error = {}});

  EXPECT_EQ(failure.code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(failure, "character"), "ghost");
}

// --- A Captured player (ADR-0050) ---

// The command line `augustac --reenact capture --player player`.
CommandLine ReenactCommandLine(const std::filesystem::path& capture, const std::string& player) {
  return CommandLine{.config_file = "augustac.yaml",
                     .message = {},
                     .options = {{"reenact", capture.string()}, {"player", player}},
                     .action = CommandLineAction::kRun};
}

// A capture of one player, the soldier, at (4, 0, -2), made with a client pack of 9s.
std::filesystem::path WriteCapture(const std::string& stem) {
  const std::filesystem::path path =
      (std::filesystem::temp_directory_path() / stem).replace_extension(augusta::capture::kExtension);
  augusta::capture::Capture capture{.header = {.engine_version = "1.0.0", .tick_rate_hz = 60, .started = {}},
                                    .joins = {{.session = Session(1), .character = "soldier", .spawn = {4, 0, -2}}},
                                    .events = {},
                                    .torn = false};
  capture.header.client_pack.fill(std::byte{9});
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  EXPECT_TRUE(augusta::capture::WriteCapture(out, capture).has_value());
  return path;
}

// Requirements: US-21
TEST(ClientPhasesTest, ARunThatAsksToReenactNothingPlaysAsAPerson) {
  const auto reenactment = ReadReenactment(
      CommandLine{.config_file = "augustac.yaml", .message = {}, .options = {}, .action = CommandLineAction::kRun});

  ASSERT_TRUE(reenactment.has_value());
  EXPECT_FALSE(reenactment->has_value());
}

// Requirements: US-21
TEST(ClientPhasesTest, ARunReenactsThePlayerOfTheCaptureItNames) {
  const std::filesystem::path capture = WriteCapture("augusta_client_reenact_one");

  const auto reenactment = ReadReenactment(ReenactCommandLine(capture, "1"));

  ASSERT_TRUE(reenactment.has_value());
  ASSERT_TRUE(reenactment->has_value());
  EXPECT_EQ((*reenactment)->character, "soldier");
  EXPECT_EQ((*reenactment)->spawn, augusta::math::Vec3(4, 0, -2));
  std::filesystem::remove(capture);
}

// Requirements: US-21
TEST(ClientPhasesTest, ACaptureThatDoesNotLoadOrLacksThePlayerIsAConfigurationFailure) {
  const std::filesystem::path capture = WriteCapture("augusta_client_reenact_two");
  const std::filesystem::path missing =
      (std::filesystem::temp_directory_path() / "augusta_no_such").replace_extension(augusta::capture::kExtension);

  for (const CommandLine& command_line : {ReenactCommandLine(capture, "2"), ReenactCommandLine(missing, "1")}) {
    const auto reenactment = ReadReenactment(command_line);

    ASSERT_FALSE(reenactment.has_value());
    EXPECT_EQ(reenactment.error().code, Code::kInvalidConfiguration);
    EXPECT_EQ(ContextOf(reenactment.error(), "capture"), command_line.options.at("reenact"));
  }
  std::filesystem::remove(capture);
}

// Requirements: US-21, US-28
TEST(ClientPhasesTest, ReenactWithoutPlayerIsAConfigurationFailure) {
  CommandLine command_line =
      ReenactCommandLine(std::filesystem::path("a").replace_extension(augusta::capture::kExtension), "1");
  command_line.options.erase("player");

  const auto reenactment = ReadReenactment(command_line);

  ASSERT_FALSE(reenactment.has_value());
  EXPECT_EQ(reenactment.error().code, Code::kInvalidConfiguration);
}

// Requirements: US-21
TEST(ClientPhasesTest, ACaptureOfAnotherClientPackIsAConfigurationFailure) {
  augusta::harness::Script script;
  script.client_pack.fill(std::byte{9});
  augusta::assets::PackHash loaded{};

  EXPECT_EQ(CheckReenactmentPack(script, loaded).error().code, Code::kInvalidConfiguration);
  loaded.fill(std::byte{9});
  EXPECT_TRUE(CheckReenactmentPack(script, loaded).has_value());
}

}  // namespace
