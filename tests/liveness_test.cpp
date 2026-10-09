#include "liveness.h"

#include <chrono>

#include <gtest/gtest.h>

#include "augusta/tick.h"

// The decision /livez answers with, without HTTP: time is handed to it.
namespace {

using augusta::server::IsLive;
using augusta::server::kLivenessWindow;
using augusta::tick::Clock;

const Clock::time_point kLastTick{std::chrono::hours{1}};
constexpr auto kInstant = std::chrono::milliseconds(1);

// Requirements: NFR-07
TEST(LivenessTest, LiveRightAfterATick) { EXPECT_TRUE(IsLive(kLastTick, kLastTick)); }

// Requirements: NFR-07
TEST(LivenessTest, LiveUpToTheWindowAfterTheLastTick) {
  EXPECT_TRUE(IsLive(kLastTick, kLastTick + kLivenessWindow - kInstant));
  EXPECT_TRUE(IsLive(kLastTick, kLastTick + kLivenessWindow));
}

// Requirements: NFR-07
TEST(LivenessTest, NotLiveOnceTheWindowHasPassedWithoutATick) {
  EXPECT_FALSE(IsLive(kLastTick, kLastTick + kLivenessWindow + kInstant));
  EXPECT_FALSE(IsLive(kLastTick, kLastTick + std::chrono::hours{1}));
}

// Requirements: NFR-07
TEST(LivenessTest, LiveWhenATickEndedAfterNowWasRead) {
  // The Simulation thread can finish a tick between the endpoint reading the
  // clock and reading the last tick's time.
  EXPECT_TRUE(IsLive(kLastTick + kInstant, kLastTick));
}

// Requirements: NFR-07
TEST(LivenessTest, TheWindowIsFiveSeconds) { EXPECT_EQ(kLivenessWindow, std::chrono::seconds{5}); }

}  // namespace
