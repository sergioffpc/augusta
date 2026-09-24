#include "match.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"

// The Lobby and the match are pure bookkeeping: no socket is opened here.
namespace {

using augusta::math::Vec3;
using augusta::networking::PeerId;
using augusta::protocol::JoinRefusal;
using augusta::protocol::JoinRequest;
using augusta::protocol::kMaxPlayers;
using augusta::protocol::PackHash;
using augusta::protocol::SessionId;
using augusta::server::Departure;
using augusta::server::Match;
using augusta::server::MatchConfig;
using augusta::server::MatchStart;

constexpr const char* kVersion = "1.2.3";
// The one character the matches below offer, unless a test says otherwise.
constexpr const char* kCharacter = "characters/player";

PeerId Peer(std::uint32_t number) { return static_cast<PeerId>(number); }

// A request to join with version and character, having loaded client_pack: by
// default the one every match below is cooked with, unless a test says otherwise.
JoinRequest Request(std::string version, std::string character, const PackHash& client_pack = {}) {
  return JoinRequest{
      .engine_version = std::move(version), .client_pack = client_pack, .character = std::move(character)};
}

// A client pack other than the one the matches below are cooked with.
PackHash OtherClientPack() {
  PackHash hash{};
  hash.back() = std::byte{1};
  return hash;
}

// A match of player_count offering kCharacter alone, pausing pause_ticks after each match.
MatchConfig Config(std::size_t player_count = kMaxPlayers, std::uint32_t pause_ticks = 0) {
  return MatchConfig{
      .engine_version = kVersion, .characters = {kCharacter}, .player_count = player_count, .pause_ticks = pause_ticks};
}

// The peers the tests below number their clients within.
constexpr std::uint32_t kPeerNumbers = 16;

// Every peer that has joined reports Ready for the current Roster.
void ReadyEveryone(Match& match) {
  for (std::uint32_t i = 0; i < kPeerNumbers; ++i) {
    match.Ready(Peer(i), match.GetRoster().version);
  }
}

std::optional<MatchStart> ReadyAndStart(Match& match) {
  ReadyEveryone(match);
  return match.TryStart();
}

std::vector<Vec3> SpawnPoints() { return {Vec3(1.0F, 0.0F, 0.0F), Vec3(2.0F, 0.0F, 0.0F), Vec3(3.0F, 0.0F, 0.0F)}; }

// Joins peers first to first + count - 1, all of which must be admitted.
void JoinPeers(Match& match, std::uint32_t first, std::uint32_t count) {
  for (std::uint32_t i = first; i < first + count; ++i) {
    ASSERT_TRUE(match.Join(Peer(i), Request(kVersion, kCharacter)).has_value()) << i;
  }
}

TEST(MatchTest, AdmitsAClientWithTheMatchingVersionToTheLobby) {
  Match match(Config());

  const auto admission = match.Join(Peer(10), Request(kVersion, kCharacter));

  ASSERT_TRUE(admission.has_value());
  EXPECT_EQ(match.SessionOf(Peer(10)), admission->session);
  EXPECT_EQ(match.PlayerCount(), 1U);
  EXPECT_FALSE(match.InMatch());
}

TEST(MatchTest, RefusesAnyOtherVersion) {
  Match match(Config());

  for (const char* other : {"", "1.2.4", "1.2", "1.2.3 ", "0.1.0"}) {
    EXPECT_EQ(match.Join(Peer(10), Request(other, kCharacter)).error(), JoinRefusal::kVersionMismatch) << other;
  }
  EXPECT_EQ(match.PlayerCount(), 0U);
  EXPECT_FALSE(match.SessionOf(Peer(10)).has_value());
}

TEST(MatchTest, AdmitsOnlyTheClientPackCookedWithTheServers) {
  MatchConfig config = Config();
  config.client_pack = OtherClientPack();
  Match match(config);

  EXPECT_EQ(match.Join(Peer(1), Request(kVersion, kCharacter)).error(), JoinRefusal::kPackMismatch);
  EXPECT_TRUE(match.Join(Peer(2), Request(kVersion, kCharacter, OtherClientPack())).has_value());
  EXPECT_EQ(match.PlayerCount(), 1U);
}

TEST(MatchTest, ChecksTheClientPackAfterTheVersionAndBeforeTheCharacter) {
  Match match(Config());

  EXPECT_EQ(match.Join(Peer(1), Request("other", kCharacter, OtherClientPack())).error(),
            JoinRefusal::kVersionMismatch);
  EXPECT_EQ(match.Join(Peer(2), Request(kVersion, "characters/nobody", OtherClientPack())).error(),
            JoinRefusal::kPackMismatch);
}

TEST(MatchTest, SessionIdsAreUniqueAndIndependentOfTheTransportHandle) {
  Match match(Config());

  const auto first = match.Join(Peer(500), Request(kVersion, kCharacter));
  const auto second = match.Join(Peer(501), Request(kVersion, kCharacter));

  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_NE(first->session, second->session);
  EXPECT_NE(static_cast<std::uint32_t>(first->session), 500U);
}

TEST(MatchTest, AdmitsUpToThePlayerCountAndRefusesTheNextAsLobbyFull) {
  Match match(Config(3));
  JoinPeers(match, 0, 3);

  EXPECT_EQ(match.Join(Peer(3), Request(kVersion, kCharacter)).error(), JoinRefusal::kLobbyFull);
  EXPECT_EQ(match.PlayerCount(), 3U);
}

TEST(MatchTest, EachPlayerIsAdmittedWithTheIndexOfItsCharacter) {
  Match match(MatchConfig{
      .engine_version = kVersion, .characters = {"characters/sniper", "characters/medic"}, .player_count = 2});

  EXPECT_EQ(match.Join(Peer(1), Request(kVersion, "characters/sniper"))->character, 1U);
  EXPECT_EQ(match.Join(Peer(2), Request(kVersion, "characters/medic"))->character, 2U);
}

TEST(MatchTest, RefusesACharacterTheScenarioDoesNotOffer) {
  Match match(Config());

  for (const char* other : {"", "characters/sniper", "characters/player/", "Characters/Player"}) {
    EXPECT_EQ(match.Join(Peer(10), Request(kVersion, other)).error(), JoinRefusal::kUnknownCharacter) << other;
  }
  EXPECT_EQ(match.PlayerCount(), 0U);
}

TEST(MatchTest, AScenarioWithNoCharactersAdmitsNoOne) {
  Match match(MatchConfig{.engine_version = kVersion, .characters = {}});

  EXPECT_EQ(match.Join(Peer(1), Request(kVersion, kCharacter)).error(), JoinRefusal::kUnknownCharacter);
}

TEST(MatchTest, AVersionMismatchOutranksAnUnknownCharacter) {
  Match match(Config());

  EXPECT_EQ(match.Join(Peer(1), Request("other", "characters/nobody")).error(), JoinRefusal::kVersionMismatch);
}

TEST(MatchTest, AnUnknownCharacterOutranksAFullLobby) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);

  EXPECT_EQ(match.Join(Peer(2), Request(kVersion, "characters/nobody")).error(), JoinRefusal::kUnknownCharacter);
}

TEST(MatchTest, AVersionMismatchIsReportedEvenWhenTheLobbyIsFull) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);

  EXPECT_EQ(match.Join(Peer(2), Request("other", kCharacter)).error(), JoinRefusal::kVersionMismatch);
}

TEST(MatchTest, AJoinDuringAMatchIsRefusedAsMatchInProgress) {
  Match match(Config(2));
  JoinPeers(match, 1, 2);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  EXPECT_EQ(match.Join(Peer(3), Request(kVersion, kCharacter)).error(), JoinRefusal::kMatchInProgress);
}

// Joining later is no use to a client that can never play here.
TEST(MatchTest, AVersionOrCharacterOutranksAMatchInProgress) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  EXPECT_EQ(match.Join(Peer(2), Request("other", kCharacter)).error(), JoinRefusal::kVersionMismatch);
  EXPECT_EQ(match.Join(Peer(3), Request(kVersion, "characters/nobody")).error(), JoinRefusal::kUnknownCharacter);
}

// A match in progress holds the Player count, so it is full as well: the
// client should hear that one ends, not that one is full.
TEST(MatchTest, AMatchInProgressOutranksAFullLobby) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  EXPECT_EQ(match.Join(Peer(2), Request(kVersion, kCharacter)).error(), JoinRefusal::kMatchInProgress);
}

TEST(MatchTest, LeavingFreesTheSlotAndNeverReusesTheSessionId) {
  Match match(Config(1));
  const auto first = match.Join(Peer(1), Request(kVersion, kCharacter));
  ASSERT_TRUE(first.has_value());

  EXPECT_EQ(match.Leave(Peer(1)), Departure::kFromLobby);
  const auto second = match.Join(Peer(2), Request(kVersion, kCharacter));

  ASSERT_TRUE(second.has_value());
  EXPECT_NE(first->session, second->session);
  EXPECT_FALSE(match.SessionOf(Peer(1)).has_value());
}

TEST(MatchTest, LeavingWithoutHavingJoinedChangesNothing) {
  Match match(Config());
  const auto version = match.GetRoster().version;

  EXPECT_EQ(match.Leave(Peer(99)), Departure::kNone);

  EXPECT_EQ(match.PlayerCount(), 0U);
  EXPECT_EQ(match.GetRoster().version, version);
}

TEST(MatchTest, JoiningAgainReturnsTheSameAdmissionWithoutTakingAnotherSlot) {
  Match match(Config());
  const auto first = match.Join(Peer(1), Request(kVersion, kCharacter));
  const auto version = match.GetRoster().version;

  const auto again = match.Join(Peer(1), Request(kVersion, kCharacter));

  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(first->session, again->session);
  EXPECT_EQ(match.PlayerCount(), 1U);
  EXPECT_EQ(match.GetRoster().version, version);
}

TEST(MatchTest, TheRosterListsEveryLobbyPlayerWithItsCharacterBySession) {
  Match match(MatchConfig{
      .engine_version = kVersion, .characters = {"characters/sniper", "characters/medic"}, .player_count = 2});
  const auto medic = match.Join(Peer(9), Request(kVersion, "characters/medic"));
  const auto sniper = match.Join(Peer(3), Request(kVersion, "characters/sniper"));

  const auto roster = match.GetRoster();

  ASSERT_EQ(roster.players.size(), 2U);
  EXPECT_EQ(roster.players[0].session, medic->session);
  EXPECT_EQ(roster.players[0].character, 2U);
  EXPECT_EQ(roster.players[1].session, sniper->session);
  EXPECT_EQ(roster.players[1].character, 1U);
}

TEST(MatchTest, TheRosterVersionGrowsOnEveryJoinAndLeave) {
  Match match(Config());
  const auto empty = match.GetRoster().version;

  JoinPeers(match, 1, 1);
  const auto one = match.GetRoster().version;
  JoinPeers(match, 2, 1);
  const auto two = match.GetRoster().version;
  match.Leave(Peer(1));
  const auto left = match.GetRoster().version;

  EXPECT_GT(one, empty);
  EXPECT_GT(two, one);
  EXPECT_GT(left, two);
  ASSERT_EQ(match.GetRoster().players.size(), 1U);
  EXPECT_EQ(match.GetRoster().players[0].session, *match.SessionOf(Peer(2)));
}

TEST(MatchTest, ARefusedJoinTakesNoSlotAndLeavesTheRosterAsItWas) {
  Match match(Config(1));
  const auto version = match.GetRoster().version;
  ASSERT_FALSE(match.Join(Peer(1), Request("other", kCharacter)).has_value());
  ASSERT_FALSE(match.Join(Peer(2), Request(kVersion, "characters/nobody")).has_value());

  EXPECT_EQ(match.GetRoster().version, version);
  EXPECT_TRUE(match.Join(Peer(3), Request(kVersion, kCharacter)).has_value());
}

TEST(MatchTest, AMatchDoesNotStartBeforeTheLobbyHoldsThePlayerCount) {
  Match match(Config(3));
  JoinPeers(match, 1, 2);

  EXPECT_FALSE(ReadyAndStart(match).has_value());
  EXPECT_FALSE(match.InMatch());
}

TEST(MatchTest, AMatchStartsOnceTheLobbyHoldsThePlayerCount) {
  Match match(Config(2));
  JoinPeers(match, 1, 2);

  const auto start = ReadyAndStart(match);

  ASSERT_TRUE(start.has_value());
  EXPECT_TRUE(match.InMatch());
  ASSERT_EQ(start->players.size(), 2U);
  EXPECT_EQ(start->players[0].session, *match.SessionOf(Peer(1)));
  EXPECT_EQ(start->players[1].session, *match.SessionOf(Peer(2)));
  EXPECT_EQ(match.Playing(), (std::vector<SessionId>{*match.SessionOf(Peer(1)), *match.SessionOf(Peer(2))}));
}

TEST(MatchTest, AMatchInProgressDoesNotStartAgain) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  EXPECT_FALSE(ReadyAndStart(match).has_value());
}

TEST(MatchTest, MatchStartTellsEachPlayersCharacter) {
  Match match(MatchConfig{
      .engine_version = kVersion, .characters = {"characters/sniper", "characters/medic"}, .player_count = 2});
  ASSERT_TRUE(match.Join(Peer(1), Request(kVersion, "characters/medic")).has_value());
  ASSERT_TRUE(match.Join(Peer(2), Request(kVersion, "characters/sniper")).has_value());

  const auto start = ReadyAndStart(match);

  ASSERT_TRUE(start.has_value());
  EXPECT_EQ(start->players[0].character, 2U);
  EXPECT_EQ(start->players[1].character, 1U);
}

TEST(MatchTest, PlayersTakeTheSpawnPointsInOrderAtMatchStart) {
  Match match(Config(3), SpawnPoints());
  JoinPeers(match, 1, 3);

  const auto start = ReadyAndStart(match);

  ASSERT_TRUE(start.has_value());
  EXPECT_EQ(start->players[0].spawn, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(start->players[1].spawn, Vec3(2.0F, 0.0F, 0.0F));
  EXPECT_EQ(start->players[2].spawn, Vec3(3.0F, 0.0F, 0.0F));
}

TEST(MatchTest, MorePlayersThanSpawnPointsWrapAround) {
  Match match(Config(4), SpawnPoints());
  JoinPeers(match, 1, 4);

  const auto start = ReadyAndStart(match);

  ASSERT_TRUE(start.has_value());
  EXPECT_EQ(start->players[3].spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, WithoutSpawnPointsPlayersSpawnAtTheOrigin) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);

  EXPECT_EQ(ReadyAndStart(match)->players[0].spawn, Vec3{});
}

TEST(MatchTest, ARefusedJoinDoesNotUseUpASpawnPoint) {
  Match match(Config(1), SpawnPoints());
  ASSERT_FALSE(match.Join(Peer(1), Request("other", kCharacter)).has_value());
  ASSERT_FALSE(match.Join(Peer(2), Request(kVersion, "characters/nobody")).has_value());
  JoinPeers(match, 3, 1);

  EXPECT_EQ(ReadyAndStart(match)->players[0].spawn, Vec3(1.0F, 0.0F, 0.0F));
}

TEST(MatchTest, APlayerWhoLeftTheLobbyTakesNoSpawnPoint) {
  Match match(Config(2), SpawnPoints());
  JoinPeers(match, 1, 2);
  match.Leave(Peer(1));
  JoinPeers(match, 3, 1);

  const auto start = ReadyAndStart(match);

  ASSERT_TRUE(start.has_value());
  EXPECT_EQ(start->players[0].spawn, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(start->players[1].spawn, Vec3(2.0F, 0.0F, 0.0F));
}

TEST(MatchTest, TheRosterIsEmptyWhileAMatchIsInProgress) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  EXPECT_TRUE(match.GetRoster().players.empty());
}

TEST(MatchTest, APlayerWhoLeavesAMatchIsNoLongerInIt) {
  Match match(Config(2));
  JoinPeers(match, 1, 2);
  ASSERT_TRUE(ReadyAndStart(match).has_value());
  const SessionId leaver = *match.SessionOf(Peer(1));

  EXPECT_EQ(match.Leave(Peer(1)), Departure::kFromMatch);

  EXPECT_FALSE(match.IsPlaying(leaver));
  EXPECT_TRUE(match.IsPlaying(*match.SessionOf(Peer(2))));
  EXPECT_EQ(match.Playing().size(), 1U);
  EXPECT_TRUE(match.InMatch());
}

TEST(MatchTest, NoOneIsPlayingInTheLobby) {
  Match match(Config(2));
  JoinPeers(match, 1, 1);

  EXPECT_FALSE(match.IsPlaying(*match.SessionOf(Peer(1))));
  EXPECT_TRUE(match.Playing().empty());
}

TEST(MatchTest, AFullLobbyDoesNotStartUntilEveryoneIsReadyForTheCurrentRoster) {
  Match match(Config(2));
  JoinPeers(match, 1, 2);
  const auto version = match.GetRoster().version;

  EXPECT_TRUE(match.Ready(Peer(1), version));
  EXPECT_FALSE(match.TryStart().has_value());
  EXPECT_TRUE(match.Ready(Peer(2), version));
  EXPECT_TRUE(match.TryStart().has_value());
}

TEST(MatchTest, AReadyForAnOlderRosterDoesNotCount) {
  Match match(Config(2));
  JoinPeers(match, 1, 1);
  const auto before = match.GetRoster().version;
  JoinPeers(match, 2, 1);
  ASSERT_TRUE(match.Ready(Peer(2), match.GetRoster().version));

  EXPECT_FALSE(match.Ready(Peer(1), before));

  EXPECT_FALSE(match.TryStart().has_value());
}

TEST(MatchTest, ANewcomerMakesEveryoneAlreadyThereNotReady) {
  Match match(Config(2));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(match.Ready(Peer(1), match.GetRoster().version));

  JoinPeers(match, 2, 1);
  ASSERT_TRUE(match.Ready(Peer(2), match.GetRoster().version));

  EXPECT_FALSE(match.TryStart().has_value());
  ASSERT_TRUE(match.Ready(Peer(1), match.GetRoster().version));
  EXPECT_TRUE(match.TryStart().has_value());
}

TEST(MatchTest, APlayerLeavingTheLobbyMakesNoOneNotReady) {
  Match match(Config(3));
  JoinPeers(match, 1, 3);
  ReadyEveryone(match);

  match.Leave(Peer(3));

  EXPECT_TRUE(match.IsReady(Peer(1)));
  EXPECT_TRUE(match.IsReady(Peer(2)));
}

// Someone who was not Ready before a departure is not made Ready by it.
TEST(MatchTest, APlayerLeavingTheLobbyMakesNoOneReadyEither) {
  Match match(Config(3));
  JoinPeers(match, 1, 3);
  ASSERT_TRUE(match.Ready(Peer(1), match.GetRoster().version));

  match.Leave(Peer(3));

  EXPECT_TRUE(match.IsReady(Peer(1)));
  EXPECT_FALSE(match.IsReady(Peer(2)));
}

TEST(MatchTest, AReadyFromAPeerThatHasNotJoinedOrDuringAMatchCountsForNothing) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);

  EXPECT_FALSE(match.Ready(Peer(9), match.GetRoster().version));
  ASSERT_TRUE(ReadyAndStart(match).has_value());
  EXPECT_FALSE(match.Ready(Peer(1), match.GetRoster().version));
}

TEST(MatchTest, EndingAMatchReturnsItsPlayersToTheLobbyUnderANewRoster) {
  Match match(Config(2));
  JoinPeers(match, 1, 2);
  ASSERT_TRUE(ReadyAndStart(match).has_value());
  const auto during = match.GetRoster().version;

  const auto ended = match.End();

  EXPECT_EQ(ended, (std::vector<SessionId>{*match.SessionOf(Peer(1)), *match.SessionOf(Peer(2))}));
  EXPECT_FALSE(match.InMatch());
  EXPECT_TRUE(match.Playing().empty());
  const auto roster = match.GetRoster();
  EXPECT_GT(roster.version, during);
  ASSERT_EQ(roster.players.size(), 2U);
  EXPECT_EQ(roster.players[0].session, *match.SessionOf(Peer(1)));
  EXPECT_EQ(roster.players[1].session, *match.SessionOf(Peer(2)));
}

TEST(MatchTest, PlayersKeepTheirSessionAndCharacterAcrossMatches) {
  Match match(MatchConfig{
      .engine_version = kVersion, .characters = {"characters/sniper", "characters/medic"}, .player_count = 1});
  const auto admission = match.Join(Peer(1), Request(kVersion, "characters/medic"));
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  match.End();

  ASSERT_EQ(match.GetRoster().players.size(), 1U);
  EXPECT_EQ(match.GetRoster().players[0].session, admission->session);
  EXPECT_EQ(match.GetRoster().players[0].character, 2U);
  EXPECT_EQ(ReadyAndStart(match)->players[0].session, admission->session);
}

TEST(MatchTest, AfterAMatchNoOneIsReadyUntilTheyReportTheNewRoster) {
  Match match(Config(1));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  match.End();

  EXPECT_FALSE(match.TryStart().has_value());
  EXPECT_TRUE(ReadyAndStart(match).has_value());
}

TEST(MatchTest, EndingWithNoMatchInProgressChangesNothing) {
  Match match(Config(2));
  JoinPeers(match, 1, 1);
  const auto version = match.GetRoster().version;

  EXPECT_TRUE(match.End().empty());

  EXPECT_EQ(match.GetRoster().version, version);
}

TEST(MatchTest, AMatchWhoseLastPlayerLeavesEndsAndTheLobbyOpens) {
  Match match(Config(2));
  JoinPeers(match, 1, 2);
  ASSERT_TRUE(ReadyAndStart(match).has_value());

  EXPECT_EQ(match.Leave(Peer(1)), Departure::kFromMatch);
  EXPECT_EQ(match.Leave(Peer(2)), Departure::kEndedMatch);

  EXPECT_FALSE(match.InMatch());
  EXPECT_TRUE(match.GetRoster().players.empty());
  EXPECT_TRUE(match.Join(Peer(3), Request(kVersion, kCharacter)).has_value());
}

TEST(MatchTest, TheNextMatchStartsOnlyOnceThePauseHasPassed) {
  constexpr std::uint32_t kPause = 300;
  Match match(Config(1, kPause));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());
  match.End();
  ReadyEveryone(match);

  for (std::uint32_t i = 0; i < kPause - 1; ++i) {
    match.Tick();
  }
  EXPECT_FALSE(match.TryStart().has_value()) << "one tick short of the pause";
  match.Tick();
  EXPECT_TRUE(match.TryStart().has_value());
}

TEST(MatchTest, TheFirstMatchWaitsForNoPause) {
  Match match(Config(1, 300));
  JoinPeers(match, 1, 1);

  EXPECT_TRUE(ReadyAndStart(match).has_value());
}

// The pause is counted from the end, not from the last start.
TEST(MatchTest, TicksDuringAMatchDoNotCountTowardsThePauseAfterIt) {
  constexpr std::uint32_t kPause = 10;
  Match match(Config(1, kPause));
  JoinPeers(match, 1, 1);
  ASSERT_TRUE(ReadyAndStart(match).has_value());
  for (std::uint32_t i = 0; i < 2 * kPause; ++i) {
    match.Tick();
  }

  match.End();

  EXPECT_FALSE(ReadyAndStart(match).has_value());
}

TEST(MatchTest, SpawnPointsContinueTheirRotationAcrossMatches) {
  Match match(Config(2), SpawnPoints());
  JoinPeers(match, 1, 2);
  ASSERT_TRUE(ReadyAndStart(match).has_value());
  match.End();

  const auto second = ReadyAndStart(match);

  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->players[0].spawn, Vec3(3.0F, 0.0F, 0.0F));
  EXPECT_EQ(second->players[1].spawn, Vec3(1.0F, 0.0F, 0.0F));
}

}  // namespace
