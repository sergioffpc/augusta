#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "augusta/agent.h"
#include "augusta/assets.h"
#include "augusta/link.h"
#include "example_run.h"
#include "match_loop.h"
#include "testing_server.h"

// Whole runs of Agents, composing Intents as load_test.py does, against a
// host::Host over loopback in the same process.
namespace {

using augusta::agent::LoadedPack;
using augusta::agent::testing::ExampleClientPack;
using augusta::agent::testing::ExamplePacks;
using augusta::agent::testing::MatchLoopResult;
using augusta::agent::testing::PlayMatches;
using augusta::agent::testing::RunAgainst;
using augusta::agent::testing::Server;
using augusta::agent::testing::Verdict;

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new augusta::agent::testing::NetworkingEnvironment);

// Requirements: US-23
TEST(PlayMatchesTest, ThePlayerCountOfAgentsPlaysTheExampleForTwoThroughTwoMatchEnds) {
  const Server server((ExamplePacks() / "server").replace_extension(augusta::assets::kPackExtension),
                      ExamplePacks() / "test.pub", 2);
  const LoadedPack pack = ExampleClientPack();

  const MatchLoopResult result = PlayMatches(RunAgainst(server.Address(), pack, 2, std::chrono::seconds(90)));

  ASSERT_EQ(result.verdict, Verdict::kSucceeded) << result.failure;
  ASSERT_EQ(result.agents.size(), 2U);
  for (const auto& agent : result.agents) {
    EXPECT_GT(agent.match_ticks, 0U);
  }
}

TEST(PlayMatchesTest, AServerThatNeverAdmitsAnyoneTimesTheRunOut) {
  // Listening, but never pumped, so no one is ever admitted.
  const augusta::link::Server silent(augusta::link::Endpoint{.address = "127.0.0.1:0"});
  const LoadedPack pack = ExampleClientPack();

  const MatchLoopResult result =
      PlayMatches(RunAgainst(silent.LocalEndpoint().address, pack, 1, std::chrono::milliseconds(300)));

  EXPECT_EQ(result.verdict, Verdict::kTimedOut);
}

// Requirements: US-23
TEST(PlayMatchesTest, ARefusedAgentFailsTheRunWithTheRefusal) {
  const Server server((ExamplePacks() / "server").replace_extension(augusta::assets::kPackExtension),
                      ExamplePacks() / "test.pub", 2);
  const LoadedPack pack = ExampleClientPack();
  auto config = RunAgainst(server.Address(), pack, 1, std::chrono::seconds(30));
  config.character = "nobody";

  const MatchLoopResult result = PlayMatches(config);

  EXPECT_EQ(result.verdict, Verdict::kFailed);
  EXPECT_NE(result.failure.find("no such character"), std::string::npos) << result.failure;
}

}  // namespace
