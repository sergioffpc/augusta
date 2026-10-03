#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/harness.h"
#include "lobby_readiness.h"

// Unit tests for what the client loads in the Lobby before it reports Ready,
// and for which Roster (ADR-0043).
namespace {

using augusta::client::LobbyReadiness;
using augusta::client::ReadyPlan;
using augusta::harness::Admission;
using augusta::harness::Lobby;
using augusta::harness::RosterEntry;
using augusta::harness::ServerView;
using augusta::harness::SessionId;

constexpr SessionId kOwn{1};
constexpr std::uint8_t kTickRateHz = 60;

// The server's admission of this client.
Admission Admitted() {
  return {.session = kOwn, .tick_rate_hz = kTickRateHz, .parameters = {}, .character = "characters/own"};
}

// This client admitted, in the Lobby, with roster numbered version.
ServerView InLobby(std::uint32_t version, std::vector<RosterEntry> roster) {
  ServerView view;
  view.accepted = Admitted();
  view.lobby = Lobby{.version = version, .roster = std::move(roster)};
  return view;
}

TEST(LobbyReadinessTest, PlansTheOtherPlayersCharactersEachOnceInRosterOrder) {
  const LobbyReadiness readiness;

  const std::optional<ReadyPlan> plan = readiness.Plan(InLobby(3, {{.session = SessionId{2}, .character = "c"},
                                                                   {.session = kOwn, .character = "characters/own"},
                                                                   {.session = SessionId{3}, .character = "a"},
                                                                   {.session = SessionId{4}, .character = "c"}}));

  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->version, 3U);
  EXPECT_EQ(plan->characters_to_load, (std::vector<std::string>{"c", "a"}));
}

TEST(LobbyReadinessTest, PlansNoCharacterAlreadyLoaded) {
  LobbyReadiness readiness;
  readiness.MarkLoaded("a");

  const std::optional<ReadyPlan> plan = readiness.Plan(
      InLobby(1, {{.session = SessionId{2}, .character = "a"}, {.session = SessionId{3}, .character = "b"}}));

  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->characters_to_load, (std::vector<std::string>{"b"}));
}

TEST(LobbyReadinessTest, PlansNothingOnceReadyForThatRoster) {
  LobbyReadiness readiness;
  readiness.MarkReady(1);

  EXPECT_FALSE(readiness.Plan(InLobby(1, {{.session = SessionId{2}, .character = "a"}})).has_value());
}

TEST(LobbyReadinessTest, PlansAgainForANewRoster) {
  LobbyReadiness readiness;
  readiness.MarkLoaded("a");
  readiness.MarkReady(1);

  const std::optional<ReadyPlan> plan = readiness.Plan(
      InLobby(2, {{.session = SessionId{2}, .character = "a"}, {.session = SessionId{3}, .character = "b"}}));

  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->version, 2U);
  EXPECT_EQ(plan->characters_to_load, (std::vector<std::string>{"b"}));
}

TEST(LobbyReadinessTest, PlansReadyEvenWithNothingToLoad) {
  const LobbyReadiness readiness;

  const std::optional<ReadyPlan> plan = readiness.Plan(InLobby(5, {{.session = kOwn, .character = "characters/own"}}));

  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->version, 5U);
  EXPECT_TRUE(plan->characters_to_load.empty());
}

TEST(LobbyReadinessTest, PlansNothingOutsideTheLobby) {
  const LobbyReadiness readiness;

  ServerView not_admitted;
  not_admitted.lobby = Lobby{.version = 1, .roster = {}};
  EXPECT_FALSE(readiness.Plan(not_admitted).has_value());

  ServerView in_match = InLobby(1, {{.session = SessionId{2}, .character = "a"}});
  in_match.in_match = true;
  EXPECT_FALSE(readiness.Plan(in_match).has_value());

  ServerView no_roster;
  no_roster.accepted = Admitted();
  EXPECT_FALSE(readiness.Plan(no_roster).has_value());
}

}  // namespace
