#include "run.h"

#include <array>
#include <chrono>

#include <gtest/gtest.h>

#include "augusta/harness.h"
#include "augusta/networking.h"
#include "example_server.h"

// How a run of Scripted players is judged, and whole runs against a real
// server::Host over loopback in the same process.
namespace {

using augusta::harness::ServerView;
using augusta::swarm::Judge;
using augusta::swarm::MatchesEnded;
using augusta::swarm::PlayerProgress;
using augusta::swarm::RunConfig;
using augusta::swarm::RunScriptedPlayers;
using augusta::swarm::Verdict;
using augusta::swarm::testing::ExampleServer;

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new augusta::swarm::testing::NetworkingEnvironment);

TEST(MatchesEndedTest, CountsEveryMatchStartedButTheOneInProgress) {
  ServerView view;
  EXPECT_EQ(MatchesEnded(view), 0U);

  view.matches_started = 1;
  view.in_match = true;
  EXPECT_EQ(MatchesEnded(view), 0U);

  view.in_match = false;
  EXPECT_EQ(MatchesEnded(view), 1U);

  view.matches_started = 2;
  view.in_match = true;
  EXPECT_EQ(MatchesEnded(view), 1U);
}

TEST(JudgeTest, KeepsRunningUntilEveryPlayerHasSeenEveryMatchEnd) {
  const std::array players{PlayerProgress{.matches_ended = 2, .failed = false},
                           PlayerProgress{.matches_ended = 1, .failed = false}};

  EXPECT_EQ(Judge(players, 2, 2, false), Verdict::kRunning);
  EXPECT_EQ(Judge(players, 2, 1, false), Verdict::kSucceeded);
}

TEST(JudgeTest, KeepsRunningUntilThePlayerCountIsConnected) {
  const std::array players{PlayerProgress{.matches_ended = 1, .failed = false}};

  EXPECT_EQ(Judge(players, 0, 1, false), Verdict::kRunning);
  EXPECT_EQ(Judge(players, 2, 1, false), Verdict::kRunning);
  EXPECT_EQ(Judge(players, 1, 1, false), Verdict::kSucceeded);
}

TEST(JudgeTest, AnyPlayerFailingFailsTheRun) {
  const std::array players{PlayerProgress{.matches_ended = 0, .failed = false},
                           PlayerProgress{.matches_ended = 0, .failed = true}};

  EXPECT_EQ(Judge(players, 2, 1, false), Verdict::kFailed);
  EXPECT_EQ(Judge(players, 2, 1, true), Verdict::kFailed);
}

TEST(JudgeTest, TheTimeoutPassingTimesTheRunOut) {
  const std::array players{PlayerProgress{.matches_ended = 0, .failed = false}};

  EXPECT_EQ(Judge(players, 1, 1, true), Verdict::kTimedOut);
}

TEST(JudgeTest, SuccessWinsOverAFailureOrTimeoutThatComesWithIt) {
  const std::array players{PlayerProgress{.matches_ended = 1, .failed = true},
                           PlayerProgress{.matches_ended = 1, .failed = false}};

  EXPECT_EQ(Judge(players, 2, 1, true), Verdict::kSucceeded);
}

TEST(RunScriptedPlayersTest, ThePlayerCountOfScriptedPlayersPlaysTheExampleForTwoThroughAMatchEnd) {
  const ExampleServer server(2);

  EXPECT_EQ(RunScriptedPlayers(server.RunOf(1, std::chrono::seconds(90))).verdict, Verdict::kSucceeded);
}

TEST(RunScriptedPlayersTest, AServerThatNeverAdmitsAnyoneTimesTheRunOut) {
  // Listening, but never pumped, so no one is ever admitted.
  const augusta::networking::Server silent(augusta::networking::Endpoint{.address = "127.0.0.1:0"});
  const RunConfig config{
      .session = {.server = silent.LocalEndpoint(), .character = augusta::swarm::testing::kCharacter},
      .map = {},
      .matches = 1,
      .timeout = std::chrono::milliseconds(300),
      .seed = 1,
  };

  EXPECT_EQ(RunScriptedPlayers(config).verdict, Verdict::kTimedOut);
}

}  // namespace
