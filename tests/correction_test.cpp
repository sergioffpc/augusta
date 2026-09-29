#include "augusta/correction.h"

#include <gtest/gtest.h>

// Pure: the smoothing is fed the totals and the frame times, no clock or ECS.
namespace {

using augusta::math::Vec3;
using augusta::presentation::Correction;
using augusta::presentation::kSnapDistance;

constexpr float kFrame = 1.0F / 60.0F;
constexpr int kBudgetFrames = 9;  // NFR-02's 150 ms at 60 Hz.

Vec3 Along(float x) { return Vec3(x, 0.0F, 0.0F); }

TEST(CorrectionTest, ThereIsNoOffsetWhileNothingHasBeenCorrected) {
  Correction smoothing;

  EXPECT_EQ(smoothing.Update(Along(0.0F), kFrame), Along(0.0F));
  EXPECT_EQ(smoothing.Update(Along(0.0F), kFrame), Along(0.0F));
}

TEST(CorrectionTest, TheFirstFrameHasNothingShownBeforeItToSlideFrom) {
  Correction smoothing;

  EXPECT_EQ(smoothing.Update(Along(0.5F), kFrame), Along(0.0F));
}

TEST(CorrectionTest, AJumpIsHiddenInFullOnTheFrameItArrives) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));

  const Vec3 offset = smoothing.Update(Along(0.5F), kFrame);

  // The body is shown where it was: 0.5 m behind where it now is.
  EXPECT_NEAR(offset.x, -0.5F, 1e-6F);
}

TEST(CorrectionTest, TheOffsetFadesToNothingWithinTheBudgetWithoutOvershoot) {
  constexpr float kJump = 0.5F;
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  float previous = smoothing.Update(Along(kJump), kFrame).x;

  for (int i = 0; i < kBudgetFrames; ++i) {
    const float offset = smoothing.Update(Along(kJump), kFrame).x;
    EXPECT_GT(offset, previous) << i;
    EXPECT_LE(offset, 0.0F) << "overshoot at " << i;
    previous = offset;
  }

  EXPECT_GT(previous, -0.05F * kJump);
}

TEST(CorrectionTest, TheSameTotalSeenAgainIsNotAJumpTwice) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  const float first = smoothing.Update(Along(0.5F), kFrame).x;

  const float again = smoothing.Update(Along(0.5F), 0.0F).x;

  EXPECT_NEAR(again, first, 1e-6F);
}

TEST(CorrectionTest, JumpsOfTicksTheFrameNeverSawAreStillHidden) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));

  // Three ticks of 0.2 m went by between two frames; only the sum is seen.
  const Vec3 offset = smoothing.Update(Along(0.6F), kFrame);

  EXPECT_NEAR(offset.x, -0.6F, 1e-6F);
}

TEST(CorrectionTest, JumpsInOppositeDirectionsCancel) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  static_cast<void>(smoothing.Update(Along(0.5F), 0.0F));

  const Vec3 offset = smoothing.Update(Along(0.0F), 0.0F);

  EXPECT_NEAR(offset.x, 0.0F, 1e-6F);
}

TEST(CorrectionTest, AJumpAtTheSnapDistanceIsShownAtOnce) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));

  EXPECT_EQ(smoothing.Update(Along(kSnapDistance), kFrame), Along(0.0F));
}

TEST(CorrectionTest, ASnapDropsWhatWasStillFading) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  ASSERT_LT(smoothing.Update(Along(0.5F), kFrame).x, 0.0F);

  EXPECT_EQ(smoothing.Update(Along(0.5F + kSnapDistance), kFrame), Along(0.0F));
}

}  // namespace
