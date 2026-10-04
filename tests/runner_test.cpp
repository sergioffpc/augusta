#include "augusta/runner.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/supervisor.h"
#include "augusta/tick.h"
#include "content.h"
#include "host.h"

// A harness::Runner running a real client session in real time against a real
// server host, in one process over loopback: the test runs the host on its
// own thread's schedule, and the Runner does everything else.
namespace {

using augusta::command::Command;
using augusta::harness::Phase;
using augusta::harness::PredictedTick;
using augusta::harness::Runner;
using augusta::harness::RunnerHooks;
using augusta::harness::ServerView;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::math::Vec3;
using augusta::networking::Endpoint;
using augusta::physics::CollisionMesh;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::Scenario;

constexpr std::uint8_t kTickRate = 60;
constexpr float kFixedTick = 1.0F / kTickRate;
constexpr auto kServeDeadline = std::chrono::seconds(10);
constexpr const char* kCharacter = "characters/player";
// Every player spawns at the origin, so the ground is a little below it.
constexpr float kGroundHeight = -0.5F;

class RunnerEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kRunnerEnvironment =
    ::testing::AddGlobalTestEnvironment(new RunnerEnvironment);

// A large horizontal slab at height y, its triangles facing up.
CollisionMesh FloorAt(float y) {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, y, -kExtent), Vec3(-kExtent, y, kExtent), Vec3(kExtent, y, kExtent),
                                  Vec3(kExtent, y, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

augusta::prediction::World WorldWithFloor() {
  augusta::prediction::World world;
  EXPECT_TRUE(world.AddCollisionMesh(FloorAt(kGroundHeight)).has_value());
  return world;
}

Command Walking() {
  Command command;
  command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  return command;
}

// Where the newest Authoritative State in view puts this client's player, if it names it.
std::optional<Vec3> OwnPosition(const ServerView& view) {
  const auto entity = view.OwnEntity();
  if (!view.authoritative.has_value() || !entity.has_value()) {
    return std::nullopt;
  }
  for (const auto& body : view.authoritative->bodies) {
    if (body.entity == *entity) {
      return body.body.position;
    }
  }
  return std::nullopt;
}

class RunnerTest : public ::testing::Test {
 protected:
  RunnerTest()
      : host_(HostConfig{.tick_rate_hz = kTickRate, .parameters = {}, .listen = Endpoint{.address = "127.0.0.1:0"}},
              Scenario{.collision = {FloorAt(kGroundHeight)},
                       .spawn_points = {},
                       .characters = {{.path = kCharacter, .hitboxes = {}}}}),
        session_(SessionConfig{.server = host_.ListenEndpoint(), .character = kCharacter}, WorldWithFloor()) {}

  // Runs the host at the tick rate - its network work, then a Tick - and has
  // the session report Ready for every Roster it is sent, as a client that has
  // loaded everyone does, until until() holds or the deadline passes. Returns
  // whether it held.
  template <typename Condition>
  bool ServeUntil(Condition until) {
    const auto deadline = std::chrono::steady_clock::now() + kServeDeadline;
    auto next_tick = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork(std::chrono::steady_clock::now());
      const auto view = session_.GetServerView();
      if (view->GetPhase() == Phase::kLobby && view->lobby.has_value()) {
        session_.ReportReady(view->lobby->version);
      }
      if (until()) {
        return true;
      }
      host_.Tick(kFixedTick);
      next_tick +=
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(kFixedTick));
      std::this_thread::sleep_until(next_tick);
    }
    return false;
  }

  Host host_;
  Session session_;
};

TEST_F(RunnerTest, MovesThePlayerOnTheServerByTheCommandsItsSourceGives) {
  const Runner runner(session_, RunnerHooks{.next_command = Walking, .on_tick = {}, .on_network_round = {}});
  ASSERT_TRUE(ServeUntil([&] { return OwnPosition(*session_.GetServerView()).has_value(); }));
  const float start = OwnPosition(*session_.GetServerView())->x;

  EXPECT_TRUE(ServeUntil([&] { return OwnPosition(*session_.GetServerView())->x > start + 1.0F; }));
  EXPECT_FALSE(runner.Failure().has_value());
}

TEST_F(RunnerTest, TellsItsHooksOfEveryTickAtTheServersPacedRateAndOfEveryNetworkRound) {
  std::mutex mutex;
  std::vector<PredictedTick> ticks;
  std::atomic<int> network_rounds = 0;
  const Runner runner(session_, RunnerHooks{.next_command = [] { return Command{}; },
                                            .on_tick =
                                                [&](const PredictedTick& tick) {
                                                  const std::lock_guard<std::mutex> lock(mutex);
                                                  ticks.push_back(tick);
                                                },
                                            .on_network_round = [&] { ++network_rounds; }});

  ASSERT_TRUE(ServeUntil([&] {
    const std::lock_guard<std::mutex> lock(mutex);
    return ticks.size() >= kTickRate;
  }));

  EXPECT_GT(network_rounds.load(), 0);
  const std::lock_guard<std::mutex> lock(mutex);
  const auto nominal = std::chrono::duration<float>(kFixedTick);
  for (std::size_t i = 1; i < ticks.size(); ++i) {
    // Each tick is due when the one before it said the next would be.
    EXPECT_EQ(ticks[i].due, ticks[i - 1].due + ticks[i - 1].duration);
    const auto duration = std::chrono::duration<float>(ticks[i].duration);
    EXPECT_NEAR(duration.count(), nominal.count(), nominal.count() * augusta::tick::kMaxPacing * 1.01F);
  }
}

TEST_F(RunnerTest, ACommandSourceThatThrowsStopsTheRunnerWithAFailureOfThePredictionThread) {
  const Runner runner(session_, RunnerHooks{.next_command = []() -> Command { throw std::runtime_error("no input"); },
                                            .on_tick = {},
                                            .on_network_round = {}});

  ASSERT_TRUE(ServeUntil([&] { return runner.Failure().has_value(); }));

  const augusta::supervisor::WorkerFailure failure = *runner.Failure();
  EXPECT_EQ(failure.thread, "prediction");
  EXPECT_EQ(failure.reason, "no input");
}

}  // namespace
