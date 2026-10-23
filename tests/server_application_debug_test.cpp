#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/application.h"
#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/server_application.h"
#include "augusta/server_config.h"

// augustad's debug tools as its Runtime builds them (ADR-0074), on the example
// scenario's golden server pack: the pcapng `debug.network.pcapng` asks for
// (ADR-0076). Built only into a Development build's server application tests,
// whose server_application_test.cpp initializes the transport.
namespace {

using augusta::application::Execute;
using augusta::application::Outcome;
using augusta::config::ServerConfig;
using augusta::failure::Code;
using augusta::failure::Failure;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::server::ServerLifecycle;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};

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

// How augustad ends with config once its runtime is built: a worker fails as
// it runs, so the run ends at once.
Outcome ServedOnce(const ServerConfig& config) {
  Faults faults;
  faults.Arm(Site::kWorkerExecution, "injected");
  auto lifecycle = ServerLifecycle(config, &faults);
  lifecycle.initialize = []() -> std::expected<void, Failure> { return {}; };
  return Execute(lifecycle);
}

// A directory of the test's own under the temp folder, empty.
std::filesystem::path FreshDirectory() {
  const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
  const std::filesystem::path directory =
      std::filesystem::path(testing::TempDir()) / (std::string("augusta_server_application_debug_test_") + test.name());
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

std::vector<std::string> FileNamesIn(const std::filesystem::path& directory) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    names.push_back(entry.path().filename().string());
  }
  return names;
}

bool IsAugustadPcapng(std::string_view name) { return name.starts_with("augustad-") && name.ends_with(".pcapng"); }

TEST(ServerApplicationDebugTest, AServerWritesOnePcapngPerRunIntoThePcapngDirectory) {
  ServerConfig config = ExampleConfig();
  config.network_pcapng = FreshDirectory();

  const Outcome outcome = ServedOnce(config);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerFailed);

  const std::vector<std::string> names = FileNamesIn(config.network_pcapng);
  ASSERT_EQ(names.size(), 1U);
  EXPECT_TRUE(IsAugustadPcapng(names.front())) << names.front();
}

// A replay server's endpoint is tapped as a live server's listening socket is (ADR-0051).
TEST(ServerApplicationDebugTest, AReplayServerWritesOnePcapngPerRunIntoThePcapngDirectory) {
  ServerConfig config = ExampleConfig();
  config.replay_captures = std::filesystem::temp_directory_path();
  config.network_pcapng = FreshDirectory();

  const Outcome outcome = ServedOnce(config);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerFailed);

  const std::vector<std::string> names = FileNamesIn(config.network_pcapng);
  ASSERT_EQ(names.size(), 1U);
  EXPECT_TRUE(IsAugustadPcapng(names.front())) << names.front();
}

}  // namespace
