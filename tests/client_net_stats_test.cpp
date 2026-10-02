#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include "augusta/networking.h"
#include "net_stats.h"

// Unit tests for what the debug HUD shows of the connection. Links no Falcor or
// window: they only use renderer.h's plain types.
namespace {

using augusta::client::HudNetStats;
using augusta::client::PacketLossPercent;
using augusta::networking::ConnectionStats;
using std::chrono::milliseconds;

constexpr float kTolerance = 1e-4F;

TEST(PacketLossPercentTest, IsFromTheWorseOfTheTwoDirections) {
  EXPECT_NEAR(*PacketLossPercent({.quality_local = 0.9F, .quality_remote = 0.75F}), 25.0F, kTolerance);
  EXPECT_NEAR(*PacketLossPercent({.quality_local = 1.0F, .quality_remote = 1.0F}), 0.0F, kTolerance);
}

TEST(PacketLossPercentTest, IgnoresADirectionNotMeasuredYet) {
  EXPECT_NEAR(*PacketLossPercent({.quality_local = 0.8F, .quality_remote = -1.0F}), 20.0F, kTolerance);
  EXPECT_FALSE(PacketLossPercent({.quality_local = -1.0F, .quality_remote = -1.0F}).has_value());
}

TEST(HudNetStatsTest, ShowsNothingWithoutAConnection) {
  HudNetStats hud;

  EXPECT_FALSE(hud.Update(std::nullopt, std::chrono::steady_clock::time_point{}).has_value());
}

TEST(HudNetStatsTest, ShowsTheConnectionsNumbers) {
  HudNetStats hud;

  const auto shown = hud.Update(
      ConnectionStats{.ping_ms = 42, .quality_local = 1.0F, .quality_remote = 0.5F, .in_bytes_per_sec = 100.0F},
      std::chrono::steady_clock::time_point{});

  ASSERT_TRUE(shown.has_value());
  EXPECT_EQ(shown->rtt_ms, 42);
  EXPECT_NEAR(*shown->loss_percent, 50.0F, kTolerance);
  EXPECT_FLOAT_EQ(shown->in_bytes_per_sec, 100.0F);
}

TEST(HudNetStatsTest, ShowsThePeakJitterOfTheLastFullWindow) {
  HudNetStats hud;
  const std::chrono::steady_clock::time_point start{};

  const auto first = hud.Update(ConnectionStats{.max_jitter_us = 3000}, start);
  static_cast<void>(hud.Update(ConnectionStats{.max_jitter_us = 7000}, start + milliseconds(500)));
  const auto after_window = hud.Update(ConnectionStats{.max_jitter_us = 1000}, start + HudNetStats::kJitterWindow);

  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(first->jitter_ms.has_value());
  ASSERT_TRUE(after_window.has_value());
  ASSERT_TRUE(after_window->jitter_ms.has_value());
  EXPECT_NEAR(*after_window->jitter_ms, 7.0F, kTolerance);
}

TEST(HudNetStatsTest, ForgetsTheJitterWhenTheConnectionGoes) {
  HudNetStats hud;
  const std::chrono::steady_clock::time_point start{};
  static_cast<void>(hud.Update(ConnectionStats{.max_jitter_us = 3000}, start));
  static_cast<void>(hud.Update(ConnectionStats{.max_jitter_us = 3000}, start + HudNetStats::kJitterWindow));

  static_cast<void>(hud.Update(std::nullopt, start + (2 * HudNetStats::kJitterWindow)));
  const auto shown = hud.Update(ConnectionStats{.max_jitter_us = 3000}, start + (3 * HudNetStats::kJitterWindow));

  ASSERT_TRUE(shown.has_value());
  EXPECT_FALSE(shown->jitter_ms.has_value());
}

}  // namespace
