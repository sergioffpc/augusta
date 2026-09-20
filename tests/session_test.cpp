#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "augusta/client_session.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "host.h"

// The seam the M3 tickets test through (issue #73): a real server host and a
// real client session, both without a window, a GPU or a wall-clock loop, in
// one process over loopback. The tests drive both sides' network work and
// their ticks by hand.
namespace {

using augusta::client_session::Session;
using augusta::client_session::SessionConfig;
using augusta::input::Command;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::physics::StaticMesh;
using augusta::server::Host;
using augusta::server::HostConfig;

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);
constexpr float kFixedTick = 1.0F / 60.0F;

// ctest runs every test case in its own process, possibly in parallel, so a
// fixed port would collide; derive one from the process id instead.
std::string LoopbackAddress() {
#ifdef _WIN32
  const int pid = _getpid();
#else
  const int pid = getpid();
#endif
  constexpr int kFirstPort = 27100;
  constexpr int kPortSpan = 800;
  return "127.0.0.1:" + std::to_string(kFirstPort + (pid % kPortSpan));
}

// Init and Shutdown once for the whole process, as in networking_test.cpp.
class SessionEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kSessionEnvironment =
    ::testing::AddGlobalTestEnvironment(new SessionEnvironment);

class SessionTest : public ::testing::Test {
 protected:
  // The script path is the server's own placeholder (scripting::Engine ignores
  // it until Lua is embedded, ADR-0022); point it at a real script then.
  SessionTest()
      : host_(HostConfig{.script_path = "scripts/round.lua", .listen = Endpoint{.address = LoopbackAddress()}}),
        session_(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}) {}

  // Runs both sides' network work until the session reports connected, or
  // the deadline passes.
  bool ConnectSession() {
    session_.Connect();
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork();
      session_.PumpEvents();
      session_.ExchangeMessages();
      if (session_.GetState() == ConnectionState::kConnected) {
        return true;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  Host host_;
  Session session_;
};

TEST_F(SessionTest, ConnectsToAHostDrivenByHand) { EXPECT_TRUE(ConnectSession()); }

TEST_F(SessionTest, PredictionMovesOnlyByTheCommandEachTickIsGiven) {
  ASSERT_TRUE(ConnectSession());
  Command walk{};
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);

  const auto start = session_.Tick(Command{}, kFixedTick);
  augusta::prediction::State walked;
  for (int i = 0; i < 30; ++i) {
    host_.Tick(kFixedTick);
    walked = session_.Tick(walk, kFixedTick);
  }
  const auto idle = session_.Tick(Command{}, kFixedTick);

  EXPECT_GT(walked.local_body.position.x, start.local_body.position.x);
  EXPECT_NEAR(idle.local_body.position.x, walked.local_body.position.x, 0.05F);
}

TEST_F(SessionTest, StaysConnectedWhileTheTestAlternatesTicksAndNetworkWork) {
  ASSERT_TRUE(ConnectSession());

  for (int i = 0; i < 10; ++i) {
    host_.Tick(kFixedTick);
    session_.Tick(Command{}, kFixedTick);
    host_.PumpNetwork();
    session_.PumpEvents();
    session_.ExchangeMessages();
  }

  EXPECT_EQ(session_.GetState(), ConnectionState::kConnected);
}

// A large horizontal slab at height y, its triangles facing up.
StaticMesh FloorAt(float y) {
  constexpr float kExtent = 100.0F;
  return StaticMesh{.points = {Vec3(-kExtent, y, -kExtent), Vec3(-kExtent, y, kExtent), Vec3(kExtent, y, kExtent),
                               Vec3(kExtent, y, -kExtent)},
                    .indices = {0, 1, 2, 0, 2, 3}};
}

// The client spawns its predicted body at the origin, so a floor two meters
// below it is where that body must come to rest.
constexpr float kFloorHeight = -2.0F;
constexpr int kFallTicks = 120;

TEST(LevelSessionTest, ThePredictedBodyRestsOnTheLevelsFloor) {
  const Endpoint address{.address = LoopbackAddress()};
  Session session(SessionConfig{.server = address, .level = {FloorAt(kFloorHeight)}});

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }

  EXPECT_NEAR(state.local_body.position.y, kFloorHeight, 0.2F);
}

TEST(LevelSessionTest, WithoutALevelThePredictedBodyKeepsFalling) {
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}});

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }

  EXPECT_LT(state.local_body.position.y, kFloorHeight - 5.0F);
}

TEST(LevelSessionTest, ASessionRefusesALevelMeshPhysicsRejects) {
  EXPECT_THROW(Session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}, .level = {StaticMesh{}}}),
               std::runtime_error);
}

TEST(LevelHostTest, AHostAcceptsALevelAndKeepsTicking) {
  Host host(HostConfig{
      .script_path = "scripts/round.lua", .listen = Endpoint{.address = LoopbackAddress()}, .level = {FloorAt(0.0F)}});

  for (int i = 0; i < 10; ++i) {
    host.Tick(kFixedTick);
  }
  SUCCEED();
}

TEST(LevelHostTest, AHostRefusesALevelMeshPhysicsRejects) {
  EXPECT_THROW(Host(HostConfig{.script_path = "scripts/round.lua",
                               .listen = Endpoint{.address = LoopbackAddress()},
                               .level = {StaticMesh{}}}),
               std::runtime_error);
}

}  // namespace
