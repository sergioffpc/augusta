#include "match.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

// Admission is pure bookkeeping: no socket is opened here.
namespace {

using augusta::math::Vec3;
using augusta::networking::PeerId;
using augusta::protocol::JoinRefusal;
using augusta::protocol::kMaxPlayers;
using augusta::server::Match;

constexpr const char* kVersion = "1.2.3";

PeerId Peer(std::uint32_t number) { return static_cast<PeerId>(number); }

TEST(MatchTest, AdmitsAClientWithTheMatchingVersion) {
  Match match(kVersion);

  const auto admission = match.Join(Peer(10), kVersion);

  ASSERT_TRUE(admission.has_value());
  EXPECT_EQ(match.SessionOf(Peer(10)), admission->session);
  EXPECT_EQ(match.PlayerCount(), 1U);
}

TEST(MatchTest, RefusesAnyOtherVersion) {
  Match match(kVersion);

  for (const char* other : {"", "1.2.4", "1.2", "1.2.3 ", "0.1.0"}) {
    EXPECT_EQ(match.Join(Peer(10), other).error(), JoinRefusal::kVersionMismatch) << other;
  }
  EXPECT_EQ(match.PlayerCount(), 0U);
  EXPECT_FALSE(match.SessionOf(Peer(10)).has_value());
}

TEST(MatchTest, SessionIdsAreUniqueAndIndependentOfTheTransportHandle) {
  Match match(kVersion);

  const auto first = match.Join(Peer(500), kVersion);
  const auto second = match.Join(Peer(501), kVersion);

  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_NE(first->session, second->session);
  EXPECT_NE(static_cast<std::uint32_t>(first->session), 500U);
}

TEST(MatchTest, AdmitsUpToCapacityAndRefusesTheNextAsFull) {
  Match match(kVersion);
  for (std::uint32_t i = 0; i < kMaxPlayers; ++i) {
    ASSERT_TRUE(match.Join(Peer(i), kVersion).has_value()) << i;
  }

  EXPECT_EQ(match.Join(Peer(kMaxPlayers), kVersion).error(), JoinRefusal::kMatchFull);
  EXPECT_EQ(match.PlayerCount(), kMaxPlayers);
}

TEST(MatchTest, AVersionMismatchIsReportedEvenWhenTheMatchIsFull) {
  Match match(kVersion, 1);
  ASSERT_TRUE(match.Join(Peer(1), kVersion).has_value());

  EXPECT_EQ(match.Join(Peer(2), "other").error(), JoinRefusal::kVersionMismatch);
}

TEST(MatchTest, LeavingFreesTheSlotAndNeverReusesTheSessionId) {
  Match match(kVersion, 1);
  const auto first = match.Join(Peer(1), kVersion);
  ASSERT_TRUE(first.has_value());

  match.Leave(Peer(1));
  const auto second = match.Join(Peer(2), kVersion);

  ASSERT_TRUE(second.has_value());
  EXPECT_NE(first->session, second->session);
  EXPECT_FALSE(match.SessionOf(Peer(1)).has_value());
}

TEST(MatchTest, LeavingWithoutHavingJoinedChangesNothing) {
  Match match(kVersion);

  match.Leave(Peer(99));

  EXPECT_EQ(match.PlayerCount(), 0U);
}

TEST(MatchTest, JoiningAgainReturnsTheSameSessionWithoutTakingAnotherSlot) {
  Match match(kVersion);
  const auto first = match.Join(Peer(1), kVersion);

  const auto again = match.Join(Peer(1), kVersion);

  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(first->session, again->session);
  EXPECT_EQ(first->spawn, again->spawn);
  EXPECT_EQ(match.PlayerCount(), 1U);
}

std::vector<Vec3> SpawnPoints() { return {Vec3(1.0F, 0.0F, 0.0F), Vec3(2.0F, 0.0F, 0.0F), Vec3(3.0F, 0.0F, 0.0F)}; }

TEST(MatchTest, PlayersTakeTheSpawnPointsInOrder) {
  Match match(kVersion, kMaxPlayers, SpawnPoints());

  EXPECT_EQ(match.Join(Peer(1), kVersion)->spawn, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(match.Join(Peer(2), kVersion)->spawn, Vec3(2.0F, 0.0F, 0.0F));
  EXPECT_EQ(match.Join(Peer(3), kVersion)->spawn, Vec3(3.0F, 0.0F, 0.0F));
}

TEST(MatchTest, MoreJoinsThanSpawnPointsWrapAround) {
  Match match(kVersion, kMaxPlayers, SpawnPoints());
  for (std::uint32_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(match.Join(Peer(i), kVersion).has_value());
  }

  const auto fourth = match.Join(Peer(3), kVersion);

  ASSERT_TRUE(fourth.has_value());
  EXPECT_EQ(fourth->spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, ARefusedJoinDoesNotUseUpASpawnPoint) {
  Match match(kVersion, kMaxPlayers, SpawnPoints());
  ASSERT_FALSE(match.Join(Peer(1), "other").has_value());

  EXPECT_EQ(match.Join(Peer(2), kVersion)->spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, WithoutSpawnPointsPlayersSpawnAtTheOrigin) {
  Match match(kVersion);

  EXPECT_EQ(match.Join(Peer(1), kVersion)->spawn, Vec3{});
}

TEST(MatchTest, ThePlayerAloneInTheMatchHasAnEmptyRoster) {
  Match match(kVersion, kMaxPlayers, SpawnPoints());

  EXPECT_TRUE(match.Join(Peer(1), kVersion)->roster.empty());
}

TEST(MatchTest, TheRosterListsThePlayersAlreadyThereAtTheirSpawnPointsBeforeAnyTick) {
  Match match(kVersion, kMaxPlayers, SpawnPoints());
  const auto first = match.Join(Peer(1), kVersion);
  const auto second = match.Join(Peer(2), kVersion);

  const auto third = match.Join(Peer(3), kVersion);

  ASSERT_TRUE(third.has_value());
  ASSERT_EQ(third->roster.size(), 2U);
  EXPECT_EQ(third->roster[0].session, first->session);
  EXPECT_EQ(third->roster[0].body.position, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(third->roster[1].session, second->session);
  EXPECT_EQ(third->roster[1].body.position, Vec3(2.0F, 0.0F, 0.0F));
}

TEST(MatchTest, TheRosterHoldsWhereThePlayersWereLastReported) {
  Match match(kVersion, kMaxPlayers, SpawnPoints());
  const auto first = match.Join(Peer(1), kVersion);
  augusta::physics::BodyState moved;
  moved.position = Vec3(9.0F, 0.0F, 9.0F);
  moved.stance = augusta::physics::Stance::kProne;
  match.UpdateBody(first->session, moved);

  const auto second = match.Join(Peer(2), kVersion);

  ASSERT_EQ(second->roster.size(), 1U);
  EXPECT_EQ(second->roster[0].body.position, moved.position);
  EXPECT_EQ(second->roster[0].body.stance, augusta::physics::Stance::kProne);
}

TEST(MatchTest, APlayerWhoLeftIsNotInTheRoster) {
  Match match(kVersion);
  ASSERT_TRUE(match.Join(Peer(1), kVersion).has_value());
  ASSERT_TRUE(match.Join(Peer(2), kVersion).has_value());

  match.Leave(Peer(1));

  const auto third = match.Join(Peer(3), kVersion);
  ASSERT_EQ(third->roster.size(), 1U);
  EXPECT_EQ(third->roster[0].session, *match.SessionOf(Peer(2)));
}

TEST(MatchTest, ReportingABodyForAnUnknownSessionChangesNothing) {
  Match match(kVersion);

  match.UpdateBody(static_cast<augusta::protocol::SessionId>(77), augusta::physics::BodyState{});

  EXPECT_EQ(match.PlayerCount(), 0U);
}

}  // namespace
