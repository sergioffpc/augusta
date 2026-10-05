#include "heartbeat.h"

#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include "augusta/tick.h"

// The heartbeat is pure: time is handed to it, and no line is ever written here.
namespace {

using augusta::server::Activity;
using augusta::server::Heartbeat;
using augusta::server::kHeartbeatInterval;
using augusta::tick::Timing;
using Clock = std::chrono::steady_clock;

const Clock::time_point kStart{};
constexpr auto kInstant = std::chrono::milliseconds(1);
constexpr Timing kOnTime{.late = false, .overrun = false};

TEST(HeartbeatTest, NothingIsDueBeforeTheIntervalHasPassed) {
  Heartbeat heartbeat(kStart);

  EXPECT_FALSE(heartbeat.RecordTick(kOnTime, kStart).has_value());
  EXPECT_FALSE(heartbeat.RecordTick(kOnTime, kStart + kHeartbeatInterval - kInstant).has_value());
}

TEST(HeartbeatTest, TheIntervalIsDueOnceItHasPassedWithEveryTickAndCountInIt) {
  Heartbeat heartbeat(kStart);
  ASSERT_FALSE(heartbeat.RecordTick({.late = true, .overrun = false}, kStart).has_value());
  heartbeat.Current().messages += 2;
  ++heartbeat.Current().dropped;

  const std::optional<Activity> second =
      heartbeat.RecordTick({.late = false, .overrun = true}, kStart + kHeartbeatInterval);

  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->ticks, 2U);
  EXPECT_EQ(second->late, 1U);
  EXPECT_EQ(second->overrun, 1U);
  EXPECT_EQ(second->messages, 2U);
  EXPECT_EQ(second->dropped, 1U);
}

TEST(HeartbeatTest, TheNextIntervalStartsAfreshWhenTheLastIsDue) {
  Heartbeat heartbeat(kStart);
  ++heartbeat.Current().misbehaving;
  ASSERT_TRUE(heartbeat.RecordTick(kOnTime, kStart + kHeartbeatInterval).has_value());

  EXPECT_FALSE(heartbeat.RecordTick(kOnTime, kStart + (2 * kHeartbeatInterval) - kInstant).has_value());
  const std::optional<Activity> next = heartbeat.RecordTick(kOnTime, kStart + (2 * kHeartbeatInterval));

  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->ticks, 2U);
  EXPECT_EQ(next->misbehaving, 0U);
}

}  // namespace
