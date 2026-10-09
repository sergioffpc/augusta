#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "application.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/protocol.h"
#include "augusta/reenactment.h"
#include "character_loader.h"

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
using augusta::client::InitializeClientTransport;
using augusta::client::LoadClient;
using augusta::client::ReadClientConfig;
using augusta::client::ReadReenactment;
using augusta::config::ClientConfig;
using augusta::config::CommandLine;
using augusta::config::CommandLineAction;
using augusta::failure::Code;
using augusta::failure::Failure;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::harness::FailureKind;
using augusta::harness::JoinRefusal;

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

TEST(ClientApplicationTest, AConfigFileThatCannotBeReadIsAConfigurationFailure) {
  const auto config = ReadClientConfig(
      CommandLine{.config_file = "no/such/augustac.yaml", .message = {}, .action = CommandLineAction::kRun});

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
  EXPECT_FALSE(config.error().detail.empty());
}

TEST(ClientApplicationTest, ATransportThatCannotBeInitializedIsATransportFailure) {
  Faults faults;
  faults.Arm(Site::kDependencyInit, "GameNetworkingSockets_Init failed");

  const auto initialized = InitializeClientTransport(&faults);

  ASSERT_FALSE(initialized.has_value());
  EXPECT_EQ(initialized.error().code, Code::kTransportInitFailed);
  EXPECT_EQ(initialized.error().detail, "GameNetworkingSockets_Init failed");
}

TEST(ClientApplicationTest, APackThatDoesNotVerifyIsAContentFailure) {
  ClientConfig config;
  config.pack_path = kPacks / "missing.pack";
  config.public_key_path = kPacks / "test.pub";
  config.character = "soldier";

  const auto loaded = LoadClient(config);

  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(loaded.error(), "path"), config.pack_path.string());
}

TEST(ClientApplicationTest, ARefusedJoinEndsTheClientsSession) {
  const Failure failure = ClassifySessionFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kLobbyFull});

  EXPECT_EQ(failure.code, Code::kJoinRefused);
  EXPECT_FALSE(failure.detail.empty());
}

TEST(ClientApplicationTest, AServerThatNeverAnsweredIsUnreachable) {
  const Failure failure = ClassifySessionFailure({.kind = FailureKind::kServerUnreachable, .refusal = {}});

  EXPECT_EQ(failure.code, Code::kServerUnreachable);
}

TEST(ClientApplicationTest, AConnectionLostAfterAdmissionIsALostPeerConnection) {
  const Failure failure = ClassifySessionFailure({.kind = FailureKind::kConnectionLost, .refusal = {}});

  EXPECT_EQ(failure.code, Code::kPeerConnectionLost);
}

TEST(ClientApplicationTest, ACharacterThatCannotBeLoadedIsAContentFailure) {
  const Failure failure = ClassifyCharacterError(CharacterError{
      .code = CharacterErrorCode::kUnknownCharacter, .character = "ghost", .subject = {}, .resolve_error = {}});

  EXPECT_EQ(failure.code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(failure, "character"), "ghost");
}

// --- A Captured player (ADR-0050) ---

// The command line `augustac --reenact capture --player player`.
CommandLine ReenactCommandLine(const std::filesystem::path& capture, const std::string& player) {
  CommandLine command_line{.config_file = "augustac.yaml", .message = {}, .action = CommandLineAction::kRun};
  command_line.options = {{"reenact", capture.string()}, {"player", player}};
  return command_line;
}

// A capture of one player, the soldier, at (4, 0, -2), made with a client pack of 9s.
std::filesystem::path WriteCapture(const std::string& name) {
  namespace protocol = augusta::protocol;
  const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(protocol::kCaptureMagic.data()), protocol::kCaptureMagic.size());
  protocol::CaptureHeaderWire header{.engine_version = "1.0.0",
                                     .format_version = protocol::kCaptureFormatVersion,
                                     .tick_rate_hz = 60};
  header.client_pack.fill(std::byte{9});
  const protocol::CaptureRecordWire records[] = {
      header,
      protocol::CapturedJoinWire{.spawn = {4, 0, -2},
                                 .session = protocol::SessionIdWire{1},
                                 .character = "soldier",
                                 .player = 1},
  };
  for (const protocol::CaptureRecordWire& record : records) {
    const protocol::BytesWire payload = protocol::EncodeCaptureRecord(record).value();
    out.put(static_cast<char>(payload.size()));
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
  }
  return path;
}

TEST(ClientApplicationTest, ARunThatAsksToReenactNothingPlaysAsAPerson) {
  const auto reenactment =
      ReadReenactment(CommandLine{.config_file = "augustac.yaml", .message = {}, .action = CommandLineAction::kRun});

  ASSERT_TRUE(reenactment.has_value());
  EXPECT_FALSE(reenactment->has_value());
}

// Requirements: US-21
TEST(ClientApplicationTest, ARunReenactsThePlayerOfTheCaptureItNames) {
  const std::filesystem::path capture = WriteCapture("augusta_client_reenact_one.capture");

  const auto reenactment = ReadReenactment(ReenactCommandLine(capture, "1"));

  ASSERT_TRUE(reenactment.has_value());
  ASSERT_TRUE(reenactment->has_value());
  EXPECT_EQ((*reenactment)->character, "soldier");
  EXPECT_EQ((*reenactment)->spawn, augusta::math::Vec3(4, 0, -2));
  std::filesystem::remove(capture);
}

// Requirements: US-21
TEST(ClientApplicationTest, ACaptureThatDoesNotLoadOrLacksThePlayerIsAConfigurationFailure) {
  const std::filesystem::path capture = WriteCapture("augusta_client_reenact_two.capture");
  const std::filesystem::path missing = std::filesystem::temp_directory_path() / "augusta_no_such.capture";

  for (const CommandLine& command_line : {ReenactCommandLine(capture, "2"), ReenactCommandLine(missing, "1")}) {
    const auto reenactment = ReadReenactment(command_line);

    ASSERT_FALSE(reenactment.has_value());
    EXPECT_EQ(reenactment.error().code, Code::kInvalidConfiguration);
    EXPECT_EQ(ContextOf(reenactment.error(), "capture"), command_line.options.at("reenact"));
  }
  std::filesystem::remove(capture);
}

TEST(ClientApplicationTest, ReenactWithoutPlayerIsAConfigurationFailure) {
  CommandLine command_line = ReenactCommandLine("a.capture", "1");
  command_line.options.erase("player");

  const auto reenactment = ReadReenactment(command_line);

  ASSERT_FALSE(reenactment.has_value());
  EXPECT_EQ(reenactment.error().code, Code::kInvalidConfiguration);
}

// Requirements: US-21
TEST(ClientApplicationTest, ACaptureOfAnotherClientPackIsAConfigurationFailure) {
  augusta::harness::Script script;
  script.client_pack.fill(std::byte{9});
  augusta::assets::PackHash loaded{};

  EXPECT_EQ(CheckReenactmentPack(script, loaded).error().code, Code::kInvalidConfiguration);
  loaded.fill(std::byte{9});
  EXPECT_TRUE(CheckReenactmentPack(script, loaded).has_value());
}

}  // namespace
