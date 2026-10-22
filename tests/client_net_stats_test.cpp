#include <optional>

#include <gtest/gtest.h>

#include "net_stats.h"

// Unit tests for the packet loss the client makes of its connection's numbers,
// in every build. Links no GPU or window.
namespace {

using augusta::client::PacketLossPercent;

constexpr float kTolerance = 1e-4F;

TEST(PacketLossPercentTest, IsFromTheWorseOfTheTwoDirections) {
  EXPECT_NEAR(*PacketLossPercent({.quality_local = 0.9F, .quality_remote = 0.75F}), 25.0F, kTolerance);
  EXPECT_NEAR(*PacketLossPercent({.quality_local = 1.0F, .quality_remote = 1.0F}), 0.0F, kTolerance);
}

TEST(PacketLossPercentTest, IgnoresADirectionNotMeasuredYet) {
  EXPECT_NEAR(*PacketLossPercent({.quality_local = 0.8F, .quality_remote = -1.0F}), 20.0F, kTolerance);
  EXPECT_FALSE(PacketLossPercent({.quality_local = -1.0F, .quality_remote = -1.0F}).has_value());
}

}  // namespace
