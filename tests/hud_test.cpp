#include "augusta/hud.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace augusta::hud {
namespace {

using std::chrono::milliseconds;

using Text = std::vector<std::string>;

NetStats MeasuredNet() {
  return {
      .rtt_ms = 12, .jitter_ms = 0.4F, .loss_percent = 0.0F, .in_bytes_per_sec = 1228.8F, .out_bytes_per_sec = 512.0F};
}

// Requirements: US-29
TEST(LinesTest, ReadsDashesBeforeAnyFrameAndWhileNotConnected) {
  EXPECT_EQ(Lines(0.0, std::nullopt, false), Text{"FPS: -- | RTT: --"});
}

// Requirements: US-29
TEST(LinesTest, ReadsTheFrameRateAndFrameTime) {
  EXPECT_EQ(Lines(1.0 / 144.0, std::nullopt, false), Text{"FPS: 144 (6.9ms) | RTT: --"});
}

// Requirements: US-29
TEST(LinesTest, ReadsTheConnectionsNumbers) {
  EXPECT_EQ(Lines(0.0, MeasuredNet(), false),
            Text{"FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 1.2KB/s | Out: 512B/s"});
}

// Requirements: US-29
TEST(LinesTest, EndsTheNetworkLineInImpairedWhileTheLinkIsImpaired) {
  EXPECT_EQ(Lines(0.0, MeasuredNet(), true),
            Text{"FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 1.2KB/s | Out: 512B/s | IMPAIRED"});
}

// Requirements: US-29
TEST(LinesTest, EndsTheNetworkLineInImpairedWhileTheLinkIsImpairedAndNotConnected) {
  EXPECT_EQ(Lines(0.0, std::nullopt, true), Text{"FPS: -- | RTT: -- | IMPAIRED"});
}

// Requirements: US-29
TEST(LinesTest, ReadsDashesForWhatIsNotMeasuredYet) {
  NetStats net = MeasuredNet();
  net.jitter_ms.reset();
  net.loss_percent.reset();

  EXPECT_EQ(Lines(0.0, net, false), Text{"FPS: -- | RTT: 12ms | Jitter: -- | Loss: -- | In: 1.2KB/s | Out: 512B/s"});
}

// Requirements: US-29
TEST(LinesTest, ReadsMegabytesPerSecond) {
  NetStats net = MeasuredNet();
  net.in_bytes_per_sec = 3.5F * 1024.0F * 1024.0F;

  EXPECT_EQ(Lines(0.0, net, false),
            Text{"FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 3.5MB/s | Out: 512B/s"});
}

// Requirements: US-29
TEST(FormatBytesTest, ReadsBytesKilobytesAndMegabytesInStepsOf1024) {
  EXPECT_EQ(FormatBytes(300.0), "300B");
  EXPECT_EQ(FormatBytes(2048.0), "2.0KB");
  EXPECT_EQ(FormatBytes(3.4 * 1024.0 * 1024.0), "3.4MB");
  EXPECT_EQ(FormatRate(512.0), "512B/s");
}

// Requirements: US-29
TEST(FrameTimeWindowTest, AveragesNothingBeforeTheFirstFrameEnds) {
  FrameTimeWindow window;
  window.Tick(FrameTimeWindow::Clock::time_point());

  EXPECT_EQ(window.Average(), 0.0);
}

// Requirements: US-29
TEST(FrameTimeWindowTest, AveragesTheFramesSoFar) {
  FrameTimeWindow window;
  FrameTimeWindow::Clock::time_point now;
  window.Tick(now);
  window.Tick(now += milliseconds(10));
  window.Tick(now += milliseconds(20));

  EXPECT_DOUBLE_EQ(window.Average(), 0.015);
}

// Requirements: US-29
TEST(FrameTimeWindowTest, ForgetsFramesOlderThanItsWindow) {
  FrameTimeWindow window;
  FrameTimeWindow::Clock::time_point now;
  window.Tick(now);
  window.Tick(now += milliseconds(1000));
  for (std::size_t i = 0; i < FrameTimeWindow::kFrames; ++i) {
    window.Tick(now += milliseconds(10));
  }

  EXPECT_DOUBLE_EQ(window.Average(), 0.010);
}

}  // namespace
}  // namespace augusta::hud
