#include "server_application.h"

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/application.h"
#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/pack_encoder.h"
#include "augusta/server_config.h"
#include "server.h"
#include "shutdown_signal.h"

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
using augusta::server::InstallShutdownSignalHandlers;
using augusta::server::ReadServerConfig;
using augusta::server::ServerProgram;
using augusta::server::ServerRuntime;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};

// The transport is initialized once for the whole process, as augustad does,
// so the tests below replace the initialize phase unless they make it fail.
class ServerApplicationEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { ASSERT_TRUE(augusta::link::Init().has_value()); }
  void TearDown() override { augusta::link::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kServerApplicationEnvironment =
    ::testing::AddGlobalTestEnvironment(new ServerApplicationEnvironment);

// A config that runs the example scenario, listening and serving metrics on
// ephemeral ports so tests run side by side.
ServerConfig ExampleConfig() {
  ServerConfig config;
  config.pack_path = (kPacks / "server").replace_extension(augusta::assets::kPackExtension);
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
  auto program = ServerProgram(config, std::nullopt, &faults);
  program.initialize = []() -> std::expected<void, Failure> { return {}; };
  return Execute(program);
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

// Requirements: NFR-18
TEST(ServerApplicationTest, AConfigFileThatCannotBeReadIsAConfigurationFailure) {
  const auto config = ReadServerConfig(CommandLine{
      .config_file = "no/such/augustad.yaml", .message = {}, .options = {}, .action = CommandLineAction::kRun});

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
  EXPECT_FALSE(config.error().detail.empty());
}

// Requirements: NFR-18
TEST(ServerApplicationTest, ACommandLineThatCannotBeReadIsAConfigurationFailure) {
  const auto config = ReadServerConfig(std::unexpected(ConfigError{.code = ConfigErrorCode::kInvalidArguments,
                                                                   .subject = "usage: augustad [--config <file>]",
                                                                   .reason = {},
                                                                   .file = {}}));

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
}

// Requirements: NFR-18
TEST(ServerApplicationTest, ATransportThatCannotBeInitializedIsATransportFailure) {
  Faults faults;
  faults.Arm(Site::kDependencyInit, "GameNetworkingSockets_Init failed");

  const Outcome outcome = Execute(ServerProgram(ExampleConfig(), std::nullopt, &faults));

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kTransportInitFailed);
  EXPECT_EQ(outcome->detail, "GameNetworkingSockets_Init failed");
}

// Requirements: NFR-18
TEST(ServerApplicationTest, APackThatDoesNotVerifyIsAContentFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.pack_path = (kPacks / "missing").replace_extension(augusta::assets::kPackExtension);

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(*outcome, "path"), config.pack_path.string());
}

// A pack that verifies but holds the client's content, not a server's.
// Requirements: NFR-18
TEST(ServerApplicationTest, APackWithoutServerContentIsAContentFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.pack_path = (kPacks / "client").replace_extension(augusta::assets::kPackExtension);

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(*outcome, "path"), config.pack_path.string());
  EXPECT_FALSE(outcome->detail.empty());
}

// "中文" in UTF-8, as a path under the folder is logged and described.
constexpr std::string_view kNonAnsiUtf8 = "\xE4\xB8\xAD\xE6\x96\x87";

// A directory named u8"中文", which no Windows ANSI code page but the CJK ones
// has, so path::string() throws for anything under it there.
class NonAnsiFolderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Named after the running test: ctest runs every test in a process of its
    // own, possibly in parallel, so no other test removes it while in use.
    root_ = std::filesystem::temp_directory_path() / (std::string("augusta_server_application_test_") +
                                                      ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root_);
    directory_ = root_ / std::filesystem::path(u8"中文");
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  std::filesystem::path root_;
  std::filesystem::path directory_;
};

// Requirements: US-28, NFR-18
TEST_F(NonAnsiFolderTest, AConfigMissingFromItIsAConfigurationFailureNamingIt) {
  const auto config = ReadServerConfig(CommandLine{
      .config_file = directory_ / "augustad.yaml", .message = {}, .options = {}, .action = CommandLineAction::kRun});

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, Code::kInvalidConfiguration);
  EXPECT_NE(config.error().detail.find(kNonAnsiUtf8), std::string::npos) << config.error().detail;
}

// A pack in such a folder verifies, which is logged with its path, and the
// content it then lacks is named by that path.
// Requirements: NFR-08, NFR-18
TEST_F(NonAnsiFolderTest, APackInItVerifiesAndIsNamedByItsContentFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  const std::filesystem::path pack = std::filesystem::path("client").replace_extension(augusta::assets::kPackExtension);
  std::filesystem::copy_file(kPacks / pack, directory_ / pack);
  config.pack_path = directory_ / pack;

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_NE(ContextOf(*outcome, "path").find(kNonAnsiUtf8), std::string::npos);
}

// The example's server pack without its rifle, signed with a key of the test's
// own at directory/server.apak, its public key at directory/test.pub: a pack a
// server would verify, and holding all it needs but the rifle.
ServerConfig WithoutTheRifle(const std::filesystem::path& directory) {
  const std::filesystem::path golden = (kPacks / "server").replace_extension(augusta::assets::kPackExtension);
  const auto layout = augusta::assets::ReadPackLayout(golden);
  EXPECT_TRUE(layout.has_value());
  std::ifstream file(golden, std::ios::binary);
  const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  std::vector<augusta::assets::AssetEntry> entries;
  for (const auto& entry : layout->entries) {
    if (entry.type == augusta::assets::AssetType::kRifle) {
      continue;
    }
    const auto* first = reinterpret_cast<const std::byte*>(bytes.data()) + entry.offset;
    entries.push_back({.type = entry.type, .path = entry.path, .data = {first, first + entry.size}});
  }
  const auto keys = augusta::assets::GenerateEd25519KeyPair();
  ServerConfig config = ExampleConfig();
  std::filesystem::create_directories(directory);
  config.pack_path = (directory / "server").replace_extension(augusta::assets::kPackExtension);
  config.public_key_path = directory / "test.pub";
  EXPECT_TRUE(augusta::assets::WritePack(config.pack_path, entries, keys.private_key, layout->client_pack).has_value());
  std::ofstream(config.public_key_path, std::ios::binary)
      .write(reinterpret_cast<const char*>(keys.public_key.data()), keys.public_key.size());
  return config;
}

// Every player's Muzzle is placed from the rifle's markers (ADR-0064), so a
// server does not start on a scenario without them.
// Requirements: NFR-18
TEST(ServerApplicationTest, APackWithoutARifleIsAContentFailure) {
  Faults faults;
  const auto directory = std::filesystem::temp_directory_path() / "augusta_server_application_test_no_rifle";
  const ServerConfig config = WithoutTheRifle(directory);

  const Outcome outcome = Served(config, faults);
  std::filesystem::remove_all(directory);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(*outcome, "asset"), "Rifle");
  EXPECT_EQ(outcome->detail, "not found");
}

// Requirements: NFR-18
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

// Requirements: NFR-18
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
// Requirements: NFR-18
TEST(ServerApplicationTest, ACaptureDirectoryThatCannotBeMadeIsAConfigurationFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.capture_directory = (kPacks / "server").replace_extension(augusta::assets::kPackExtension) / "captures";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidConfiguration);
  EXPECT_EQ(ContextOf(*outcome, "directory"), config.capture_directory.string());
}

// Requirements: NFR-18
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

// Requirements: NFR-18
TEST(ServerApplicationTest, AReplayServerWhoseCapturesAreNoDirectoryIsAConfigurationFailure) {
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.replay_captures = kPacks / "no-such-captures";

  const Outcome outcome = Served(config, faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidConfiguration);
  EXPECT_EQ(ContextOf(*outcome, "directory"), config.replay_captures.string());
}

// Requirements: NFR-18
TEST(ServerApplicationTest, AWorkerThatCannotStartReachesTheOutcomeWithItsTypedCause) {
  Faults faults;
  faults.Arm(Site::kWorkerCreation, "resource temporarily unavailable");

  const Outcome outcome = Served(ExampleConfig(), faults);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerCreationFailed);
  EXPECT_EQ(ContextOf(*outcome, "thread"), "network");
}

// Requirements: NFR-18
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
// Requirements: NFR-18
TEST(ServerApplicationTest, AStartupFailureIsLoggedOnceByTheBoundaryThatDecidesIt) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  Faults faults;
  ServerConfig config = ExampleConfig();
  config.pack_path = (kPacks / "client").replace_extension(augusta::assets::kPackExtension);

  testing::internal::CaptureStdout();
  const int status = Conclude("server", Served(config, faults));
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_NE(status, 0);
  EXPECT_EQ(Occurrences(written, "ERROR"), 0U) << written;
  EXPECT_EQ(Occurrences(written, "CRITICAL"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "subsystem=server event=terminal_failure code=invalid_content"), 1U) << written;
}

// augustad's shutdown signal (ADR-0005): its handler only sets the process's
// shutdown request, which a running ServerProgram watches, so a SIGTERM at any
// point either stops the run or does nothing. Each test raises it in a death
// test's own process, since the request, once set, lasts the process's life.

// How augustad ends with config when a SIGTERM is raised, from a thread beside
// it as a signal may arrive, once its run has started.
Outcome ServedUntilSignalled(const ServerConfig& config) {
  auto program = ServerProgram(config, std::nullopt);
  program.initialize = []() -> std::expected<void, Failure> { return {}; };
  std::atomic<bool> running{false};
  program.run = [&running, run = std::move(program.run)](ServerRuntime& runtime) {
    running = true;
    return run(runtime);
  };
  const std::jthread signaller([&running](const std::stop_token& stop) {
    while (!running && !stop.stop_requested()) {
      std::this_thread::yield();
    }
    if (running) {
      std::raise(SIGTERM);
    }
  });
  return Execute(program);
}

// The death test's exit status for how augustad ended: 0 if it stopped as asked.
[[noreturn]] void ExitWith(const Outcome& outcome) { std::exit(outcome.has_value() ? 1 : 0); }

// Each test's process re-runs the test binary rather than forking one whose
// transport is already initialized.
class ServerShutdownSignalTest : public ::testing::Test {
 protected:
  void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
};

// Requirements: NFR-18
TEST_F(ServerShutdownSignalTest, ASignalBeforeTheRunStopsItAsSoonAsItStarts) {
  EXPECT_EXIT(
      {
        InstallShutdownSignalHandlers();
        std::raise(SIGTERM);
        Faults faults;
        ExitWith(Served(ExampleConfig(), faults));
      },
      ::testing::ExitedWithCode(0), "");
}

// Requirements: NFR-18
TEST_F(ServerShutdownSignalTest, ASignalDuringTheRunStopsIt) {
  EXPECT_EXIT(
      {
        InstallShutdownSignalHandlers();
        ExitWith(ServedUntilSignalled(ExampleConfig()));
      },
      ::testing::ExitedWithCode(0), "");
}

// The runtime is released before Execute returns: a signal then only sets the
// request again, and the process goes on - with a handler that reached the
// released runtime, linux-san's ASan would end it instead.
// Requirements: NFR-18
TEST_F(ServerShutdownSignalTest, ASignalAfterTheRuntimeIsReleasedDoesNothing) {
  EXPECT_EXIT(
      {
        InstallShutdownSignalHandlers();
        const Outcome outcome = ServedUntilSignalled(ExampleConfig());
        std::raise(SIGTERM);
        std::raise(SIGINT);
        ExitWith(outcome);
      },
      ::testing::ExitedWithCode(0), "");
}

// The control for the test above: the same signals, after the same run, end
// the process once its handler is gone.
// Requirements: NFR-18
TEST_F(ServerShutdownSignalTest, ASignalAfterTheRunEndsTheProcessWithoutTheHandler) {
  EXPECT_DEATH(
      {
        InstallShutdownSignalHandlers();
        const Outcome outcome = ServedUntilSignalled(ExampleConfig());
        std::signal(SIGTERM, SIG_DFL);
        std::raise(SIGTERM);
        ExitWith(outcome);
      },
      "");
}

}  // namespace
