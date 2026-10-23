#include "augusta/correction_tally.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/prediction.h"

// How often and how far Reconciliation moved the body, from Prediction States
// built by hand.
namespace {

using augusta::math::Vec3;
using augusta::prediction::Corrections;
using augusta::prediction::CorrectionTally;
using augusta::prediction::State;

State StateAt(const Vec3& total_correction, std::uint32_t rifle_corrections = 0) {
  State state;
  state.total_correction = total_correction;
  state.rifle_corrections = rifle_corrections;
  return state;
}

TEST(CorrectionTallyTest, NoMovementIsNoCorrection) {
  CorrectionTally tally;
  tally.RecordTick(StateAt({}), true);
  tally.RecordTick(StateAt({}), true);

  const Corrections corrections = tally.Totals();
  EXPECT_EQ(corrections.count, 0U);
  EXPECT_EQ(corrections.last_m, 0.0F);
  EXPECT_EQ(corrections.largest_m, 0.0F);
  EXPECT_EQ(corrections.rifle_count, 0U);
}

TEST(CorrectionTallyTest, ATickWhoseTotalCorrectionMovedIsOneCorrectionOfThatLength) {
  CorrectionTally tally;
  tally.RecordTick(StateAt({}), true);
  tally.RecordTick(StateAt(Vec3(0.3F, 0.0F, 0.4F)), true);
  tally.RecordTick(StateAt(Vec3(0.3F, 0.0F, 0.4F)), true);

  const Corrections corrections = tally.Totals();
  EXPECT_EQ(corrections.count, 1U);
  EXPECT_FLOAT_EQ(corrections.last_m, 0.5F);
}

TEST(CorrectionTallyTest, ACorrectionBackTowardsWhereTheBodyWasStillCounts) {
  CorrectionTally tally;
  tally.RecordTick(StateAt(Vec3(1.0F, 0.0F, 0.0F)), true);
  tally.RecordTick(StateAt({}), true);

  const Corrections corrections = tally.Totals();
  EXPECT_EQ(corrections.count, 2U);
  EXPECT_FLOAT_EQ(corrections.last_m, 1.0F);
}

TEST(CorrectionTallyTest, KeepsTheLargestCorrectionAndReplacesTheLast) {
  CorrectionTally tally;
  tally.RecordTick(StateAt(Vec3(0.0F, 0.0F, 0.5F)), true);
  tally.RecordTick(StateAt(Vec3(0.0F, 0.0F, 0.7F)), true);

  const Corrections corrections = tally.Totals();
  EXPECT_FLOAT_EQ(corrections.largest_m, 0.5F);
  EXPECT_FLOAT_EQ(corrections.last_m, 0.2F);
}

TEST(CorrectionTallyTest, RifleCorrectionsFollowTheirRunningTotal) {
  CorrectionTally tally;
  tally.RecordTick(StateAt({}, 1), true);
  tally.RecordTick(StateAt({}, 1), true);
  tally.RecordTick(StateAt({}, 3), true);

  EXPECT_EQ(tally.Totals().rifle_count, 3U);
}

TEST(CorrectionTallyTest, TicksNotCountedFollowTheTotalsButAddNothing) {
  CorrectionTally tally;
  // Control: a counted tick that moves both totals adds to the counts.
  tally.RecordTick(StateAt(Vec3(1.0F, 0.0F, 0.0F), 1), true);
  // Outside a Match: the totals move, nothing is counted.
  tally.RecordTick(StateAt(Vec3(3.0F, 0.0F, 0.0F), 4), false);
  // Back in a Match, the same totals again: nothing moved since.
  tally.RecordTick(StateAt(Vec3(3.0F, 0.0F, 0.0F), 4), true);

  const Corrections corrections = tally.Totals();
  EXPECT_EQ(corrections.count, 1U);
  EXPECT_FLOAT_EQ(corrections.last_m, 1.0F);
  EXPECT_FLOAT_EQ(corrections.largest_m, 1.0F);
  EXPECT_EQ(corrections.rifle_count, 1U);
}

}  // namespace
