#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>
#include <prometheus/client_metric.h>
#include <prometheus/metric_family.h>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/supervisor.h"
#include "capture.h"
#include "content.h"
#include "host.h"
#include "runtime.h"

// A Match capture that cannot be written (ADR-0050), seen from the Host and the
// ServerRuntime that runs it: an optional one degrades while the server goes
// on, a strict one is the runtime's terminal failure (ADR-0033). The capture's
// own behaviour is capture_test.cpp's. Each server's Match has no players, so
// it starts on the first tick and its capture's header is written with no
// client.
namespace {

using augusta::failure::Code;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::server::CaptureMode;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::Scenario;
using augusta::server::ServerRuntime;

constexpr std::uint8_t kTickRate = 60;
constexpr float kDeltaTime = 1.0F / kTickRate;
constexpr auto kPatience = std::chrono::seconds(10);

// Init and Shutdown once for the whole process, as in networking_test.cpp.
class NetworkingEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkingEnvironment);

std::size_t Occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// Whether done holds within kPatience.
bool Eventually(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + kPatience;
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

// The state whose augustad_capture_health series is 1, nullopt when none is.
std::optional<std::string> CaptureHealthIn(const Host& host) {
  for (const prometheus::MetricFamily& family : host.Metrics().Collect()) {
    if (family.name != "augustad_capture_health") {
      continue;
    }
    for (const prometheus::ClientMetric& series : family.metric) {
      if (series.gauge.value == 1.0) {
        return series.label.front().value;
      }
    }
  }
  return std::nullopt;
}

class CaptureFailureTest : public ::testing::Test {
 protected:
  CaptureFailureTest() {
    augusta::logging::Init();
    augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  }

  // After the Host or runtime that wrote into it has closed its files.
  ~CaptureFailureTest() override { std::filesystem::remove_all(directory_); }

  CaptureFailureTest(const CaptureFailureTest&) = delete;
  CaptureFailureTest& operator=(const CaptureFailureTest&) = delete;
  CaptureFailureTest(CaptureFailureTest&&) = delete;
  CaptureFailureTest& operator=(CaptureFailureTest&&) = delete;

  // A server on the loopback capturing in mode, or capturing nothing, with
  // faults_ asked at the capture's write and flush. Its Match has no players.
  HostConfig Config(std::optional<CaptureMode> mode) {
    augusta::parameters::Parameters parameters{};
    parameters.player_count = 0;
    return HostConfig{
        .tick_rate_hz = kTickRate,
        .parameters = parameters,
        .listen = {.address = "127.0.0.1:0"},
        .recording = {},
        .recording_mode = {},
        .server_pack = {},
        .capture_directory = mode.has_value() ? directory_ : std::filesystem::path{},
        .capture_mode = mode.value_or(CaptureMode::kOptional),
        .faults = &faults_,
    };
  }

  static Scenario EmptyScenario() {
    return Scenario{.collision = {}, .spawn_points = {}, .characters = {}, .client_pack = {}};
  }

  std::unique_ptr<Host> MakeHost(std::optional<CaptureMode> mode) {
    return std::make_unique<Host>(Config(mode), EmptyScenario());
  }

  Faults faults_;

 private:
  // Unique to this process: ctest may run the tests of this suite side by side.
  std::filesystem::path directory_ =
      std::filesystem::temp_directory_path() / ("augusta_capture_failure_" + std::to_string(std::random_device{}()));
};

TEST_F(CaptureFailureTest, AHostThatCapturesNothingHasNoCaptureHealth) {
  const auto host = MakeHost(std::nullopt);
  host->Tick(kDeltaTime);
  EXPECT_EQ(CaptureHealthIn(*host), std::nullopt);
  EXPECT_FALSE(host->FinishCapture().has_value());
}

// The captures are stopped once the run's last tick is captured, and read so
// for as long as the Host's metrics are collected.
TEST_F(CaptureFailureTest, ACapturingHostIsEnabledUntilItsCaptureIsFinishedThenStopped) {
  testing::internal::CaptureStdout();
  {
    const auto host = MakeHost(CaptureMode::kStrict);
    host->Tick(kDeltaTime);
    EXPECT_EQ(CaptureHealthIn(*host), "enabled");

    EXPECT_FALSE(host->FinishCapture().has_value());
    EXPECT_EQ(CaptureHealthIn(*host), "stopped");
    host->Tick(kDeltaTime);
    EXPECT_EQ(CaptureHealthIn(*host), "stopped");
  }
  const std::string log = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Occurrences(log, "event=capture_enabled"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "mode=strict"), 2U) << log;
  EXPECT_EQ(Occurrences(log, "event=capture_disabled mode=strict lost=false"), 1U) << log;
}

// Requirements: US-21
TEST_F(CaptureFailureTest, AnOptionalCaptureThatFailsDegradesWhileTheHostTicksOn) {
  faults_.Arm(Site::kCaptureWrite, "disk full", Faults::kEveryTime);
  const auto host = MakeHost(CaptureMode::kOptional);
  EXPECT_TRUE(Eventually([&] {
    host->Tick(kDeltaTime);
    return CaptureHealthIn(*host) == "degraded";
  }));

  EXPECT_FALSE(host->CaptureFailure().has_value());
  EXPECT_FALSE(host->FinishCapture().has_value());
  const auto before = host->Tick(kDeltaTime).state.tick;
  EXPECT_EQ(host->Tick(kDeltaTime).state.tick, before + 1);
}

TEST_F(CaptureFailureTest, AStrictCaptureThatFailsIsTheHostsFailureToStopOn) {
  faults_.Arm(Site::kCaptureFlush, "disk full", Faults::kEveryTime);
  const auto host = MakeHost(CaptureMode::kStrict);
  host->Tick(kDeltaTime);
  ASSERT_TRUE(Eventually([&] { return host->CaptureFailure().has_value(); }));

  EXPECT_EQ(host->CaptureFailure()->code, Code::kStrictCaptureFailed);
  EXPECT_EQ(DispositionOf(host->CaptureFailure()->code), Disposition::kRuntime);
  EXPECT_EQ(CaptureHealthIn(*host), "stopped");
}

TEST_F(CaptureFailureTest, AStrictCapturesLossIsKnownOnceItsRecordsAreWritten) {
  faults_.Arm(Site::kCaptureWrite, "disk full");
  const auto host = MakeHost(CaptureMode::kStrict);
  host->Tick(kDeltaTime);

  // No waiting on the writer here: FinishCapture does.
  const std::optional<augusta::failure::Failure> lost = host->FinishCapture();
  ASSERT_TRUE(lost.has_value());
  EXPECT_EQ(lost->code, Code::kStrictCaptureFailed);
  EXPECT_NE(augusta::failure::DescribeFailure(*lost).find("match=1 step=write"), std::string::npos);
}

TEST_F(CaptureFailureTest, AStrictCaptureThatFailsStopsTheRuntimeOnceWithItsFailure) {
  faults_.Arm(Site::kCaptureWrite, "disk full", Faults::kEveryTime);
  std::optional<augusta::failure::Failure> cause;
  testing::internal::CaptureStdout();
  {
    ServerRuntime runtime(Config(CaptureMode::kStrict), 0, EmptyScenario());
    // Bounds a runtime that never stops on its own, which fails the test below.
    std::atomic<bool> returned = false;
    std::thread watchdog([&] {
      Eventually([&] { return returned.load(); });
      runtime.Stop();
    });
    cause = runtime.Run();
    returned = true;
    watchdog.join();
  }
  const std::string log = testing::internal::GetCapturedStdout();

  ASSERT_TRUE(cause.has_value()) << log;
  EXPECT_EQ(cause->code, Code::kStrictCaptureFailed);
  EXPECT_EQ(DispositionOf(cause->code), Disposition::kRuntime);
  EXPECT_EQ(cause->detail, "disk full");
  EXPECT_EQ(Occurrences(log, "ERROR"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "code=strict_capture_failed disposition=runtime"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "event=capture_disabled mode=strict"), 1U) << log;
  EXPECT_EQ(Occurrences(log, std::string(augusta::supervisor::kThreadContextKey) + "=simulation"), 1U) << log;
}

// Requirements: US-21
TEST_F(CaptureFailureTest, AnOptionalCaptureThatFailsLeavesTheRuntimeRunningUntilItIsStopped) {
  faults_.Arm(Site::kCaptureWrite, "disk full", Faults::kEveryTime);
  std::optional<augusta::failure::Failure> cause;
  testing::internal::CaptureStdout();
  {
    ServerRuntime runtime(Config(CaptureMode::kOptional), 0, EmptyScenario());
    std::thread stopper([&] {
      // Long past the header's failed write, many ticks in.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      runtime.Stop();
    });
    cause = runtime.Run();
    stopper.join();
  }
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_FALSE(cause.has_value()) << log;
  EXPECT_EQ(Occurrences(log, "ERROR"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "event=capture_degraded mode=optional code=capture_write_failed"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "event=capture_disabled mode=optional lost=true"), 1U) << log;
}

// However its Simulation loop ends - here the local transport fails - the run
// stops its captures before Run returns, while its metrics endpoint still
// serves, so the last scrape reads them stopped.
TEST_F(CaptureFailureTest, ARuntimeStopsItsCapturesBeforeRunReturns) {
  faults_.Arm(Site::kTransportReceive, "poll group gone", Faults::kEveryTime);
  constexpr std::string_view kReturned = "run_returned";
  testing::internal::CaptureStdout();
  {
    ServerRuntime runtime(Config(CaptureMode::kOptional), 0, EmptyScenario());
    const std::optional<augusta::failure::Failure> cause = runtime.Run();
    std::cout << kReturned << std::endl;
    EXPECT_TRUE(cause.has_value());
  }
  const std::string log = testing::internal::GetCapturedStdout();

  const std::size_t stopped = log.find("event=capture_disabled mode=optional lost=false");
  ASSERT_NE(stopped, std::string::npos) << log;
  EXPECT_LT(stopped, log.find(kReturned)) << log;
  EXPECT_EQ(Occurrences(log, "event=capture_disabled"), 1U) << log;
}

}  // namespace
