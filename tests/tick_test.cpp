#include "augusta/tick.h"

#include <chrono>

#include <gtest/gtest.h>

// Pure: the schedule is fed the times, so a fake clock stands in for the loop's.
namespace {

using augusta::tick::Clock;
using augusta::tick::kLateTolerance;
using augusta::tick::kMaxTicksBehind;
using augusta::tick::Measure;
using augusta::tick::NextDeadline;

constexpr Clock::duration kTick = std::chrono::microseconds{16'667};  // 60 Hz.
constexpr Clock::duration kWork = std::chrono::milliseconds{2};
const Clock::time_point kStart{std::chrono::seconds{100}};

TEST(TickTest, TicksThatFinishInTimeAreDueEvenlySpaced) {
  Clock::time_point deadline = kStart;

  for (int i = 1; i <= 10; ++i) {
    deadline = NextDeadline(deadline, kTick, deadline + kWork);

    EXPECT_EQ(deadline, kStart + (i * kTick)) << i;
  }
}

TEST(TickTest, OneLateTickDoesNotDelayTheTicksAfterIt) {
  Clock::time_point deadline = kStart;
  // The first Tick overruns by half a tick, so the second starts late.
  deadline = NextDeadline(deadline, kTick, deadline + kTick + (kTick / 2));
  deadline = NextDeadline(deadline, kTick, kStart + kTick + (kTick / 2) + kWork);

  EXPECT_EQ(deadline, kStart + (2 * kTick));
  EXPECT_EQ(NextDeadline(deadline, kTick, deadline + kWork), kStart + (3 * kTick));
}

TEST(TickTest, ALoopAFewTicksBehindCatchesUp) {
  const Clock::time_point stalled_until = kStart + ((kMaxTicksBehind + 1) * kTick);

  EXPECT_EQ(NextDeadline(kStart, kTick, stalled_until), kStart + kTick);
}

TEST(TickTest, ALongStallResynchronisesToNowInsteadOfBursting) {
  const Clock::time_point stalled_until = kStart + (10 * kTick);

  const Clock::time_point deadline = NextDeadline(kStart, kTick, stalled_until);

  EXPECT_EQ(deadline, stalled_until);
  EXPECT_EQ(NextDeadline(deadline, kTick, deadline + kWork), stalled_until + kTick);
}

TEST(TickTest, TheTickDurationCanChangeBetweenTicks) {
  const Clock::duration longer = kTick + std::chrono::microseconds{300};

  EXPECT_EQ(NextDeadline(kStart, longer, kStart + kWork), kStart + longer);
}

TEST(TickTest, ATickThatStartsAndFinishesInTimeIsNeitherLateNorOverrun) {
  const auto timing = Measure(kStart, kTick, kStart + kLateTolerance, kStart + kLateTolerance + kWork);

  EXPECT_FALSE(timing.late);
  EXPECT_FALSE(timing.overrun);
}

TEST(TickTest, ATickThatStartsPastTheToleranceIsLate) {
  const Clock::time_point start = kStart + kLateTolerance + std::chrono::microseconds{1};

  EXPECT_TRUE(Measure(kStart, kTick, start, start + kWork).late);
}

TEST(TickTest, ATickWhoseWorkTakesLongerThanATickOverruns) {
  const auto timing = Measure(kStart, kTick, kStart, kStart + kTick + std::chrono::microseconds{1});

  EXPECT_FALSE(timing.late);
  EXPECT_TRUE(timing.overrun);
}

}  // namespace
