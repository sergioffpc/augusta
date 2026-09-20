#include "match.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "augusta/networking.h"
#include "augusta/protocol.h"

// Admission is pure bookkeeping: no socket is opened here.
namespace {

using augusta::networking::PeerId;
using augusta::protocol::JoinRefusal;
using augusta::server::kMaxPlayers;
using augusta::server::Match;

constexpr const char* kVersion = "1.2.3";

PeerId Peer(std::uint32_t number) { return static_cast<PeerId>(number); }

TEST(MatchTest, AdmitsAClientWithTheMatchingVersion) {
  Match match(kVersion);

  const auto session = match.Join(Peer(10), kVersion);

  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(match.SessionOf(Peer(10)), *session);
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
  EXPECT_NE(*first, *second);
  EXPECT_NE(static_cast<std::uint32_t>(*first), 500U);
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
  EXPECT_NE(*first, *second);
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

  EXPECT_EQ(first, again);
  EXPECT_EQ(match.PlayerCount(), 1U);
}

}  // namespace
