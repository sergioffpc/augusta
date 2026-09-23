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
using augusta::server::MatchConfig;

constexpr const char* kVersion = "1.2.3";
// The one character the matches below offer, unless a test says otherwise.
constexpr const char* kCharacter = "characters/player";

PeerId Peer(std::uint32_t number) { return static_cast<PeerId>(number); }

TEST(MatchTest, AdmitsAClientWithTheMatchingVersion) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  const auto admission = match.Join(Peer(10), kVersion, kCharacter);

  ASSERT_TRUE(admission.has_value());
  EXPECT_EQ(match.SessionOf(Peer(10)), admission->session);
  EXPECT_EQ(match.PlayerCount(), 1U);
}

TEST(MatchTest, RefusesAnyOtherVersion) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  for (const char* other : {"", "1.2.4", "1.2", "1.2.3 ", "0.1.0"}) {
    EXPECT_EQ(match.Join(Peer(10), other, kCharacter).error(), JoinRefusal::kVersionMismatch) << other;
  }
  EXPECT_EQ(match.PlayerCount(), 0U);
  EXPECT_FALSE(match.SessionOf(Peer(10)).has_value());
}

TEST(MatchTest, SessionIdsAreUniqueAndIndependentOfTheTransportHandle) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  const auto first = match.Join(Peer(500), kVersion, kCharacter);
  const auto second = match.Join(Peer(501), kVersion, kCharacter);

  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_NE(first->session, second->session);
  EXPECT_NE(static_cast<std::uint32_t>(first->session), 500U);
}

TEST(MatchTest, AdmitsUpToCapacityAndRefusesTheNextAsFull) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});
  for (std::uint32_t i = 0; i < kMaxPlayers; ++i) {
    ASSERT_TRUE(match.Join(Peer(i), kVersion, kCharacter).has_value()) << i;
  }

  EXPECT_EQ(match.Join(Peer(kMaxPlayers), kVersion, kCharacter).error(), JoinRefusal::kMatchFull);
  EXPECT_EQ(match.PlayerCount(), kMaxPlayers);
}

TEST(MatchTest, AdmitsEveryCharacterTheScenarioOffers) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {"characters/sniper", "characters/medic"}});

  EXPECT_TRUE(match.Join(Peer(1), kVersion, "characters/sniper").has_value());
  EXPECT_TRUE(match.Join(Peer(2), kVersion, "characters/medic").has_value());
}

TEST(MatchTest, RefusesACharacterTheScenarioDoesNotOffer) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  for (const char* other : {"", "characters/sniper", "characters/player/", "Characters/Player"}) {
    EXPECT_EQ(match.Join(Peer(10), kVersion, other).error(), JoinRefusal::kUnknownCharacter) << other;
  }
  EXPECT_EQ(match.PlayerCount(), 0U);
}

TEST(MatchTest, AScenarioWithNoCharactersAdmitsNoOne) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {}});

  EXPECT_EQ(match.Join(Peer(1), kVersion, kCharacter).error(), JoinRefusal::kUnknownCharacter);
}

TEST(MatchTest, AVersionMismatchOutranksAnUnknownCharacter) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  EXPECT_EQ(match.Join(Peer(1), "other", "characters/nobody").error(), JoinRefusal::kVersionMismatch);
}

TEST(MatchTest, AnUnknownCharacterOutranksAFullMatch) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}, .capacity = 1});
  ASSERT_TRUE(match.Join(Peer(1), kVersion, kCharacter).has_value());

  EXPECT_EQ(match.Join(Peer(2), kVersion, "characters/nobody").error(), JoinRefusal::kUnknownCharacter);
}

TEST(MatchTest, ARefusedCharacterTakesNoSlotAndNoSpawnPoint) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}, .capacity = 1},
              {Vec3(1.0F, 0.0F, 0.0F), Vec3(2.0F, 0.0F, 0.0F)});
  ASSERT_FALSE(match.Join(Peer(1), kVersion, "characters/nobody").has_value());

  const auto admitted = match.Join(Peer(2), kVersion, kCharacter);
  ASSERT_TRUE(admitted.has_value());
  EXPECT_EQ(admitted->spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, AVersionMismatchIsReportedEvenWhenTheMatchIsFull) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}, .capacity = 1});
  ASSERT_TRUE(match.Join(Peer(1), kVersion, kCharacter).has_value());

  EXPECT_EQ(match.Join(Peer(2), "other", kCharacter).error(), JoinRefusal::kVersionMismatch);
}

TEST(MatchTest, LeavingFreesTheSlotAndNeverReusesTheSessionId) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}, .capacity = 1});
  const auto first = match.Join(Peer(1), kVersion, kCharacter);
  ASSERT_TRUE(first.has_value());

  match.Leave(Peer(1));
  const auto second = match.Join(Peer(2), kVersion, kCharacter);

  ASSERT_TRUE(second.has_value());
  EXPECT_NE(first->session, second->session);
  EXPECT_FALSE(match.SessionOf(Peer(1)).has_value());
}

TEST(MatchTest, LeavingWithoutHavingJoinedChangesNothing) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  match.Leave(Peer(99));

  EXPECT_EQ(match.PlayerCount(), 0U);
}

TEST(MatchTest, JoiningAgainReturnsTheSameSessionWithoutTakingAnotherSlot) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});
  const auto first = match.Join(Peer(1), kVersion, kCharacter);

  const auto again = match.Join(Peer(1), kVersion, kCharacter);

  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(first->session, again->session);
  EXPECT_EQ(first->spawn, again->spawn);
  EXPECT_EQ(match.PlayerCount(), 1U);
}

std::vector<Vec3> SpawnPoints() { return {Vec3(1.0F, 0.0F, 0.0F), Vec3(2.0F, 0.0F, 0.0F), Vec3(3.0F, 0.0F, 0.0F)}; }

TEST(MatchTest, PlayersTakeTheSpawnPointsInOrder) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}}, SpawnPoints());

  EXPECT_EQ(match.Join(Peer(1), kVersion, kCharacter)->spawn, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(match.Join(Peer(2), kVersion, kCharacter)->spawn, Vec3(2.0F, 0.0F, 0.0F));
  EXPECT_EQ(match.Join(Peer(3), kVersion, kCharacter)->spawn, Vec3(3.0F, 0.0F, 0.0F));
}

TEST(MatchTest, MoreJoinsThanSpawnPointsWrapAround) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}}, SpawnPoints());
  for (std::uint32_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(match.Join(Peer(i), kVersion, kCharacter).has_value());
  }

  const auto fourth = match.Join(Peer(3), kVersion, kCharacter);

  ASSERT_TRUE(fourth.has_value());
  EXPECT_EQ(fourth->spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, ARefusedJoinDoesNotUseUpASpawnPoint) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}}, SpawnPoints());
  ASSERT_FALSE(match.Join(Peer(1), "other", kCharacter).has_value());

  EXPECT_EQ(match.Join(Peer(2), kVersion, kCharacter)->spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, WithoutSpawnPointsPlayersSpawnAtTheOrigin) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  EXPECT_EQ(match.Join(Peer(1), kVersion, kCharacter)->spawn, Vec3{});
}

TEST(MatchTest, ThePlayerAloneInTheMatchHasAnEmptyRoster) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}}, SpawnPoints());

  EXPECT_TRUE(match.Join(Peer(1), kVersion, kCharacter)->roster.empty());
}

TEST(MatchTest, TheRosterListsThePlayersAlreadyThereAtTheirSpawnPointsBeforeAnyTick) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}}, SpawnPoints());
  const auto first = match.Join(Peer(1), kVersion, kCharacter);
  const auto second = match.Join(Peer(2), kVersion, kCharacter);

  const auto third = match.Join(Peer(3), kVersion, kCharacter);

  ASSERT_TRUE(third.has_value());
  ASSERT_EQ(third->roster.size(), 2U);
  EXPECT_EQ(third->roster[0].session, first->session);
  EXPECT_EQ(third->roster[0].body.position, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(third->roster[1].session, second->session);
  EXPECT_EQ(third->roster[1].body.position, Vec3(2.0F, 0.0F, 0.0F));
}

TEST(MatchTest, TheRosterHoldsWhereThePlayersWereLastReported) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}}, SpawnPoints());
  const auto first = match.Join(Peer(1), kVersion, kCharacter);
  augusta::physics::BodyState moved;
  moved.position = Vec3(9.0F, 0.0F, 9.0F);
  moved.stance = augusta::physics::Stance::kProne;
  match.UpdateBody(first->session, moved);

  const auto second = match.Join(Peer(2), kVersion, kCharacter);

  ASSERT_EQ(second->roster.size(), 1U);
  EXPECT_EQ(second->roster[0].body.position, moved.position);
  EXPECT_EQ(second->roster[0].body.stance, augusta::physics::Stance::kProne);
}

TEST(MatchTest, APlayerWhoLeftIsNotInTheRoster) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});
  ASSERT_TRUE(match.Join(Peer(1), kVersion, kCharacter).has_value());
  ASSERT_TRUE(match.Join(Peer(2), kVersion, kCharacter).has_value());

  match.Leave(Peer(1));

  const auto third = match.Join(Peer(3), kVersion, kCharacter);
  ASSERT_EQ(third->roster.size(), 1U);
  EXPECT_EQ(third->roster[0].session, *match.SessionOf(Peer(2)));
}

TEST(MatchTest, ReportingABodyForAnUnknownSessionChangesNothing) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {kCharacter}});

  match.UpdateBody(static_cast<augusta::protocol::SessionId>(77), augusta::physics::BodyState{});

  EXPECT_EQ(match.PlayerCount(), 0U);
}

}  // namespace
