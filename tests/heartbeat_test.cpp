#include "heartbeat.h"

#include <chrono>
#include <optional>

#include <gtest/gtest.h>

// The heartbeat is pure: time and the running totals are handed to it, and no
// line is ever written here.
namespace {

using augusta::server::Activity;
using augusta::server::Heartbeat;
using augusta::server::kHeartbeatInterval;
using Clock = std::chrono::steady_clock;

const Clock::time_point kStart{};
constexpr auto kInstant = std::chrono::milliseconds(1);

// Requirements: NFR-07
TEST(HeartbeatTest, NothingIsDueBeforeTheIntervalHasPassed) {
  Heartbeat heartbeat(kStart);

  EXPECT_FALSE(heartbeat.Record(Activity{.ticks = 1}, kStart).has_value());
  EXPECT_FALSE(heartbeat.Record(Activity{.ticks = 2}, kStart + kHeartbeatInterval - kInstant).has_value());
}

// Requirements: NFR-07
TEST(HeartbeatTest, TheIntervalIsDueOnceItHasPassedWithEverythingCountedSinceTheStart) {
  Heartbeat heartbeat(kStart);

  const std::optional<Activity> second = heartbeat.Record(Activity{.ticks = 2,
                                                                   .late = 1,
                                                                   .overrun = 1,
                                                                   .messages = 2,
                                                                   .stale = 3,
                                                                   .dropped = 1,
                                                                   .overflow = 4,
                                                                   .misbehaving = 1},
                                                          kStart + kHeartbeatInterval);

  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->ticks, 2U);
  EXPECT_EQ(second->late, 1U);
  EXPECT_EQ(second->overrun, 1U);
  EXPECT_EQ(second->messages, 2U);
  EXPECT_EQ(second->stale, 3U);
  EXPECT_EQ(second->dropped, 1U);
  EXPECT_EQ(second->overflow, 4U);
  EXPECT_EQ(second->misbehaving, 1U);
}

// Requirements: NFR-07
TEST(HeartbeatTest, TheNextIntervalCountsOnlyWhatHappenedSinceTheLastWasDue) {
  Heartbeat heartbeat(kStart);
  ASSERT_TRUE(heartbeat.Record(Activity{.ticks = 60, .misbehaving = 1}, kStart + kHeartbeatInterval).has_value());

  EXPECT_FALSE(heartbeat.Record(Activity{.ticks = 100, .misbehaving = 1}, kStart + (2 * kHeartbeatInterval) - kInstant)
                   .has_value());
  const std::optional<Activity> next =
      heartbeat.Record(Activity{.ticks = 120, .misbehaving = 1}, kStart + (2 * kHeartbeatInterval));

  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->ticks, 60U);
  EXPECT_EQ(next->misbehaving, 0U);
}

}  // namespace
