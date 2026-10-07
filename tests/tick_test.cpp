#include "augusta/tick.h"

#include <chrono>
#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

// Pure: the schedule is fed the times, so a fake clock stands in for the loop's.
namespace {

using augusta::tick::Clock;
using augusta::tick::FractionElapsed;
using augusta::tick::kLateTolerance;
using augusta::tick::kMaxPacing;
using augusta::tick::kMaxTicksBehind;
using augusta::tick::Measure;
using augusta::tick::NextDeadline;
using augusta::tick::PacedTickDuration;

constexpr Clock::duration kTick = std::chrono::microseconds{16'667};  // 60 Hz.
constexpr Clock::duration kWork = std::chrono::milliseconds{2};
const Clock::time_point kStart{std::chrono::seconds{100}};

// Requirements: NFR-01
TEST(TickTest, TicksThatFinishInTimeAreDueEvenlySpaced) {
  Clock::time_point deadline = kStart;

  for (int i = 1; i <= 10; ++i) {
    deadline = NextDeadline(deadline, kTick, deadline + kWork);

    EXPECT_EQ(deadline, kStart + (i * kTick)) << i;
  }
}

// Requirements: NFR-01
TEST(TickTest, OneLateTickDoesNotDelayTheTicksAfterIt) {
  Clock::time_point deadline = kStart;
  // The first Tick overruns by half a tick, so the second starts late.
  deadline = NextDeadline(deadline, kTick, deadline + kTick + (kTick / 2));
  deadline = NextDeadline(deadline, kTick, kStart + kTick + (kTick / 2) + kWork);

  EXPECT_EQ(deadline, kStart + (2 * kTick));
  EXPECT_EQ(NextDeadline(deadline, kTick, deadline + kWork), kStart + (3 * kTick));
}

// Requirements: NFR-01
TEST(TickTest, ALoopAFewTicksBehindCatchesUp) {
  const Clock::time_point stalled_until = kStart + ((kMaxTicksBehind + 1) * kTick);

  EXPECT_EQ(NextDeadline(kStart, kTick, stalled_until), kStart + kTick);
}

// Requirements: NFR-01
TEST(TickTest, ALongStallResynchronisesToNowInsteadOfBursting) {
  const Clock::time_point stalled_until = kStart + (10 * kTick);

  const Clock::time_point deadline = NextDeadline(kStart, kTick, stalled_until);

  EXPECT_EQ(deadline, stalled_until);
  EXPECT_EQ(NextDeadline(deadline, kTick, deadline + kWork), stalled_until + kTick);
}

// Requirements: NFR-01
TEST(TickTest, TheTickDurationCanChangeBetweenTicks) {
  const Clock::duration longer = kTick + std::chrono::microseconds{300};

  EXPECT_EQ(NextDeadline(kStart, longer, kStart + kWork), kStart + longer);
}

// Requirements: NFR-01
TEST(TickTest, ATickThatStartsAndFinishesInTimeIsNeitherLateNorOverrun) {
  const auto timing = Measure(kStart, kTick, kStart + kLateTolerance, kStart + kLateTolerance + kWork);

  EXPECT_FALSE(timing.late);
  EXPECT_FALSE(timing.overrun);
}

// Requirements: NFR-01
TEST(TickTest, ATickThatStartsPastTheToleranceIsLate) {
  const Clock::time_point start = kStart + kLateTolerance + std::chrono::microseconds{1};

  EXPECT_TRUE(Measure(kStart, kTick, start, start + kWork).late);
}

// Requirements: NFR-01
TEST(TickTest, ATickWhoseWorkTakesLongerThanATickOverruns) {
  const auto timing = Measure(kStart, kTick, kStart, kStart + kTick + std::chrono::microseconds{1});

  EXPECT_FALSE(timing.late);
  EXPECT_TRUE(timing.overrun);
}

// Requirements: NFR-01
TEST(TickTest, ATickMeasuresHowLongItsWorkTook) {
  EXPECT_EQ(Measure(kStart, kTick, kStart + kLateTolerance, kStart + kLateTolerance + kWork).duration, kWork);
}

// Requirements: NFR-01
TEST(TickTest, ATickThatEndsAFewTicksBehindIsNotResynchronised) {
  const Clock::time_point end = kStart + ((kMaxTicksBehind + 1) * kTick);

  EXPECT_FALSE(Measure(kStart, kTick, kStart, end).resynchronised);
}

// Requirements: NFR-01
TEST(TickTest, ATickThatEndsTooFarBehindIsResynchronisedAsTheNextDeadlineIs) {
  const Clock::time_point end = kStart + ((kMaxTicksBehind + 1) * kTick) + std::chrono::microseconds{1};

  EXPECT_TRUE(Measure(kStart, kTick, kStart, end).resynchronised);
  EXPECT_EQ(NextDeadline(kStart, kTick, end), end);
}

// A client whose Ticks are paced, told how many of its commands the server holds.
// Requirements: NFR-01
TEST(TickPacingTest, AClientTheServerHoldsMoreCommandsOfLengthensItsTick) {
  EXPECT_GT(PacedTickDuration(kTick, 2), kTick);
  EXPECT_GT(PacedTickDuration(kTick, 3), PacedTickDuration(kTick, 2));
}

// Requirements: NFR-01
TEST(TickPacingTest, AClientTheServerHoldsFewerCommandsOfShortensItsTick) {
  EXPECT_LT(PacedTickDuration(kTick, 1), kTick);
  EXPECT_LT(PacedTickDuration(kTick, 0), PacedTickDuration(kTick, 1));
}

// Requirements: NFR-01
TEST(TickPacingTest, OneAndTwoCommandsHeldAreEquallyFarOnEitherSideOfNominal) {
  const auto longer = PacedTickDuration(kTick, 2) - kTick;
  const auto shorter = kTick - PacedTickDuration(kTick, 1);

  EXPECT_LE(std::chrono::abs(longer - shorter), std::chrono::nanoseconds{1});
}

// Requirements: NFR-01
TEST(TickPacingTest, ATickIsNeverPacedFurtherThanItsBound) {
  const std::chrono::duration<double> nominal = kTick;
  for (int queued = 0; queued <= 255; ++queued) {
    const std::chrono::duration<double> paced = PacedTickDuration(kTick, static_cast<std::uint8_t>(queued));

    EXPECT_LE(std::abs(paced / nominal - 1.0), kMaxPacing + 1e-6) << queued;
  }
}

// Requirements: NFR-01
TEST(TickTest, TheFractionElapsedRunsFromZeroAtATicksStartToOneAtItsEnd) {
  EXPECT_FLOAT_EQ(FractionElapsed(kStart, kTick, kStart), 0.0F);
  EXPECT_NEAR(FractionElapsed(kStart, kTick, kStart + (kTick / 4)), 0.25F, 1e-4F);
  EXPECT_FLOAT_EQ(FractionElapsed(kStart, kTick, kStart + kTick), 1.0F);
}

// Requirements: NFR-01
TEST(TickTest, TheFractionElapsedIsHeldBeforeATickStartsAndAfterItEnds) {
  EXPECT_FLOAT_EQ(FractionElapsed(kStart, kTick, kStart - kWork), 0.0F);
  EXPECT_FLOAT_EQ(FractionElapsed(kStart, kTick, kStart + (3 * kTick)), 1.0F);
}

// Requirements: NFR-01
TEST(TickTest, ATickOfNoDurationIsAlreadyOver) {
  EXPECT_FLOAT_EQ(FractionElapsed(kStart, Clock::duration::zero(), kStart), 1.0F);
}

}  // namespace
