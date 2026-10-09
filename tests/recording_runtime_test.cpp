#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
#include "augusta/supervisor.h"
#include "content.h"
#include "host.h"
#include "recording.h"
#include "runtime.h"

// A Match recording that cannot be written (ADR-0048), seen from the Host and
// the ServerRuntime that runs it: an optional one degrades while the server
// goes on, a strict one is the runtime's terminal failure (ADR-0033). The
// recording's own behaviour is recording_test.cpp's.
namespace {

using augusta::failure::Code;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::RecordingMode;
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

// The state whose augustad_recording_state series is 1, nullopt when none is.
std::optional<std::string> RecordingStateIn(const Host& host) {
  for (const prometheus::MetricFamily& family : host.Metrics().Collect()) {
    if (family.name != "augustad_recording_state") {
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

class RecordingFailureTest : public ::testing::Test {
 protected:
  RecordingFailureTest() {
    augusta::logging::Init();
    augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  }

  // After the Host or runtime that wrote it has closed it.
  ~RecordingFailureTest() override { std::filesystem::remove(path_); }

  RecordingFailureTest(const RecordingFailureTest&) = delete;
  RecordingFailureTest& operator=(const RecordingFailureTest&) = delete;
  RecordingFailureTest(RecordingFailureTest&&) = delete;
  RecordingFailureTest& operator=(RecordingFailureTest&&) = delete;

  // A server on the loopback recording in mode, or recording nothing, with
  // faults_ asked at the recording's write and flush.
  HostConfig Config(std::optional<RecordingMode> mode) {
    return HostConfig{
        .tick_rate_hz = kTickRate,
        .parameters = {},
        .listen = {.address = "127.0.0.1:0"},
        .recording = mode.has_value() ? path_ : std::filesystem::path{},
        .recording_mode = mode.value_or(RecordingMode::kOptional),
        .server_pack = {},
        .capture = {},
        .faults = &faults_,
    };
  }

  static Scenario EmptyScenario() {
    return Scenario{.collision = {}, .spawn_points = {}, .characters = {}, .client_pack = {}};
  }

  std::unique_ptr<Host> MakeHost(std::optional<RecordingMode> mode) {
    return std::make_unique<Host>(Config(mode), EmptyScenario());
  }

  Faults faults_;

 private:
  // Unique to this process: ctest may run the tests of this suite side by side.
  std::filesystem::path path_ = std::filesystem::temp_directory_path() /
                                ("augusta_recording_failure_" + std::to_string(std::random_device{}()) + ".rec");
};

TEST_F(RecordingFailureTest, AHostThatRecordsNothingHasNoRecordingState) {
  const auto host = MakeHost(std::nullopt);
  host->Tick(kDeltaTime);
  EXPECT_EQ(RecordingStateIn(*host), std::nullopt);
  EXPECT_FALSE(host->RecordingFailure().has_value());
}

TEST_F(RecordingFailureTest, ARecordingHostThatWritesItsTicksIsEnabled) {
  testing::internal::CaptureStdout();
  {
    const auto host = MakeHost(RecordingMode::kOptional);
    host->Tick(kDeltaTime);
    EXPECT_EQ(RecordingStateIn(*host), "enabled");
    EXPECT_FALSE(host->RecordingFailure().has_value());
  }
  const std::string log = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Occurrences(log, "event=recording_enabled"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "mode=optional"), 2U) << log;
  EXPECT_EQ(Occurrences(log, "event=recording_stopped mode=optional ticks=1 lost=false"), 1U) << log;
}

TEST_F(RecordingFailureTest, AnOptionalRecordingThatFailsDegradesWhileTheHostTicksOn) {
  faults_.Arm(Site::kRecordingWrite, "disk full", Faults::kEveryTime);
  const auto host = MakeHost(RecordingMode::kOptional);
  EXPECT_TRUE(Eventually([&] {
    host->Tick(kDeltaTime);
    return RecordingStateIn(*host) == "degraded";
  }));

  EXPECT_FALSE(host->RecordingFailure().has_value());
  const auto before = host->Tick(kDeltaTime).state.tick;
  EXPECT_EQ(host->Tick(kDeltaTime).state.tick, before + 1);
}

TEST_F(RecordingFailureTest, AStrictRecordingThatFailsIsTheHostsFailureToStopOn) {
  faults_.Arm(Site::kRecordingFlush, "disk full", Faults::kEveryTime);
  const auto host = MakeHost(RecordingMode::kStrict);
  host->Tick(kDeltaTime);
  ASSERT_TRUE(Eventually([&] { return host->RecordingFailure().has_value(); }));

  EXPECT_EQ(host->RecordingFailure()->code, Code::kStrictRecordingFailed);
  EXPECT_EQ(DispositionOf(host->RecordingFailure()->code), Disposition::kRuntime);
  EXPECT_EQ(RecordingStateIn(*host), "stopped");
}

TEST_F(RecordingFailureTest, AnOptionalRecordingWhoseFlushFailsDegradesWhileTheHostTicksOn) {
  faults_.Arm(Site::kRecordingFlush, "disk full", Faults::kEveryTime);
  const auto host = MakeHost(RecordingMode::kOptional);
  host->Tick(kDeltaTime);

  EXPECT_FALSE(host->FinishRecording().has_value());
  EXPECT_EQ(RecordingStateIn(*host), "degraded");
  const auto before = host->Tick(kDeltaTime).state.tick;
  EXPECT_EQ(host->Tick(kDeltaTime).state.tick, before + 1);
}

TEST_F(RecordingFailureTest, AStrictRecordingsLossIsKnownOnceItsTicksAreWritten) {
  // The header and the first tick are written; the second tick's write fails.
  const auto host = MakeHost(RecordingMode::kStrict);
  host->Tick(kDeltaTime);
  ASSERT_FALSE(host->FinishRecording().has_value());
  faults_.Arm(Site::kRecordingWrite, "disk full");
  host->Tick(kDeltaTime);

  // No waiting on the writer here: FinishRecording does.
  const std::optional<augusta::failure::Failure> lost = host->FinishRecording();
  ASSERT_TRUE(lost.has_value());
  EXPECT_EQ(lost->code, Code::kStrictRecordingFailed);
  EXPECT_NE(augusta::failure::DescribeFailure(*lost).find("tick=2 step=write"), std::string::npos);
}

TEST_F(RecordingFailureTest, AStrictRecordingThatFailsStopsTheRuntimeOnceWithItsFailure) {
  faults_.Arm(Site::kRecordingWrite, "disk full", Faults::kEveryTime);
  std::optional<augusta::failure::Failure> cause;
  testing::internal::CaptureStdout();
  {
    ServerRuntime runtime(Config(RecordingMode::kStrict), 0, EmptyScenario());
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
  EXPECT_EQ(cause->code, Code::kStrictRecordingFailed);
  EXPECT_EQ(DispositionOf(cause->code), Disposition::kRuntime);
  EXPECT_EQ(cause->detail, "disk full");
  EXPECT_EQ(Occurrences(log, "ERROR"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "code=strict_recording_failed disposition=runtime"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "event=recording_stopped mode=strict"), 1U) << log;
  EXPECT_EQ(Occurrences(log, std::string(augusta::supervisor::kThreadContextKey) + "=simulation"), 1U) << log;
}

TEST_F(RecordingFailureTest, AnOptionalRecordingThatFailsLeavesTheRuntimeRunningUntilItIsStopped) {
  faults_.Arm(Site::kRecordingWrite, "disk full", Faults::kEveryTime);
  std::optional<augusta::failure::Failure> cause;
  testing::internal::CaptureStdout();
  {
    ServerRuntime runtime(Config(RecordingMode::kOptional), 0, EmptyScenario());
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
  EXPECT_EQ(Occurrences(log, "event=recording_degraded code=recording_write_failed"), 1U) << log;
  EXPECT_EQ(Occurrences(log, "event=recording_stopped mode=optional ticks=0 lost=true"), 1U) << log;
}

}  // namespace
