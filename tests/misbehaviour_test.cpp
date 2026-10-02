#include "misbehaviour.h"

#include <chrono>
#include <cstddef>

#include <gtest/gtest.h>

// The tracker is pure: time is handed to it, and no peer is ever really disconnected here.
namespace {

using augusta::server::IsMisbehaviour;
using augusta::server::kMisbehaviourThreshold;
using augusta::server::kMisbehaviourWindow;
using augusta::server::MisbehaviourTracker;
using augusta::server::PeerRejection;
using augusta::server::Verdict;
using Clock = std::chrono::steady_clock;

const Clock::time_point kStart{};

// Records rejection count times at now; returns the last verdict.
Verdict RecordTimes(MisbehaviourTracker& tracker, PeerRejection rejection, std::size_t count,
                    Clock::time_point now = kStart) {
  Verdict verdict = Verdict::kKeep;
  for (std::size_t i = 0; i < count; ++i) {
    verdict = tracker.Record(rejection, now);
  }
  return verdict;
}

TEST(MisbehaviourTrackerTest, BelowTheThresholdThePeerIsKept) {
  MisbehaviourTracker tracker;

  for (std::size_t i = 1; i < kMisbehaviourThreshold; ++i) {
    EXPECT_EQ(tracker.Record(PeerRejection::kUndecodable, kStart), Verdict::kKeep) << "rejection " << i;
  }
}

TEST(MisbehaviourTrackerTest, AtTheThresholdADisconnectIsDecided) {
  MisbehaviourTracker tracker;
  RecordTimes(tracker, PeerRejection::kUndecodable, kMisbehaviourThreshold - 1);

  EXPECT_EQ(tracker.Record(PeerRejection::kUndecodable, kStart), Verdict::kDisconnect);
}

TEST(MisbehaviourTrackerTest, EveryNonRoutineRejectionCountsTowardTheSameThreshold) {
  MisbehaviourTracker tracker;
  const PeerRejection kinds[] = {PeerRejection::kUndecodable, PeerRejection::kNotAClientMessage,
                                 PeerRejection::kNonFiniteCommand, PeerRejection::kOutOfRangeCommand,
                                 PeerRejection::kCommandsBeforeJoining};

  Verdict verdict = Verdict::kKeep;
  for (std::size_t i = 0; i < kMisbehaviourThreshold; ++i) {
    verdict = tracker.Record(kinds[i % std::size(kinds)], kStart);
  }

  EXPECT_EQ(verdict, Verdict::kDisconnect);
}

TEST(MisbehaviourTrackerTest, RejectionsOlderThanTheWindowAgeOut) {
  MisbehaviourTracker tracker;
  RecordTimes(tracker, PeerRejection::kUndecodable, kMisbehaviourThreshold - 1);

  EXPECT_EQ(tracker.Record(PeerRejection::kUndecodable, kStart + kMisbehaviourWindow), Verdict::kKeep);
}

TEST(MisbehaviourTrackerTest, RejectionsStillInsideTheWindowCount) {
  MisbehaviourTracker tracker;
  RecordTimes(tracker, PeerRejection::kUndecodable, kMisbehaviourThreshold - 1);

  EXPECT_EQ(tracker.Record(PeerRejection::kUndecodable, kStart + kMisbehaviourWindow - std::chrono::milliseconds(1)),
            Verdict::kDisconnect);
}

TEST(MisbehaviourTrackerTest, ASteadyTrickleBelowTheRateNeverDisconnects) {
  MisbehaviourTracker tracker;
  // A little slower than one fewer than the threshold per window, forever.
  const auto interval =
      std::chrono::milliseconds(kMisbehaviourWindow) / (kMisbehaviourThreshold - 1) + std::chrono::milliseconds(1);

  for (std::size_t i = 0; i < 10 * kMisbehaviourThreshold; ++i) {
    EXPECT_EQ(tracker.Record(PeerRejection::kOutOfRangeCommand, kStart + (i * interval)), Verdict::kKeep)
        << "rejection " << i;
  }
}

TEST(MisbehaviourTrackerTest, RoutineRejectionsNeverCount) {
  MisbehaviourTracker tracker;
  const PeerRejection routine[] = {PeerRejection::kStaleCommand, PeerRejection::kCommandsOutsideMatch,
                                   PeerRejection::kStaleReady, PeerRejection::kJoinRefused};

  for (const PeerRejection rejection : routine) {
    EXPECT_EQ(RecordTimes(tracker, rejection, 10 * kMisbehaviourThreshold), Verdict::kKeep);
  }
  // Nor do they push a peer that misbehaved over the threshold.
  RecordTimes(tracker, PeerRejection::kUndecodable, kMisbehaviourThreshold - 1);
  EXPECT_EQ(RecordTimes(tracker, PeerRejection::kStaleCommand, kMisbehaviourThreshold), Verdict::kKeep);
}

TEST(MisbehaviourTrackerTest, OnlyTheNonRoutineRejectionsAreMisbehaviour) {
  EXPECT_TRUE(IsMisbehaviour(PeerRejection::kUndecodable));
  EXPECT_TRUE(IsMisbehaviour(PeerRejection::kNotAClientMessage));
  EXPECT_TRUE(IsMisbehaviour(PeerRejection::kNonFiniteCommand));
  EXPECT_TRUE(IsMisbehaviour(PeerRejection::kOutOfRangeCommand));
  EXPECT_TRUE(IsMisbehaviour(PeerRejection::kCommandsBeforeJoining));
  EXPECT_FALSE(IsMisbehaviour(PeerRejection::kStaleCommand));
  EXPECT_FALSE(IsMisbehaviour(PeerRejection::kCommandsOutsideMatch));
  EXPECT_FALSE(IsMisbehaviour(PeerRejection::kStaleReady));
  EXPECT_FALSE(IsMisbehaviour(PeerRejection::kJoinRefused));
}

}  // namespace
