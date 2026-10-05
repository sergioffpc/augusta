#include "run.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/map.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "content.h"
#include "host.h"

// How a run of Scripted players is judged, and whole runs against a real
// server::Host over loopback in the same process.
namespace {

using augusta::harness::ServerView;
using augusta::loadtest::Judge;
using augusta::loadtest::MatchesEnded;
using augusta::loadtest::PlayerProgress;
using augusta::loadtest::RunConfig;
using augusta::loadtest::RunScriptedPlayers;
using augusta::loadtest::Verdict;

constexpr std::uint8_t kTickRate = 60;
constexpr const char* kCharacter = "characters/player";

class RunEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kRunEnvironment =
    ::testing::AddGlobalTestEnvironment(new RunEnvironment);

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

// The example scenario's packs, verified with the test key, as augustad and
// augusta-loadtest load them.
augusta::assets::Pack ExamplePack(std::string_view name) {
  const std::filesystem::path root(AUGUSTA_EXAMPLE_PACKS);
  auto pack = augusta::assets::LoadVerifiedPack(root / name, root / "test.pub");
  EXPECT_TRUE(pack.has_value());
  return *std::move(pack);
}

// A Host run at the tick rate on a thread of its own, as augustad's would be,
// until it is destroyed.
class RunningHost {
 public:
  RunningHost(const augusta::server::HostConfig& config, augusta::server::Content content)
      : host_(config, std::move(content.scenario), std::move(content.policy)),
        thread_([this](const std::stop_token& stop) { Serve(stop); }) {}

  [[nodiscard]] augusta::networking::Endpoint ListenEndpoint() const { return host_.ListenEndpoint(); }

 private:
  void Serve(const std::stop_token& stop) {
    constexpr float kFixedTick = 1.0F / kTickRate;
    const auto duration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(kFixedTick));
    auto next_tick = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
      host_.PumpNetwork(std::chrono::steady_clock::now());
      static_cast<void>(host_.Tick(kFixedTick));
      next_tick += duration;
      std::this_thread::sleep_until(next_tick);
    }
  }

  augusta::server::Host host_;
  // Last, so it is joined before the Host goes.
  std::jthread thread_;
};

TEST(RunScriptedPlayersTest, ThePlayerCountOfScriptedPlayersPlaysTheExampleForTwoThroughAMatchEnd) {
  const augusta::assets::Pack server_pack = ExamplePack("server.pack");
  auto content = augusta::server::LoadServerContent(server_pack, kTickRate);
  ASSERT_TRUE(content.has_value()) << augusta::server::DescribeContentError(content.error());
  // The example lets one player run it alone, and a Match of one never ends
  // by last player standing: the server runs it for two, as a scenario cooked
  // for a duel would, and tells each Scripted player so when it joins.
  augusta::parameters::Parameters parameters = content->parameters;
  parameters.player_count = 2;
  const RunningHost host(
      augusta::server::HostConfig{
          .tick_rate_hz = kTickRate, .parameters = parameters, .listen = {.address = "127.0.0.1:0"}},
      *std::move(content));

  const augusta::assets::Pack client_pack = ExamplePack("client.pack");
  auto map = augusta::map::LoadCollision(client_pack);
  ASSERT_TRUE(map.has_value()) << augusta::map::DescribeMapError(map.error());
  const RunConfig config{
      .session = {.server = host.ListenEndpoint(), .client_pack = client_pack.Hash(), .character = kCharacter},
      .map = *std::move(map),
      .matches = 1,
      .timeout = std::chrono::seconds(90),
      .seed = 1,
  };

  EXPECT_EQ(RunScriptedPlayers(config), Verdict::kSucceeded);
}

TEST(RunScriptedPlayersTest, AServerThatNeverAdmitsAnyoneTimesTheRunOut) {
  // Listening, but never pumped, so no one is ever admitted.
  const augusta::networking::Server silent(augusta::networking::Endpoint{.address = "127.0.0.1:0"});
  const RunConfig config{
      .session = {.server = silent.LocalEndpoint(), .character = kCharacter},
      .map = {},
      .matches = 1,
      .timeout = std::chrono::milliseconds(300),
      .seed = 1,
  };

  EXPECT_EQ(RunScriptedPlayers(config), Verdict::kTimedOut);
}

}  // namespace
