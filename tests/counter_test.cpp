#include "augusta/counter.h"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

using augusta::counter::IsNewer;

constexpr std::uint32_t kMax32 = std::numeric_limits<std::uint32_t>::max();

TEST(CounterTest, AGreaterNumberIsNewer) {
  EXPECT_TRUE(IsNewer<std::uint32_t>(2, 1));
  EXPECT_FALSE(IsNewer<std::uint32_t>(1, 2));
}

TEST(CounterTest, ANumberIsNotNewerThanItself) { EXPECT_FALSE(IsNewer<std::uint32_t>(7, 7)); }

TEST(CounterTest, TheFirstNumberIsNewerThanNone) { EXPECT_TRUE(IsNewer<std::uint32_t>(1, 0)); }

TEST(CounterTest, ANumberPastTheWrapIsNewerThanOneBeforeIt) {
  EXPECT_TRUE(IsNewer<std::uint32_t>(0, kMax32));
  EXPECT_TRUE(IsNewer<std::uint32_t>(5, kMax32 - 5));
  EXPECT_FALSE(IsNewer<std::uint32_t>(kMax32 - 5, 5));
}

TEST(CounterTest, ANumberHalfTheRangeAwayIsNewerInNeitherDirection) {
  constexpr std::uint32_t kHalf = std::uint32_t{1} << 31U;
  EXPECT_FALSE(IsNewer<std::uint32_t>(kHalf, 0));
  EXPECT_FALSE(IsNewer<std::uint32_t>(0, kHalf));
}

TEST(CounterTest, ANumberJustUnderHalfTheRangeAheadIsNewer) {
  constexpr std::uint32_t kJustUnderHalf = (std::uint32_t{1} << 31U) - 1;
  EXPECT_TRUE(IsNewer<std::uint32_t>(kJustUnderHalf, 0));
  EXPECT_FALSE(IsNewer<std::uint32_t>(0, kJustUnderHalf));
}

TEST(CounterTest, NarrowCountersWrapAtTheirOwnWidth) {
  EXPECT_TRUE(IsNewer<std::uint8_t>(0, 255));
  EXPECT_TRUE(IsNewer<std::uint16_t>(3, 65'530));
  EXPECT_FALSE(IsNewer<std::uint8_t>(200, 10));
}

TEST(CounterTest, TicksOrderTheSameWay) {
  EXPECT_TRUE(IsNewer<std::uint64_t>(2, 1));
  EXPECT_TRUE(IsNewer<std::uint64_t>(0, std::numeric_limits<std::uint64_t>::max()));
}

}  // namespace
