#include <chrono>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "augusta/agent.h"
#include "augusta/agent_report.h"
#include "augusta/assets.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/uuid.h"
#include "example_run.h"
#include "match_loop.h"
#include "testing_server.h"

// Whole runs of Agents, composing Intents as load_test.py does, against a
// host::Host over loopback in the same process.
namespace {

using augusta::agent::Agent;
using augusta::agent::LoadedPack;
using augusta::agent::Readiness;
using augusta::agent::testing::ExampleClientPack;
using augusta::agent::testing::ExamplePacks;
using augusta::agent::testing::kCharacter;
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

// Every Agent is a Run of its own (ADR-0038): its join carries a Run ID no
// other Agent's does, which the server logs at admission.
TEST(AgentJoinTest, EachAgentJoinsWithARunIdOfItsOwn) {
  augusta::logging::Init();
  const Server server((ExamplePacks() / "server").replace_extension(augusta::assets::kPackExtension),
                      ExamplePacks() / "test.pub", 2);
  const LoadedPack pack = ExampleClientPack();

  testing::internal::CaptureStdout();
  {
    const auto ignore = [](const augusta::agent::Report& /*report*/) {};
    const Agent first(server.Address(), pack, kCharacter, Readiness::kWhenTold, ignore);
    const Agent second(server.Address(), pack, kCharacter, Readiness::kWhenTold, ignore);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while ((!first.View().server->accepted.has_value() || !second.View().server->accepted.has_value()) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  const std::string log = testing::internal::GetCapturedStdout();

  const std::regex admission("event=lobby_joined peer=[0-9]+ run=([0-9a-f-]{36}) ");
  std::set<std::string> runs;
  for (auto it = std::sregex_iterator(log.begin(), log.end(), admission); it != std::sregex_iterator(); ++it) {
    runs.insert((*it)[1].str());
  }
  EXPECT_EQ(runs.size(), 2U) << log;
  EXPECT_FALSE(runs.contains(augusta::uuid::Uuid{}.ToString())) << log;
}

}  // namespace
