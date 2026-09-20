#include "augusta/reconciliation.h"

#include <cstdint>

#include <gtest/gtest.h>

// The reconciliation decision and the history of predicted states are pure:
// no physics is stepped here.
namespace {

using augusta::math::Length;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::prediction::Apply;
using augusta::prediction::Correction;
using augusta::prediction::History;
using augusta::prediction::kBlendFactor;
using augusta::prediction::kMaxHistory;
using augusta::prediction::kSnapDistance;
using augusta::prediction::ResolveCorrection;

BodyState At(float x, float y = 0.0F, float z = 0.0F) {
  BodyState state{};
  state.position = Vec3(x, y, z);
  return state;
}

TEST(ResolveCorrectionTest, NoErrorMeansNoCorrection) {
  const Correction correction = ResolveCorrection(At(1.0F), At(1.0F));

  EXPECT_EQ(correction.position, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(correction.error, 0.0F);
  EXPECT_FALSE(correction.snapped);
  EXPECT_FALSE(correction.stance.has_value());
}

TEST(ResolveCorrectionTest, AnErrorBelowTheSnapDistanceIsBlendedIn) {
  const Correction correction = ResolveCorrection(At(0.0F), At(1.0F));

  EXPECT_FALSE(correction.snapped);
  EXPECT_NEAR(correction.error, 1.0F, 1e-6F);
  EXPECT_NEAR(correction.position.x, kBlendFactor, 1e-6F);
}

TEST(ResolveCorrectionTest, AnErrorJustBelowTheSnapDistanceIsStillBlended) {
  EXPECT_FALSE(ResolveCorrection(At(0.0F), At(kSnapDistance - 0.01F)).snapped);
}

TEST(ResolveCorrectionTest, AnErrorAtOrAboveTheSnapDistanceSnapsToTheWholeError) {
  for (const float distance : {kSnapDistance, kSnapDistance + 5.0F, 1000.0F}) {
    const Correction correction = ResolveCorrection(At(0.0F), At(distance));

    EXPECT_TRUE(correction.snapped) << distance;
    EXPECT_NEAR(correction.position.x, distance, 1e-3F) << distance;
  }
}

TEST(ResolveCorrectionTest, TheErrorIsMeasuredInThreeDimensions) {
  const Correction correction = ResolveCorrection(At(0.0F), At(1.0F, 2.0F, 2.0F));

  EXPECT_NEAR(correction.error, 3.0F, 1e-5F);
  EXPECT_TRUE(correction.snapped);
}

TEST(ResolveCorrectionTest, VelocityAndStaminaAreCorrectedByTheSameShare) {
  BodyState predicted = At(0.0F);
  BodyState authoritative = At(1.0F);
  authoritative.velocity = Vec3(2.0F, 0.0F, 0.0F);
  authoritative.stamina = 0.5F;

  const Correction correction = ResolveCorrection(predicted, authoritative);

  EXPECT_NEAR(correction.velocity.x, 2.0F * kBlendFactor, 1e-6F);
  EXPECT_NEAR(correction.stamina, -0.5F * kBlendFactor, 1e-6F);
}

TEST(ResolveCorrectionTest, AStanceTheServerRefusedIsTaken) {
  BodyState predicted = At(0.0F);
  predicted.stance = Stance::kStanding;
  BodyState authoritative = At(0.0F);
  authoritative.stance = Stance::kCrouching;

  const Correction correction = ResolveCorrection(predicted, authoritative);

  ASSERT_TRUE(correction.stance.has_value());
  EXPECT_EQ(*correction.stance, Stance::kCrouching);
}

TEST(ResolveCorrectionTest, RepeatedBlendsConvergeWithoutOvershootWithinTheNfr02Budget) {
  constexpr int kBudgetTicks = 9;  // 150 ms at 60 Hz.
  const BodyState authoritative = At(1.0F);
  BodyState predicted = At(0.0F);

  float previous_error = 1.0F;
  for (int i = 0; i < kBudgetTicks; ++i) {
    predicted = Apply(predicted, ResolveCorrection(predicted, authoritative));
    const float error = Length(authoritative.position - predicted.position);
    EXPECT_LT(error, previous_error) << i;
    EXPECT_LE(predicted.position.x, authoritative.position.x) << "overshoot at " << i;
    previous_error = error;
  }

  EXPECT_LT(previous_error, 0.05F);
}

TEST(ApplyTest, MovesPositionVelocityAndStaminaAndTakesTheStance) {
  BodyState state = At(1.0F);
  state.stamina = 1.0F;
  Correction correction;
  correction.position = Vec3(0.5F, 0.0F, 0.0F);
  correction.velocity = Vec3(0.0F, 1.0F, 0.0F);
  correction.stamina = -0.25F;
  correction.stance = Stance::kProne;

  const BodyState corrected = Apply(state, correction);

  EXPECT_EQ(corrected.position, Vec3(1.5F, 0.0F, 0.0F));
  EXPECT_EQ(corrected.velocity, Vec3(0.0F, 1.0F, 0.0F));
  EXPECT_EQ(corrected.stamina, 0.75F);
  EXPECT_EQ(corrected.stance, Stance::kProne);
}

TEST(HistoryTest, AcknowledgeReturnsTheStatePredictedAfterThatCommand) {
  History history;
  history.Record(1, At(1.0F));
  history.Record(2, At(2.0F));
  history.Record(3, At(3.0F));

  const auto predicted = history.Acknowledge(2);

  ASSERT_TRUE(predicted.has_value());
  EXPECT_EQ(predicted->position.x, 2.0F);
}

TEST(HistoryTest, AcknowledgingDiscardsWhatIsOlderAndTheStateItself) {
  History history;
  for (std::uint32_t sequence = 1; sequence <= 5; ++sequence) {
    history.Record(sequence, At(static_cast<float>(sequence)));
  }

  ASSERT_TRUE(history.Acknowledge(3).has_value());

  EXPECT_EQ(history.Size(), 2U);
  EXPECT_FALSE(history.Acknowledge(3).has_value());
  EXPECT_FALSE(history.Acknowledge(2).has_value());
  EXPECT_TRUE(history.Acknowledge(4).has_value());
}

TEST(HistoryTest, ARepeatedAcknowledgementIsStaleAndFindsNothing) {
  History history;
  history.Record(1, At(1.0F));
  history.Record(2, At(2.0F));
  ASSERT_TRUE(history.Acknowledge(1).has_value());

  EXPECT_FALSE(history.Acknowledge(1).has_value());
  EXPECT_EQ(history.Size(), 1U);
}

TEST(HistoryTest, AnAcknowledgementOlderThanEverythingHeldChangesNothing) {
  History history;
  history.Record(10, At(1.0F));
  history.Record(11, At(2.0F));

  EXPECT_FALSE(history.Acknowledge(4).has_value());

  EXPECT_EQ(history.Size(), 2U);
}

TEST(HistoryTest, AnAcknowledgementOfACommandNeverRecordedFindsNothing) {
  History history;
  history.Record(1, At(1.0F));
  history.Record(3, At(3.0F));

  EXPECT_FALSE(history.Acknowledge(2).has_value());
  EXPECT_TRUE(history.Acknowledge(3).has_value());
}

TEST(HistoryTest, ShiftMovesEveryHeldStateButNotTheirStance) {
  History history;
  BodyState crouched = At(1.0F);
  crouched.stance = Stance::kCrouching;
  history.Record(1, crouched);
  history.Record(2, At(2.0F));
  Correction correction;
  correction.position = Vec3(0.5F, 0.0F, 0.0F);
  correction.stance = Stance::kProne;

  history.Shift(correction);

  const auto first = history.Acknowledge(1);
  const auto second = history.Acknowledge(2);
  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_EQ(first->position.x, 1.5F);
  EXPECT_EQ(first->stance, Stance::kCrouching);
  EXPECT_EQ(second->position.x, 2.5F);
}

TEST(HistoryTest, OnlyTheMostRecentStatesAreKept) {
  History history;
  const auto total = static_cast<std::uint32_t>(kMaxHistory + 10);
  for (std::uint32_t sequence = 1; sequence <= total; ++sequence) {
    history.Record(sequence, At(0.0F));
  }

  EXPECT_EQ(history.Size(), kMaxHistory);
  EXPECT_FALSE(history.Acknowledge(1).has_value());
}

}  // namespace
