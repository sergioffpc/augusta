#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/version.h"
#include "host.h"
#include "match.h"

// The seam the M3 tickets test through (issue #73): a real server host and a
// real client session, both without a window, a GPU or a wall-clock loop, in
// one process over loopback. The tests drive both sides' network work and
// their ticks by hand.
namespace {

using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::input::Command;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::physics::Stance;
using augusta::physics::StaticMesh;
using augusta::protocol::JoinRefusal;
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

// A host and however many clients a test starts, all driven by hand.
class JoinTest : public ::testing::Test {
 protected:
  JoinTest()
      : host_(HostConfig{.script_path = "scripts/round.lua", .listen = Endpoint{.address = LoopbackAddress()}}) {}

  // Starts connecting a new client that presents engine_version.
  Session& AddClient(const std::string& engine_version = std::string(augusta::EngineVersion())) {
    sessions_.push_back(std::make_unique<Session>(
        SessionConfig{.server = Endpoint{.address = LoopbackAddress()}, .engine_version = engine_version}));
    sessions_.back()->Connect();
    return *sessions_.back();
  }

  // Runs both sides' network work until every client has been answered, or the
  // deadline passes.
  bool WaitForAnswers() {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork();
      bool all_answered = true;
      for (const auto& session : sessions_) {
        session->PumpEvents();
        session->ExchangeMessages();
        all_answered = all_answered && (session->GetSessionId().has_value() || session->GetRefusal().has_value());
      }
      if (all_answered) {
        return true;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
};

TEST_F(JoinTest, AClientWithTheMatchingVersionIsAdmittedWithASessionId) {
  Session& client = AddClient();

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_TRUE(client.GetSessionId().has_value());
  EXPECT_FALSE(client.GetRefusal().has_value());
}

TEST_F(JoinTest, SessionIdsAreUniqueAmongConnectedClients) {
  constexpr int kClients = 3;
  for (int i = 0; i < kClients; ++i) {
    AddClient();
  }

  ASSERT_TRUE(WaitForAnswers());

  std::set<augusta::protocol::SessionId> ids;
  for (const auto& session : sessions_) {
    ASSERT_TRUE(session->GetSessionId().has_value());
    ids.insert(*session->GetSessionId());
  }
  EXPECT_EQ(ids.size(), static_cast<std::size_t>(kClients));
}

TEST_F(JoinTest, AClientWithAnotherEngineVersionIsRefusedForTheVersion) {
  Session& client = AddClient("0.0.0-not-the-servers");

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kVersionMismatch);
  EXPECT_FALSE(client.GetSessionId().has_value());
}

TEST_F(JoinTest, TheNinthClientIsRefusedBecauseTheMatchIsFull) {
  for (std::size_t i = 0; i < augusta::protocol::kMaxPlayers; ++i) {
    AddClient();
  }
  ASSERT_TRUE(WaitForAnswers());
  for (const auto& session : sessions_) {
    ASSERT_TRUE(session->GetSessionId().has_value());
  }

  Session& ninth = AddClient();
  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(ninth.GetRefusal(), JoinRefusal::kMatchFull);
  EXPECT_FALSE(ninth.GetSessionId().has_value());
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

TEST(MapSessionTest, ThePredictedBodyRestsOnTheMapsFloor) {
  const Endpoint address{.address = LoopbackAddress()};
  Session session(SessionConfig{.server = address, .collision = {FloorAt(kFloorHeight)}});

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }

  EXPECT_NEAR(state.local_body.position.y, kFloorHeight, 0.2F);
}

TEST(MapSessionTest, WithoutAMapThePredictedBodyKeepsFalling) {
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}});

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }

  EXPECT_LT(state.local_body.position.y, kFloorHeight - 5.0F);
}

TEST(MapSessionTest, ASessionRefusesAMapMeshPhysicsRejects) {
  EXPECT_THROW(Session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}, .collision = {StaticMesh{}}}),
               std::runtime_error);
}

TEST(MapHostTest, AHostAcceptsAMapAndKeepsTicking) {
  Host host(HostConfig{.script_path = "scripts/round.lua",
                       .listen = Endpoint{.address = LoopbackAddress()},
                       .collision = {FloorAt(0.0F)}});

  for (int i = 0; i < 10; ++i) {
    host.Tick(kFixedTick);
  }
  SUCCEED();
}

TEST(MapHostTest, AHostRefusesAMapMeshPhysicsRejects) {
  EXPECT_THROW(Host(HostConfig{.script_path = "scripts/round.lua",
                               .listen = Endpoint{.address = LoopbackAddress()},
                               .collision = {StaticMesh{}}}),
               std::runtime_error);
}

// A joined client and a host with flat ground, driven tick by tick.
class MovementTest : public ::testing::Test {
 protected:
  static constexpr auto kNetworkDelay = std::chrono::milliseconds(8);
  // The ticks SetUp runs, each with one command sent.
  static constexpr int kSettleTicks = 30;

  MovementTest()
      : host_(HostConfig{.script_path = "scripts/round.lua",
                         .listen = Endpoint{.address = LoopbackAddress()},
                         .collision = {FloorAt(0.0F)}}),
        session_(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}) {}

  void SetUp() override {
    session_.Connect();
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (!session_.GetSessionId().has_value() && std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork();
      session_.PumpEvents();
      session_.ExchangeMessages();
      std::this_thread::sleep_for(kPollInterval);
    }
    ASSERT_TRUE(session_.GetSessionId().has_value());
    // The first server tick puts the new player in the world; let it settle on the floor.
    for (int i = 0; i < kSettleTicks; ++i) {
      Step(Command{});
    }
  }

  // The server takes in what has arrived and ticks; the client takes in the state it gets back.
  void ServerTickAndDeliver() {
    std::this_thread::sleep_for(kNetworkDelay);
    host_.PumpNetwork();
    host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    session_.PumpEvents();
    session_.ExchangeMessages();
  }

  // One tick of the whole match: the client predicts and sends command, then the server ticks.
  void Step(const Command& command) {
    session_.Tick(command, kFixedTick);
    ServerTickAndDeliver();
  }

  [[nodiscard]] augusta::physics::BodyState Self() const {
    const auto state = session_.GetAuthoritativeState();
    EXPECT_TRUE(state.has_value());
    if (state.has_value()) {
      for (const auto& player : state->players) {
        if (player.session == *session_.GetSessionId()) {
          return player.body;
        }
      }
    }
    ADD_FAILURE() << "this client's player is not in the state";
    return {};
  }

  static Command Walking(Stance stance = Stance::kStanding) {
    Command command;
    command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    command.movement.desired_stance = stance;
    return command;
  }

  static float HorizontalSpeed(const augusta::physics::BodyState& body) {
    return std::hypot(body.velocity.x, body.velocity.z);
  }

  Host host_;
  Session session_;
};

TEST_F(MovementTest, AForwardCommandMovesTheAuthoritativePlayerAndTheStateReachesTheClient) {
  const float start = Self().position.x;

  for (int i = 0; i < 60; ++i) {
    Step(Walking());
  }

  EXPECT_GT(Self().position.x, start + 2.0F);
}

TEST_F(MovementTest, TheServerAcknowledgesTheCommandsItHasProcessed) {
  constexpr int kSteps = 20;
  for (int i = 0; i < kSteps; ++i) {
    Step(Walking());
  }

  const auto state = session_.GetAuthoritativeState();
  ASSERT_TRUE(state.has_value());
  EXPECT_GE(state->acknowledged_sequence, kSettleTicks + kSteps - 2U);
  EXPECT_LE(state->acknowledged_sequence, kSettleTicks + kSteps);
}

TEST_F(MovementTest, StanceCommandsChangeTheAuthoritativeStanceAndSpeed) {
  const auto walk_at = [&](Stance stance) {
    for (int i = 0; i < 20; ++i) {
      Step(Walking(stance));
    }
    EXPECT_EQ(Self().stance, stance);
    return HorizontalSpeed(Self());
  };

  const float standing = walk_at(Stance::kStanding);
  const float crouching = walk_at(Stance::kCrouching);
  const float prone = walk_at(Stance::kProne);

  EXPECT_GT(standing, crouching + 0.3F);
  EXPECT_GT(crouching, prone + 0.3F);
}

TEST_F(MovementTest, ALostDatagramDoesNotLoseAMovementCommand) {
  constexpr int kDeliveredSteps = 2;
  constexpr int kLostCommands = 3;
  for (int i = 0; i < kDeliveredSteps; ++i) {
    Step(Walking());
  }

  std::uint32_t previous = session_.GetAuthoritativeState()->acknowledged_sequence;

  // Commands the server never hears, then one that arrives together with them.
  augusta::networking::SimulateNetworkConditions({.loss_percent = 100.0F});
  for (int i = 0; i < kLostCommands; ++i) {
    session_.Tick(Walking(), kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
  }
  augusta::networking::SimulateNetworkConditions({});
  session_.Tick(Walking(), kFixedTick);
  const std::uint32_t last_sent = kSettleTicks + kDeliveredSteps + kLostCommands + 1;

  // The server consumes one command per tick, so every sequence passes through
  // the acknowledgement in turn; a lost one would make it skip.
  std::uint32_t acknowledged = 0;
  for (int i = 0; i < 12; ++i) {
    ServerTickAndDeliver();
    acknowledged = session_.GetAuthoritativeState()->acknowledged_sequence;
    EXPECT_LE(acknowledged, previous + 1) << "the server skipped a command";
    previous = acknowledged;
  }

  EXPECT_EQ(acknowledged, last_sent);
}

// A client speaking the protocol by hand, to send what a real one would not.
class RawClient {
 public:
  explicit RawClient(const Endpoint& server) { client_.Connect(server); }

  ~RawClient() { client_.Disconnect(); }

  // Runs host and client until the server has admitted this one.
  bool Join(Host& host) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    bool requested = false;
    while (std::chrono::steady_clock::now() < deadline) {
      client_.PumpEvents();
      host.PumpNetwork();
      if (!requested && client_.GetState() == ConnectionState::kConnected) {
        Send(augusta::protocol::JoinRequest{.engine_version = std::string(augusta::EngineVersion())});
        requested = true;
      }
      for (const auto& payload : client_.ReceiveMessages()) {
        const auto message = augusta::protocol::Decode(payload);
        if (message.has_value() && std::holds_alternative<augusta::protocol::JoinAccepted>(*message)) {
          return true;
        }
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  void Send(const augusta::protocol::Message& message) {
    client_.Send(augusta::protocol::Encode(message), augusta::networking::Reliability::kReliable);
  }

  // The Authoritative State updates received since the last call.
  std::vector<augusta::protocol::AuthoritativeState> Receive() {
    client_.PumpEvents();
    std::vector<augusta::protocol::AuthoritativeState> states;
    for (const auto& payload : client_.ReceiveMessages()) {
      const auto message = augusta::protocol::Decode(payload);
      if (message.has_value()) {
        if (const auto* state = std::get_if<augusta::protocol::AuthoritativeState>(&*message)) {
          states.push_back(*state);
        }
      }
    }
    return states;
  }

 private:
  augusta::networking::Client client_;
};

TEST_F(MovementTest, CommandsThatAreOutOfOrderNonFiniteOrOutOfRangeAreDroppedWithoutAffectingTheWorld) {
  RawClient raw(Endpoint{.address = LoopbackAddress()});
  ASSERT_TRUE(raw.Join(host_));

  const auto command = [](std::uint32_t sequence, float yaw = 0.0F, float pitch = 0.0F) {
    augusta::protocol::SequencedCommand sequenced{.sequence = sequence};
    sequenced.command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    sequenced.command.yaw = yaw;
    sequenced.command.pitch = pitch;
    return sequenced;
  };
  constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float kImpossiblePitch = 5.0F;
  // 1 and 4 are good; 2 and 3 are numbers no client produces; 6 arrives before 5.
  raw.Send(augusta::protocol::Commands{
      .commands = {command(1), command(2, kNaN), command(3, 0.0F, kImpossiblePitch), command(4)}});
  raw.Send(augusta::protocol::Commands{.commands = {command(6)}});
  raw.Send(augusta::protocol::Commands{.commands = {command(5)}});

  std::vector<std::uint32_t> acknowledged;
  for (int i = 0; i < 12; ++i) {
    std::this_thread::sleep_for(kNetworkDelay);
    host_.PumpNetwork();
    host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    for (const auto& state : raw.Receive()) {
      acknowledged.push_back(state.acknowledged_sequence);
      for (const auto& player : state.players) {
        EXPECT_TRUE(std::isfinite(player.body.position.x) && std::isfinite(player.body.position.y));
      }
    }
  }

  ASSERT_FALSE(acknowledged.empty());
  EXPECT_EQ(acknowledged.back(), 6U);
  for (const std::uint32_t ack : acknowledged) {
    EXPECT_TRUE(ack == 0 || ack == 1 || ack == 4 || ack == 6)
        << "processed a command that should have been dropped: " << ack;
  }
}

}  // namespace
