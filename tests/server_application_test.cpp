#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "application.h"
#include "augusta/application.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/server_config.h"

// augustad's application boundary (ADR-0033), on the example scenario's
// golden server pack: whatever its startup or its runtime fails with, the
// server ends as one classified Outcome, and says so in one terminal event.
namespace {

using augusta::application::Conclude;
using augusta::application::Execute;
using augusta::application::Outcome;
using augusta::config::CommandLine;
using augusta::config::CommandLineAction;
using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::config::ServerConfig;
using augusta::failure::Code;
using augusta::failure::Failure;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::server::ReadServerConfig;
using augusta::server::ServerLifecycle;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};

// The transport is initialized once for the whole process, as augustad does,
// so the tests below replace the initialize phase unless they make it fail.
class ServerApplicationEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { ASSERT_TRUE(augusta::networking::Init().has_value()); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kServerApplicationEnvironment =
    ::testing::AddGlobalTestEnvironment(new ServerApplicationEnvironment);

// A config that runs the example scenario, listening and serving metrics on
// ephemeral ports so tests run side by side.
ServerConfig ExampleConfig() {
  ServerConfig config;
  config.pack_path = kPacks / "server.pack";
  config.public_key_path = kPacks / "test.pub";
  config.tick_rate_hz = 60;
  config.listen_address = "127.0.0.1:0";
  config.log_level = "info";
  config.metrics_port = 0;
  return config;
}

// How augustad ends with config, faults armed as the test has them, and the
// transport the environment initialized.
Outcome Served(const ServerConfig& config, Faults& faults) {
  auto lifecycle = ServerLifecycle(config, &faults);
  lifecycle.initialize = []() -> std::expected<void, Failure> { return {}; };
  return Execute(lifecycle);
}

// The value of the failure's context under key, or empty if it has none.
std::string ContextOf(const Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
      return field.value;
    }
  }
  return {};
}

// How many times needle occurs in text.
std::size_t Occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

TEST(ServerApplicationTest, AConfigFileThatCannotBeReadIsAConfigurationFailure) {
  const auto config = ReadServerConfig(CommandLine{
      .config_file = "no/such/augustad.yaml", .message = {}, .options = {}, .action = CommandLineAction::kRun});

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
  EXPECT_FALSE(config.error().detail.empty());
}

TEST(ServerApplicationTest, ACommandLineThatCannotBeReadIsAConfigurationFailure) {
  const auto config = ReadServerConfig(std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidArguments,
                                                                   .subject = "usage: augustad [--config <file>]",
                                                                   .reason = {},
                                                                   .file = {}}));

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
}

TEST(ServerApplicationTest, ATransportThatCannotBeInitializedIsATransportFailure) {
  Faults faults;
  faults.Arm(Site::kDependencyInit, "GameNetworkingSockets_Init failed");

  const Outcome outcome = Execute(ServerLifecycle(ExampleConfig(), &faults));

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kTransportInitFailed);
  EXPECT_EQ(outcome->detail, "GameNetworkingSockets_Init failed");
}

TEST(ServerApplicationTest, APackThatDoesNotVerifyIsAContentFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.pack_path = kPacks / "missing.pack";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(*outcome, "path"), config.pack_path.string());
}

// A pack that verifies but holds the client's content, not a server's.
TEST(ServerApplicationTest, APackWithoutServerContentIsAContentFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.pack_path = kPacks / "client.pack";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(*outcome, "path"), config.pack_path.string());
  EXPECT_FALSE(outcome->detail.empty());
}

TEST(ServerApplicationTest, AListenAddressThatDoesNotParseIsAConfigurationFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.listen_address = "not an address";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidConfiguration);
  EXPECT_EQ(ContextOf(*outcome, "address"), "not an address");
  EXPECT_EQ(ContextOf(*outcome, "phase"), "construct");
}

TEST(ServerApplicationTest, AListenerThatCannotBeSetUpIsAListenerFailure) {
  Faults faults;
  faults.Arm(Site::kListenerSetup, "address in use");

  const Outcome outcome = Served(ExampleConfig(), faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kListenerSetupFailed);
  EXPECT_EQ(outcome->detail, "address in use");
  EXPECT_EQ(ContextOf(*outcome, "phase"), "construct");
}

// A file where the capture directory should be: no directory can be made there.
TEST(ServerApplicationTest, ACaptureDirectoryThatCannotBeMadeIsAConfigurationFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.capture_directory = kPacks / "server.pack" / "captures";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidConfiguration);
  EXPECT_EQ(ContextOf(*outcome, "directory"), config.capture_directory.string());
}

TEST(ServerApplicationTest, AWorkerFailureReachesTheOutcomeWithItsTypedCause) {
  Faults faults;
  faults.Arm(Site::kWorkerExecution, "injected");

  const Outcome outcome = Served(ExampleConfig(), faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerFailed);
  EXPECT_FALSE(ContextOf(*outcome, "thread").empty());
  EXPECT_EQ(outcome->detail, "injected");
}

// With replay.captures set, augustad runs a replay server (ADR-0051) on the
// same pack, threads and supervisor as a live one.
// Requirements: US-21
TEST(ServerApplicationTest, AReplayServerRunsOnThePackUntilItsFirstCause) {
  Faults faults;
  faults.Arm(Site::kWorkerExecution, "injected");
  ServerConfig config = ExampleConfig();
  config.replay_captures = std::filesystem::temp_directory_path();

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerFailed);
  EXPECT_EQ(outcome->detail, "injected");
}

TEST(ServerApplicationTest, AReplayServerWhoseCapturesAreNoDirectoryIsAConfigurationFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.replay_captures = kPacks / "no-such-captures";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidConfiguration);
  EXPECT_EQ(ContextOf(*outcome, "directory"), config.replay_captures.string());
}

TEST(ServerApplicationTest, AWorkerThatCannotStartReachesTheOutcomeWithItsTypedCause) {
  Faults faults;
  faults.Arm(Site::kWorkerCreation, "resource temporarily unavailable");

  const Outcome outcome = Served(ExampleConfig(), faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerCreationFailed);
  EXPECT_EQ(ContextOf(*outcome, "thread"), "network");
}

TEST(ServerApplicationTest, ARuntimeFailureEndsInOneTerminalEventAndANonZeroExitStatus) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  Faults faults;
  faults.Arm(Site::kWorkerCreation, "resource temporarily unavailable");

  testing::internal::CaptureStdout();
  const int status = Conclude("server", Served(ExampleConfig(), faults));
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_NE(status, 0);
  // The runtime's line, where its stop was decided, and the process's terminal
  // event (ADR-0033): one each, and no other at ERROR or above.
  EXPECT_EQ(Occurrences(written, "ERROR"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "subsystem=supervisor event=worker_failed"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "CRITICAL"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "subsystem=server event=terminal_failure code=worker_creation_failed"), 1U) << written;
}

// The content loader returns what is wrong rather than logging it: the
// boundary's terminal event is the only line at ERROR or above.
TEST(ServerApplicationTest, AStartupFailureIsLoggedOnceByTheBoundaryThatDecidesIt) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.pack_path = kPacks / "client.pack";

  testing::internal::CaptureStdout();
  const int status = Conclude("server", Served(config, faults));
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_NE(status, 0);
  EXPECT_EQ(Occurrences(written, "ERROR"), 0U) << written;
  EXPECT_EQ(Occurrences(written, "CRITICAL"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "subsystem=server event=terminal_failure code=invalid_content"), 1U) << written;
}

}  // namespace
