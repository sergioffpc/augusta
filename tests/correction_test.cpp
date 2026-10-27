#include "correction.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/prediction.h"

// Pure: the smoothing is fed the totals and the frame times, no clock or ECS.
namespace {

using augusta::math::Vec3;
using augusta::prediction::State;
using augusta::presentation::BlendTicks;
using augusta::presentation::Correction;
using augusta::presentation::kSnapDistance;

constexpr float kFrame = 1.0F / 60.0F;
constexpr int kBudgetFrames = 9;  // NFR-02's 150 ms at 60 Hz.

Vec3 Along(float x) { return Vec3(x, 0.0F, 0.0F); }

// Requirements: NFR-02
TEST(CorrectionTest, ThereIsNoOffsetWhileNothingHasBeenCorrected) {
  Correction smoothing;

  EXPECT_EQ(smoothing.Update(Along(0.0F), kFrame), Along(0.0F));
  EXPECT_EQ(smoothing.Update(Along(0.0F), kFrame), Along(0.0F));
}

// Requirements: NFR-02
TEST(CorrectionTest, TheFirstFrameHasNothingShownBeforeItToSlideFrom) {
  Correction smoothing;

  EXPECT_EQ(smoothing.Update(Along(0.5F), kFrame), Along(0.0F));
}

// Requirements: NFR-02
TEST(CorrectionTest, AJumpIsHiddenInFullOnTheFrameItArrives) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));

  const Vec3 offset = smoothing.Update(Along(0.5F), kFrame);

  // The body is shown where it was: 0.5 m behind where it now is.
  EXPECT_NEAR(offset.x, -0.5F, 1e-6F);
}

// Requirements: NFR-02
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

// Requirements: NFR-02
TEST(CorrectionTest, TheSameTotalSeenAgainIsNotAJumpTwice) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  const float first = smoothing.Update(Along(0.5F), kFrame).x;

  const float again = smoothing.Update(Along(0.5F), 0.0F).x;

  EXPECT_NEAR(again, first, 1e-6F);
}

// Requirements: NFR-02
TEST(CorrectionTest, JumpsOfTicksTheFrameNeverSawAreStillHidden) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));

  // Three ticks of 0.2 m went by between two frames; only the sum is seen.
  const Vec3 offset = smoothing.Update(Along(0.6F), kFrame);

  EXPECT_NEAR(offset.x, -0.6F, 1e-6F);
}

// Requirements: NFR-02
TEST(CorrectionTest, JumpsInOppositeDirectionsCancel) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  static_cast<void>(smoothing.Update(Along(0.5F), 0.0F));

  const Vec3 offset = smoothing.Update(Along(0.0F), 0.0F);

  EXPECT_NEAR(offset.x, 0.0F, 1e-6F);
}

// Requirements: NFR-02
TEST(CorrectionTest, AJumpAtTheSnapDistanceIsShownAtOnce) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));

  EXPECT_EQ(smoothing.Update(Along(kSnapDistance), kFrame), Along(0.0F));
}

// Requirements: NFR-02
TEST(CorrectionTest, ASnapDropsWhatWasStillFading) {
  Correction smoothing;
  static_cast<void>(smoothing.Update(Along(0.0F), kFrame));
  ASSERT_LT(smoothing.Update(Along(0.5F), kFrame).x, 0.0F);

  EXPECT_EQ(smoothing.Update(Along(0.5F + kSnapDistance), kFrame), Along(0.0F));
}

// A body standing still at the origin until reconciliation moves it jump meters
// along x on tick kJumpTick, ticking at 60 Hz, shown at frames_per_second the
// way PresentationWorld shows it: the two newest ticks blended at the fraction
// of the tick elapsed, plus Correction's offset. Render time starts kPhase of
// a tick in, so frames do not land on tick boundaries.
class BlendedJump {
 public:
  static constexpr float kTick = 1.0F / 60.0F;
  static constexpr int kJumpTick = 10;
  static constexpr float kPhase = 0.3F;

  BlendedJump(float jump, float frames_per_second) : jump_(jump), frame_(1.0F / frames_per_second) {}

  // The shown x of every frame from the first that shows kJumpTick as the
  // latest tick, up to seconds after that tick.
  std::vector<float> ShownFromTheJump(float seconds) { return Shown(seconds, false); }

  // The same, with Correction fed the blended total alone, which sees a jump
  // only in the shares the blend shows of it frame by frame.
  std::vector<float> ShownFedTheBlendedTotal(float seconds) { return Shown(seconds, true); }

 private:
  std::vector<float> Shown(float seconds, bool blended_total_alone) {
    Correction correction;
    std::vector<float> shown;
    for (int frame = 0;; ++frame) {
      const float time = (kPhase * kTick) + (static_cast<float>(frame) * frame_);
      const int latest = static_cast<int>(std::floor(time / kTick));
      const float fraction = (time / kTick) - static_cast<float>(latest);
      const State previous = AtTick(latest - 1);
      const State current = AtTick(latest);
      const State blended = BlendTicks(previous, current, fraction);
      const Vec3 offset = blended_total_alone ? correction.Update(blended.total_correction, frame_)
                                              : correction.Update(previous.total_correction, current.total_correction,
                                                                  fraction, frame_);
      if (latest >= kJumpTick) {
        shown.push_back(blended.local_body.position.x + offset.x);
      }
      if (time >= (static_cast<float>(kJumpTick) * kTick) + seconds) {
        return shown;
      }
    }
  }

  [[nodiscard]] State AtTick(int tick) const {
    State state;
    if (tick >= kJumpTick) {
      state.local_body.position = Along(jump_);
      state.total_correction = Along(jump_);
    }
    return state;
  }

  float jump_;
  float frame_;
};

// Requirements: NFR-02
TEST(CorrectionTest, AJumpOfTheSnapDistanceOrMoreIsShownInFullOnTheFirstFrameOfItsTick) {
  for (const float frames_per_second : {60.0F, 144.0F}) {
    for (const float jump : {2.5F, 4.0F}) {
      BlendedJump body(jump, frames_per_second);

      const std::vector<float> shown = body.ShownFromTheJump(BlendedJump::kTick);

      ASSERT_FALSE(shown.empty());
      for (std::size_t i = 0; i < shown.size(); ++i) {
        EXPECT_NEAR(shown[i], jump, 1e-5F) << frames_per_second << " fps, " << jump << " m, frame " << i;
      }
    }
  }

  // Control: fed the blended total alone, the same jump slides in at 144 fps.
  BlendedJump body(2.5F, 144.0F);
  EXPECT_LT(body.ShownFedTheBlendedTotal(BlendedJump::kTick).front(), 2.5F - 0.1F);
}

// Requirements: NFR-02
TEST(CorrectionTest, AJumpUnderTheSnapDistanceFadesAsWhenFedTheBlendedTotal) {
  constexpr float kJump = 1.0F;
  constexpr float kBudget = 0.15F;  // NFR-02.
  for (const float frames_per_second : {60.0F, 144.0F}) {
    BlendedJump body(kJump, frames_per_second);

    const std::vector<float> shown = body.ShownFromTheJump(kBudget);

    // Shown where it was on the frame the jump arrives, sliding forward from
    // there frame for frame as it does fed the blended total alone.
    ASSERT_GE(shown.size(), 2U);
    EXPECT_NEAR(shown.front(), 0.0F, 1e-5F) << frames_per_second << " fps";
    EXPECT_EQ(shown, body.ShownFedTheBlendedTotal(kBudget)) << frames_per_second << " fps";
    for (std::size_t i = 1; i < shown.size(); ++i) {
      EXPECT_GT(shown[i], shown[i - 1]) << frames_per_second << " fps, frame " << i;
      EXPECT_LE(shown[i], kJump) << "overshoot at " << frames_per_second << " fps, frame " << i;
    }
  }
}

}  // namespace
