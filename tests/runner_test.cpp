#include "augusta/runner.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
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
constexpr const char* kCharacter = "soldier";
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

// The value of the failure's context under key, or empty if it has none.
std::string ContextOf(const augusta::failure::Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
      return field.value;
    }
  }
  return {};
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
      : host_(HostConfig{.tick_rate_hz = kTickRate,
                         .parameters = {},
                         .listen = Endpoint{.address = "127.0.0.1:0"},
                         .server_pack = {},
                         .capture_directory = {},
                         .capture_mode = {},
                         .faults = nullptr},
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

  const augusta::failure::Failure failure = *runner.Failure();
  EXPECT_EQ(failure.code, augusta::failure::Code::kWorkerFailed);
  EXPECT_EQ(ContextOf(failure, augusta::supervisor::kThreadContextKey), "prediction");
  EXPECT_EQ(failure.detail, "no input");
}

// Once the Network I/O thread fails, the Prediction thread finishes at most
// the Tick it is in: a Tick that already sees the failure, and so the stop
// requested before it, is the last one. Nothing ticks once the Runner is gone.
TEST_F(RunnerTest, ANetworkThreadFailureStopsThePredictionTicksAndBothThreadsAreJoined) {
  constexpr auto kSeveralTicks = std::chrono::milliseconds(200);
  std::atomic<const Runner*> running{nullptr};
  std::atomic<int> ticks = 0;
  std::atomic<int> ticks_seeing_the_failure = 0;
  std::atomic<bool> fail_next_round = false;
  {
    const Runner runner(session_, RunnerHooks{.next_command = [] { return Command{}; },
                                              .on_tick =
                                                  [&](const PredictedTick&) {
                                                    ++ticks;
                                                    const Runner* const current = running.load();
                                                    if (current != nullptr && current->Failure().has_value()) {
                                                      ++ticks_seeing_the_failure;
                                                    }
                                                  },
                                              .on_network_round =
                                                  [&] {
                                                    if (fail_next_round) {
                                                      throw std::runtime_error("link down");
                                                    }
                                                  }});
    running = &runner;
    ASSERT_TRUE(ServeUntil([&] { return ticks >= kTickRate / 4; }));

    fail_next_round = true;
    ASSERT_TRUE(ServeUntil([&] { return runner.Failure().has_value(); }));
    // Long enough for several more Ticks, had the stop not ended them.
    std::this_thread::sleep_for(kSeveralTicks);

    const augusta::failure::Failure failure = *runner.Failure();
    EXPECT_EQ(failure.code, augusta::failure::Code::kWorkerFailed);
    EXPECT_EQ(ContextOf(failure, augusta::supervisor::kThreadContextKey), "network");
    EXPECT_EQ(failure.detail, "link down");
  }
  const int ticks_when_joined = ticks;
  std::this_thread::sleep_for(kSeveralTicks);

  EXPECT_LE(ticks_seeing_the_failure.load(), 1);
  EXPECT_EQ(ticks.load(), ticks_when_joined);
}

// A Join request naming a character longer than the protocol carries is never
// sent, in any build: the Network I/O thread stops on a broken invariant
// (ADR-0033) instead, and the server admits no one.
TEST_F(RunnerTest, AJoinRequestTheProtocolCannotCarryStopsTheRunnerWithAnInvariantFailureOfTheNetworkThread) {
  Session session(SessionConfig{.server = host_.ListenEndpoint(),
                                .character = std::string(augusta::protocol::kMaxCharacterNameLength + 1, 'c')},
                  WorldWithFloor());
  const Runner runner(session,
                      RunnerHooks{.next_command = [] { return Command{}; }, .on_tick = {}, .on_network_round = {}});

  ASSERT_TRUE(ServeUntil([&] { return runner.Failure().has_value(); }));

  const augusta::failure::Failure failure = *runner.Failure();
  EXPECT_EQ(failure.code, augusta::failure::Code::kInvariantViolated);
  EXPECT_EQ(ContextOf(failure, augusta::supervisor::kThreadContextKey), "network");
  EXPECT_FALSE(session.GetSessionId().has_value());
  // The Runner took it: it is reported once.
  EXPECT_FALSE(session.TakeInvariantFailure().has_value());
}

}  // namespace
