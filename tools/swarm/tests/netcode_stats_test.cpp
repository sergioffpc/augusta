#include "netcode_stats.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/prediction.h"

// What a Scripted player's ticks add up to, from Prediction States built by hand.
namespace {

using augusta::math::Vec3;
using augusta::prediction::State;
using augusta::swarm::NetcodeStats;
using augusta::swarm::NetcodeTally;

State StateAt(const Vec3& total_correction, std::uint32_t total_rounds_fired = 0) {
  State state;
  state.total_correction = total_correction;
  state.total_rounds_fired = total_rounds_fired;
  return state;
}

TEST(NetcodeTallyTest, StartsAtNothing) {
  const NetcodeStats stats = NetcodeTally().Stats();

  EXPECT_EQ(stats.match_ticks, 0U);
  EXPECT_EQ(stats.corrections, 0U);
  EXPECT_EQ(stats.largest_correction_m, 0.0F);
  EXPECT_EQ(stats.rounds_fired, 0U);
  EXPECT_EQ(stats.hit_confirmations, 0U);
}

TEST(NetcodeTallyTest, CountsOnlyTheTicksPlayedInAMatch) {
  NetcodeTally tally;
  tally.RecordTick(StateAt({}), false);
  tally.RecordTick(StateAt({}), true);
  tally.RecordTick(StateAt({}), true);

  EXPECT_EQ(tally.Stats().match_ticks, 2U);
}

TEST(NetcodeTallyTest, ATickWhoseTotalCorrectionMovedIsOneCorrectionOfThatLength) {
  NetcodeTally tally;
  tally.RecordTick(StateAt({}), true);
  tally.RecordTick(StateAt(Vec3(0.3F, 0.0F, 0.4F)), true);
  tally.RecordTick(StateAt(Vec3(0.3F, 0.0F, 0.4F)), true);
  tally.RecordTick(StateAt(Vec3(0.3F, 0.0F, 0.6F)), true);

  const NetcodeStats stats = tally.Stats();
  EXPECT_EQ(stats.corrections, 2U);
  EXPECT_FLOAT_EQ(stats.largest_correction_m, 0.5F);
}

TEST(NetcodeTallyTest, ACorrectionBackTowardsWhereTheBodyWasStillCounts) {
  NetcodeTally tally;
  tally.RecordTick(StateAt(Vec3(1.0F, 0.0F, 0.0F)), true);
  tally.RecordTick(StateAt({}), true);

  const NetcodeStats stats = tally.Stats();
  EXPECT_EQ(stats.corrections, 2U);
  EXPECT_FLOAT_EQ(stats.largest_correction_m, 1.0F);
}

TEST(NetcodeTallyTest, CountsTheRoundsFiredInAMatch) {
  NetcodeTally tally;
  tally.RecordTick(StateAt({}, 1), true);
  tally.RecordTick(StateAt({}, 1), true);
  tally.RecordTick(StateAt({}, 2), true);
  // The last tick's state, repeated outside a Match, fires nothing more.
  tally.RecordTick(StateAt({}, 2), false);
  tally.RecordTick(StateAt({}, 3), true);

  EXPECT_EQ(tally.Stats().rounds_fired, 3U);
}

TEST(NetcodeTallyTest, AddsUpTheHitConfirmationsItIsHanded) {
  NetcodeTally tally;
  tally.RecordHitConfirmations(2);
  tally.RecordHitConfirmations(0);
  tally.RecordHitConfirmations(3);

  EXPECT_EQ(tally.Stats().hit_confirmations, 5U);
}

TEST(NetcodeStatsTest, AddingAnotherPlayersSumsTheCountsAndKeepsTheLargerCorrection) {
  NetcodeStats all{
      .match_ticks = 10, .corrections = 1, .largest_correction_m = 0.5F, .rounds_fired = 4, .hit_confirmations = 2};
  all += NetcodeStats{
      .match_ticks = 20, .corrections = 3, .largest_correction_m = 0.2F, .rounds_fired = 6, .hit_confirmations = 5};

  EXPECT_EQ(all.match_ticks, 30U);
  EXPECT_EQ(all.corrections, 4U);
  EXPECT_FLOAT_EQ(all.largest_correction_m, 0.5F);
  EXPECT_EQ(all.rounds_fired, 10U);
  EXPECT_EQ(all.hit_confirmations, 7U);
}

TEST(NetcodeStatsTest, TheCorrectionRateIsTheShareOfMatchTicksWithACorrection) {
  EXPECT_EQ(NetcodeStats{}.CorrectionRate(), 0.0F);
  EXPECT_FLOAT_EQ((NetcodeStats{.match_ticks = 200, .corrections = 5}).CorrectionRate(), 0.025F);
}

TEST(NetcodeStatsTest, TheHitRateIsTheShareOfRoundsFiredTheServerConfirmedAsHits) {
  EXPECT_EQ(NetcodeStats{}.HitRate(), 0.0F);
  EXPECT_FLOAT_EQ((NetcodeStats{.rounds_fired = 40, .hit_confirmations = 10}).HitRate(), 0.25F);
}

}  // namespace
