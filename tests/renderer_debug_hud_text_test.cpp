#include <chrono>
#include <cstddef>
#include <optional>

#include <gtest/gtest.h>

#include "augusta/renderer.h"
#include "debug_hud_text.h"

namespace augusta::renderer {
namespace {

using std::chrono::milliseconds;

DebugHudNetStats MeasuredNet() {
  return {
      .rtt_ms = 12, .jitter_ms = 0.4F, .loss_percent = 0.0F, .in_bytes_per_sec = 1228.8F, .out_bytes_per_sec = 512.0F};
}

TEST(DebugHudTextTest, ReadsDashesBeforeAnyFrameAndWhileNotConnected) {
  EXPECT_EQ(DebugHudText(0.0, std::nullopt), "FPS: -- | RTT: --");
}

TEST(DebugHudTextTest, ReadsTheFrameRateAndFrameTime) {
  EXPECT_EQ(DebugHudText(1.0 / 144.0, std::nullopt), "FPS: 144 (6.9ms) | RTT: --");
}

TEST(DebugHudTextTest, ReadsTheConnectionsNumbers) {
  EXPECT_EQ(DebugHudText(0.0, MeasuredNet()),
            "FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 1.2KB/s | Out: 512B/s");
}

TEST(DebugHudTextTest, ReadsDashesForWhatIsNotMeasuredYet) {
  DebugHudNetStats net = MeasuredNet();
  net.jitter_ms.reset();
  net.loss_percent.reset();

  EXPECT_EQ(DebugHudText(0.0, net), "FPS: -- | RTT: 12ms | Jitter: -- | Loss: -- | In: 1.2KB/s | Out: 512B/s");
}

TEST(DebugHudTextTest, ReadsMegabytesPerSecond) {
  DebugHudNetStats net = MeasuredNet();
  net.in_bytes_per_sec = 3.5F * 1024.0F * 1024.0F;

  EXPECT_EQ(DebugHudText(0.0, net), "FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 3.5MB/s | Out: 512B/s");
}

TEST(FrameTimeWindowTest, AveragesNothingBeforeTheFirstFrameEnds) {
  FrameTimeWindow window;
  window.Tick(FrameTimeWindow::Clock::time_point());

  EXPECT_EQ(window.Average(), 0.0);
}

TEST(FrameTimeWindowTest, AveragesTheFramesSoFar) {
  FrameTimeWindow window;
  FrameTimeWindow::Clock::time_point now;
  window.Tick(now);
  window.Tick(now += milliseconds(10));
  window.Tick(now += milliseconds(20));

  EXPECT_DOUBLE_EQ(window.Average(), 0.015);
}

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
}  // namespace augusta::renderer
