#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "application.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/harness.h"
#include "character_loader.h"

// augustac's application boundary (ADR-0033): whatever its startup or its
// runtime fails with, the client ends on one classified failure. Its runtime
// needs a window and a GPU, so what is tested here is each phase's
// classification; application_test.cpp tests the boundary that runs them.
namespace {

using augusta::client::CharacterError;
using augusta::client::CharacterErrorCode;
using augusta::client::ClassifyCharacterError;
using augusta::client::ClassifySessionFailure;
using augusta::client::InitializeClientTransport;
using augusta::client::LoadClient;
using augusta::client::ReadClientConfig;
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
  const auto config = ReadClientConfig(CommandLine{
      .config_file = "no/such/augustac.yaml", .message = {}, .options = {}, .action = CommandLineAction::kRun});

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

}  // namespace
