#include "frame_regions.h"

#include <cstdint>
#include <set>

#include <gtest/gtest.h>

namespace {

using augusta::renderer::FrameRegion;

constexpr std::uint32_t kFramesInFlight = 3;

TEST(FrameRegionTest, ConsecutiveFramesTakeConsecutiveRegions) {
  EXPECT_EQ(FrameRegion(0, kFramesInFlight), 0U);
  EXPECT_EQ(FrameRegion(1, kFramesInFlight), 1U);
  EXPECT_EQ(FrameRegion(2, kFramesInFlight), 2U);
}

TEST(FrameRegionTest, WrapsOnceEveryFrameInFlightHasHadOne) {
  EXPECT_EQ(FrameRegion(3, kFramesInFlight), 0U);
  EXPECT_EQ(FrameRegion(4, kFramesInFlight), 1U);
}

// What the regions are for: a frame never writes the region a frame still in
// flight before it is drawing.
TEST(FrameRegionTest, FramesInFlightTogetherNeverShareARegion) {
  for (std::uint64_t first = 0; first < 2 * kFramesInFlight; ++first) {
    std::set<std::uint32_t> regions;
    for (std::uint64_t frame = first; frame < first + kFramesInFlight; ++frame) {
      regions.insert(FrameRegion(frame, kFramesInFlight));
    }
    EXPECT_EQ(regions.size(), kFramesInFlight) << "frames " << first << " to " << first + kFramesInFlight - 1;
  }
}

TEST(FrameRegionTest, StaysInRangePastThirtyTwoBitsOfFrames) {
  constexpr std::uint64_t kLateFrame = (std::uint64_t{1} << 32U) + 1;
  EXPECT_EQ(FrameRegion(kLateFrame, kFramesInFlight), kLateFrame % kFramesInFlight);
}

TEST(FrameRegionTest, OneFrameInFlightAlwaysUsesTheOnlyRegion) {
  EXPECT_EQ(FrameRegion(0, 1), 0U);
  EXPECT_EQ(FrameRegion(5, 1), 0U);
}

}  // namespace
