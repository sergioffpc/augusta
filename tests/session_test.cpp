#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/simulation.h"
#include "augusta/version.h"
#include "host.h"
#include "match.h"
#include "parameters_loader.h"

// The seam the M3 tickets test through (issue #73): a real server host and a
// real client session, both without a window, a GPU or a wall-clock loop, in
// one process over loopback. The tests drive both sides' network work and
// their ticks by hand.
namespace {

using augusta::harness::Failure;
using augusta::harness::FailureKind;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::input::Command;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::protocol::JoinRefusal;
using augusta::server::Host;
using augusta::server::HostConfig;

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);
constexpr float kFixedTick = 1.0F / 60.0F;

// What a test's server runs on: NFR-01's 60 Hz and stamina rules that never drain.
constexpr float kTestTickRate = 60.0F;
constexpr Parameters kTestParameters{};

// A PredictionWorld with no map, and nothing decided yet: a server decides it on the client's join.
augusta::prediction::World EmptyWorld() { return augusta::prediction::World(); }

// ctest runs every test case in its own process, possibly in parallel, so a
// fixed port would collide; derive one from the process id instead.
std::string LoopbackAddress() {
#ifdef _WIN32
  const int pid = _getpid();
#else
  const int pid = getpid();
#endif
  constexpr int kFirstPort = 27100;
  // Prime, since Windows process ids are all multiples of 4 and a span sharing
  // that factor would leave only a quarter of its ports in use.
  constexpr int kPortSpan = 2999;
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
      : host_(HostConfig{.tick_rate_hz = kTestTickRate,
                         .parameters = kTestParameters,
                         .script_path = "scripts/round.lua",
                         .listen = Endpoint{.address = LoopbackAddress()}}),
        session_(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld()) {}

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
      : host_(HostConfig{.tick_rate_hz = kTestTickRate,
                         .parameters = kTestParameters,
                         .script_path = "scripts/round.lua",
                         .listen = Endpoint{.address = LoopbackAddress()}}) {}

  // Starts connecting a new client that presents engine_version.
  Session& AddClient(const std::string& engine_version = std::string(augusta::EngineVersion())) {
    sessions_.push_back(std::make_unique<Session>(
        SessionConfig{.server = Endpoint{.address = LoopbackAddress()}, .engine_version = engine_version},
        EmptyWorld()));
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

TEST_F(JoinTest, ARefusedClientReportsTheRefusalAsItsFailure) {
  Session& client = AddClient("0.0.0-not-the-servers");

  ASSERT_TRUE(WaitForAnswers());

  const auto failure = client.GetFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kRefused);
  EXPECT_EQ(failure->refusal, JoinRefusal::kVersionMismatch);
}

TEST_F(JoinTest, AnAdmittedClientHasNoFailure) {
  Session& client = AddClient();

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_FALSE(client.GetFailure().has_value());
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
CollisionMesh FloorAt(float y) {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, y, -kExtent), Vec3(-kExtent, y, kExtent), Vec3(kExtent, y, kExtent),
                                  Vec3(kExtent, y, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A PredictionWorld whose map is a floor at height y.
augusta::prediction::World WorldWithFloorAt(float y) {
  augusta::prediction::World world = EmptyWorld();
  EXPECT_TRUE(world.AddCollisionMesh(FloorAt(y)).has_value());
  return world;
}

// The client spawns its predicted body at the origin, so a floor two meters
// below it is where that body must come to rest.
constexpr float kFloorHeight = -2.0F;
constexpr int kFallTicks = 120;

TEST(MapSessionTest, ThePredictedBodyRestsOnTheMapsFloor) {
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, WorldWithFloorAt(kFloorHeight));

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }

  EXPECT_NEAR(state.local_body.position.y, kFloorHeight, 0.2F);
}

TEST(MapSessionTest, WithoutAMapThePredictedBodyKeepsFalling) {
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }

  EXPECT_LT(state.local_body.position.y, kFloorHeight - 5.0F);
}

TEST(MapSessionTest, APredictionWorldRefusesAMapMeshPhysicsRejects) {
  augusta::prediction::World world = EmptyWorld();

  const auto added = world.AddCollisionMesh(CollisionMesh{});

  ASSERT_FALSE(added.has_value());
  EXPECT_EQ(added.error(), augusta::physics::CollisionMeshError::kEmpty);
}

TEST(MapHostTest, AHostAcceptsAMapAndKeepsTicking) {
  Host host(HostConfig{.tick_rate_hz = kTestTickRate,
                       .parameters = kTestParameters,
                       .script_path = "scripts/round.lua",
                       .listen = Endpoint{.address = LoopbackAddress()},
                       .collision = {FloorAt(0.0F)}});

  for (int i = 0; i < 10; ++i) {
    host.Tick(kFixedTick);
  }
  SUCCEED();
}

TEST(MapHostTest, AHostRefusesAMapMeshPhysicsRejects) {
  EXPECT_THROW(Host(HostConfig{.tick_rate_hz = kTestTickRate,
                               .parameters = kTestParameters,
                               .script_path = "scripts/round.lua",
                               .listen = Endpoint{.address = LoopbackAddress()},
                               .collision = {CollisionMesh{}}}),
               std::runtime_error);
}

// A joined client and a host with flat ground, driven tick by tick.
class MovementTest : public ::testing::Test {
 protected:
  // Every player spawns at the origin, so the ground is a little below it:
  // a body placed exactly on a floor starts overlapping it, which PhysX does not resolve well.
  static constexpr float kGroundHeight = -0.5F;

  static constexpr auto kNetworkDelay = std::chrono::milliseconds(8);
  // The ticks SetUp runs, each with one command sent.
  static constexpr int kSettleTicks = 30;

  MovementTest() : MovementTest({FloorAt(kGroundHeight)}) {}

  // The client always knows the floor; the host knows server_map, which a test
  // may make differ from it to give the two something to disagree about.
  explicit MovementTest(std::vector<CollisionMesh> server_map)
      : host_(HostConfig{.tick_rate_hz = kTestTickRate,
                         .parameters = kTestParameters,
                         .script_path = "scripts/round.lua",
                         .listen = Endpoint{.address = LoopbackAddress()},
                         .collision = std::move(server_map)}),
        session_(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, WorldWithFloorAt(kGroundHeight)) {}

  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }

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

  // One tick of the whole match: the client predicts and sends command, then
  // the server ticks. Returns what the client predicted.
  augusta::prediction::State Step(const Command& command) {
    const augusta::prediction::State predicted = session_.Tick(command, kFixedTick);
    ServerTickAndDeliver();
    return predicted;
  }

  // Ticks the whole match with command for the given number of steps.
  augusta::prediction::State Run(int steps, const Command& command) {
    augusta::prediction::State predicted;
    for (int i = 0; i < steps; ++i) {
      predicted = Step(command);
    }
    return predicted;
  }

  // Ticks the server, without the client sending anything more, until it has
  // processed the command with this sequence or a generous number of ticks has
  // passed; returns the sequence it has processed.
  std::uint32_t DrainServer(int last_sequence) {
    constexpr int kMaxTicks = 120;
    std::uint32_t acknowledged = session_.GetAuthoritativeState()->acknowledged_sequence;
    for (int i = 0; i < kMaxTicks && acknowledged < static_cast<std::uint32_t>(last_sequence); ++i) {
      ServerTickAndDeliver();
      acknowledged = session_.GetAuthoritativeState()->acknowledged_sequence;
    }
    return acknowledged;
  }

  // How far apart, on the ground plane, the client's prediction and the newest
  // authoritative state of its player are.
  [[nodiscard]] float PredictionError(const augusta::prediction::State& predicted) const {
    const Vec3 authoritative = Self().position;
    return std::hypot(predicted.local_body.position.x - authoritative.x,
                      predicted.local_body.position.z - authoritative.z);
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

TEST_F(MovementTest, AtAHundredMillisecondsOfLatencyNoCommandIsLostAndThePredictionSettlesOnTheServer) {
  constexpr int kOneWayLatencyMs = 50;
  constexpr int kWalkSteps = 60;
  constexpr int kSettleSteps = 60;
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs});

  Run(kWalkSteps, Walking());
  Run(kSettleSteps, Command{});

  // Every command reached the server and was processed, and what the client predicts is where the server has the
  // player.
  EXPECT_EQ(DrainServer(kSettleTicks + kWalkSteps + kSettleSteps),
            static_cast<std::uint32_t>(kSettleTicks + kWalkSteps + kSettleSteps));
  EXPECT_LT(PredictionError(Run(kSettleSteps, Command{})), 0.05F);
}

TEST_F(MovementTest, WithPacketLossEveryCommandIsStillProcessedAndThePredictionStaysConsistent) {
  constexpr float kLossPercent = 20.0F;
  constexpr int kWalkSteps = 60;
  constexpr int kSettleSteps = 60;
  augusta::networking::SimulateNetworkConditions({.loss_percent = kLossPercent});

  Run(kWalkSteps, Walking());
  Run(kSettleSteps, Command{});

  // The last commands sent under loss may have been lost for good, since the
  // client has nothing newer to repeat them with; a few more sent over a clean
  // network carry whatever the server is still missing.
  augusta::networking::SimulateNetworkConditions({});
  constexpr int kRecoverySteps = 10;
  Run(kRecoverySteps, Command{});

  const auto sent = static_cast<std::uint32_t>(kSettleTicks + kWalkSteps + kSettleSteps + kRecoverySteps);
  EXPECT_EQ(DrainServer(sent), sent);
  EXPECT_LT(PredictionError(Run(kSettleSteps, Command{})), 0.05F);
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

  void Send(const augusta::protocol::Message& message) { SendPayload(augusta::protocol::Encode(message)); }

  // Sends bytes as they are, whether or not they are a message.
  void SendPayload(const augusta::protocol::Bytes& payload) {
    client_.Send(payload, augusta::networking::Reliability::kReliable);
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

// A vertical wall across the walking path, at x, that only the server knows.
CollisionMesh WallAt(float x) {
  constexpr float kHalfWidth = 20.0F;
  constexpr float kHeight = 5.0F;
  constexpr float kBottom = -1.0F;
  return CollisionMesh{.points = {Vec3(x, kBottom, -kHalfWidth), Vec3(x, kHeight, -kHalfWidth),
                                  Vec3(x, kHeight, kHalfWidth), Vec3(x, kBottom, kHalfWidth)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

class DivergedMovementTest : public MovementTest {
 protected:
  static constexpr float kWallX = 3.0F;

  DivergedMovementTest() : MovementTest({FloorAt(kGroundHeight), WallAt(kWallX)}) {}
};

TEST_F(DivergedMovementTest, ADivergenceAtAHundredMillisecondsOfLatencyIsCorrectedWithinTheBudget) {
  constexpr int kOneWayLatencyMs = 50;
  constexpr int kWalkSteps = 90;
  // The round trip plus NFR-02's 150 ms, in 16 ms steps of this test.
  constexpr int kBudgetSteps = 16;
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs});

  // The client walks through a wall only the server has; the server stops the player at it.
  const auto walked = Run(kWalkSteps, Walking());
  ASSERT_GT(PredictionError(walked), 0.05F) << "the client and the server never disagreed";

  augusta::prediction::State predicted = walked;
  int steps_to_agree = 0;
  while (PredictionError(predicted) > 0.1F && steps_to_agree < 4 * kBudgetSteps) {
    predicted = Step(Command{});
    ++steps_to_agree;
  }

  EXPECT_LE(steps_to_agree, kBudgetSteps);
  EXPECT_LT(Self().position.x, kWallX);
}

// A host on a floor and however many clients a test joins to it, driven tick by tick.
class LoopbackMatch : public ::testing::Test {
 protected:
  static constexpr float kFloorY = 0.0F;
  static constexpr int kSettleTicks = 30;
  static constexpr auto kNetworkDelay = std::chrono::milliseconds(8);

  explicit LoopbackMatch(const HostConfig& config) : host_(config) {}

  // A host config for the floor with spawn_points, the parameters and the tick rate.
  static HostConfig OnTheFloor(std::vector<Vec3> spawn_points, const Parameters& parameters = kTestParameters,
                               float tick_rate_hz = kTestTickRate) {
    return HostConfig{.tick_rate_hz = tick_rate_hz,
                      .parameters = parameters,
                      .script_path = "scripts/round.lua",
                      .listen = Endpoint{.address = LoopbackAddress()},
                      .collision = {FloorAt(kFloorY)},
                      .spawn_points = std::move(spawn_points)};
  }

  // Connects a new client and runs the network until the server has answered it.
  // The client's own stamina rules are none: any it uses came from the server.
  Session& Join() {
    sessions_.push_back(std::make_unique<Session>(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}},
                                                  WorldWithFloorAt(kFloorY)));
    Session& client = *sessions_.back();
    client.Connect();
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (!client.GetSessionId().has_value() && std::chrono::steady_clock::now() < deadline) {
      Exchange();
      std::this_thread::sleep_for(kPollInterval);
    }
    EXPECT_TRUE(client.GetSessionId().has_value());
    return client;
  }

  void Exchange() {
    host_.PumpNetwork();
    for (const auto& session : sessions_) {
      session->PumpEvents();
      session->ExchangeMessages();
    }
  }

  // One tick of the whole match: every client predicts and sends command, the
  // server ticks, the states come back.
  void Step(const Command& command = Command{}) {
    for (const auto& session : sessions_) {
      states_[session.get()] = session->Tick(command, kFixedTick);
    }
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
  }

  void Run(int steps, const Command& command = Command{}) {
    for (int i = 0; i < steps; ++i) {
      Step(command);
    }
  }

  // Where the newest state client received puts session, or nullopt if it lists no such player.
  static std::optional<augusta::physics::BodyState> BodySeenBy(const Session& client,
                                                               augusta::protocol::SessionId session) {
    const auto state = client.GetAuthoritativeState();
    if (state.has_value()) {
      for (const auto& player : state->players) {
        if (player.session == session) {
          return player.body;
        }
      }
    }
    return std::nullopt;
  }

  static std::optional<Vec3> PositionSeenBy(const Session& client, augusta::protocol::SessionId session) {
    const auto body = BodySeenBy(client, session);
    return body.has_value() ? std::optional<Vec3>(body->position) : std::nullopt;
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
  std::map<const Session*, augusta::prediction::State> states_;
};

class SpawnTest : public LoopbackMatch {
 protected:
  static std::vector<Vec3> SpawnPoints() {
    return {Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F), Vec3(30.0F, kFloorY, -5.0F)};
  }

  SpawnTest() : LoopbackMatch(OnTheFloor(SpawnPoints())) {}
};

TEST_F(SpawnTest, TwoClientsThatJoinBackToBackSpawnAtDifferentSpawnPoints) {
  Session& first = Join();
  Session& second = Join();

  Run(kSettleTicks);

  const auto first_position = PositionSeenBy(first, *first.GetSessionId());
  const auto second_position = PositionSeenBy(first, *second.GetSessionId());
  ASSERT_TRUE(first_position.has_value() && second_position.has_value());
  EXPECT_NEAR(first_position->x, SpawnPoints()[0].x, 0.1F);
  EXPECT_NEAR(first_position->z, SpawnPoints()[0].z, 0.1F);
  EXPECT_NEAR(second_position->x, SpawnPoints()[1].x, 0.1F);
  EXPECT_NEAR(second_position->z, SpawnPoints()[1].z, 0.1F);
}

TEST_F(SpawnTest, AClientsPredictionStartsAtItsSpawnPoint) {
  Join();
  Session& second = Join();

  Run(1);

  const augusta::physics::BodyState& body = states_.at(&second).local_body;
  EXPECT_NEAR(body.position.x, SpawnPoints()[1].x, 0.1F);
  EXPECT_NEAR(body.position.z, SpawnPoints()[1].z, 0.1F);
}

TEST_F(SpawnTest, AJoiningClientIsToldWhoIsAlreadyThereAndWhereTheyAreNow) {
  Session& first = Join();
  Run(kSettleTicks);
  // The first player walks away from its spawn point (3 m/s for half a second or more).
  Command walk;
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  Run(kSettleTicks, walk);

  Session& second = Join();

  const auto roster = second.GetRoster();
  ASSERT_EQ(roster.size(), 1U);
  EXPECT_EQ(roster[0].session, *first.GetSessionId());
  EXPECT_GT(roster[0].body.position.x, SpawnPoints()[0].x + 1.0F);
  EXPECT_NEAR(roster[0].body.position.z, SpawnPoints()[0].z, 0.1F);
}

TEST_F(SpawnTest, TheFirstClientAloneInTheMatchHasAnEmptyRoster) { EXPECT_TRUE(Join().GetRoster().empty()); }

TEST_F(SpawnTest, ThePlayersAlreadyThereSeeTheNewcomerAppearInTheirState) {
  Session& first = Join();
  Run(kSettleTicks);
  ASSERT_EQ(first.GetAuthoritativeState()->players.size(), 1U);

  Session& second = Join();
  Run(kSettleTicks);

  EXPECT_EQ(first.GetAuthoritativeState()->players.size(), 2U);
  EXPECT_TRUE(PositionSeenBy(first, *second.GetSessionId()).has_value());
}

TEST_F(SpawnTest, MoreJoinsThanSpawnPointsWrapInsteadOfFailing) {
  const std::size_t joins = SpawnPoints().size() + 1;
  for (std::size_t i = 0; i < joins; ++i) {
    Join();
  }

  Run(kSettleTicks);

  Session& last = *sessions_.back();
  EXPECT_EQ(last.GetAuthoritativeState()->players.size(), joins);
  const auto position = PositionSeenBy(last, *last.GetSessionId());
  ASSERT_TRUE(position.has_value());
  EXPECT_NEAR(position->x, SpawnPoints()[0].x, 0.1F);
}

// One client on a floor, on the server's stamina rules: a bar that empties in
// a second of sprinting and refills in four of rest, and a walk forced at or
// below a fifth of it.
class StaminaTest : public LoopbackMatch {
 protected:
  static constexpr float kForcedWalkBelow = 0.2F;
  static constexpr float kWalkSpeed = 3.0F;
  static constexpr float kSprintSpeed = 4.8F;

  StaminaTest()
      : LoopbackMatch(OnTheFloor(
            {}, {.stamina = {
                     .deplete_per_second = 1.0F, .regen_per_second = 0.25F, .forced_walk_below = kForcedWalkBelow}})) {}

  void SetUp() override {
    client_ = &Join();
    Run(kSettleTicks);
  }

  static Command Moving(bool sprint) {
    Command command;
    command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    command.movement.sprint = sprint;
    return command;
  }

  [[nodiscard]] augusta::physics::BodyState Authoritative() const {
    return BodySeenBy(*client_, *client_->GetSessionId()).value();
  }

  static float Speed(const augusta::physics::BodyState& body) { return std::hypot(body.velocity.x, body.velocity.z); }

  // The player's mean authoritative speed over ticks ticks of command.
  float MeanSpeedOver(int ticks, const Command& command) {
    float total = 0.0F;
    for (int i = 0; i < ticks; ++i) {
      Step(command);
      total += Speed(Authoritative());
    }
    return total / static_cast<float>(ticks);
  }

  Session* client_ = nullptr;
};

TEST_F(StaminaTest, SustainedSprintDrainsStaminaToTheThresholdAndSpeedDropsToWalking) {
  const float fresh_speed = MeanSpeedOver(10, Moving(/*sprint=*/true));

  Run(90, Moving(/*sprint=*/true));
  const float worn_out_speed = MeanSpeedOver(30, Moving(/*sprint=*/true));

  EXPECT_NEAR(fresh_speed, kSprintSpeed, 0.3F);
  EXPECT_LE(Authoritative().stamina, kForcedWalkBelow + 0.05F);
  // Not always exactly walking speed: the rule is memoryless, so a player
  // holding sprint at the threshold gets the odd sprinting tick back.
  EXPECT_LT(worn_out_speed, (kWalkSpeed + kSprintSpeed) / 2.0F);
}

TEST_F(StaminaTest, StaminaRecoversWhileNotSprintingAndSprintIsAllowedAgainOnlyAboveTheThreshold) {
  Run(100, Moving(/*sprint=*/true));
  const float drained = Authoritative().stamina;
  ASSERT_LE(drained, kForcedWalkBelow + 0.05F);

  // Walking is not sprinting, so stamina comes back.
  Run(30, Moving(/*sprint=*/false));
  EXPECT_GT(Authoritative().stamina, drained);

  // Well above the threshold, sprint is honored again.
  Run(70, Moving(/*sprint=*/false));
  ASSERT_GT(Authoritative().stamina, kForcedWalkBelow + 0.3F);
  Run(5, Moving(/*sprint=*/true));
  EXPECT_GT(Speed(Authoritative()), kWalkSpeed + 1.0F);
}

TEST_F(StaminaTest, AClientPredictsItsStaminaWithTheServersRulesNotItsOwn) {
  // The client was built with rules that never drain: it can only be following the server's.
  Run(45, Moving(/*sprint=*/true));

  const float predicted = states_.at(client_).local_body.stamina;

  EXPECT_LT(predicted, 0.7F);
  EXPECT_NEAR(predicted, Authoritative().stamina, 0.1F);
}

// The server's Parameters come from a script (ADR-0039), loaded the way augustad
// loads the one in its pack. A bar that empties in a second of sprinting and never refills.
class ScriptedParametersTest : public LoopbackMatch {
 protected:
  static augusta::parameters::Parameters LoadScript() {
    const auto loaded = augusta::parameters::Load(
        "local sprint_seconds = 1\n"
        "return {\n"
        "  stamina = {\n"
        "  deplete_per_second = 1 / sprint_seconds,\n"
        "  regen_per_second = 0,\n"
        "  forced_walk_below = 0.2,\n"
        "} }");
    return loaded.value();
  }

  ScriptedParametersTest() : LoopbackMatch(OnTheFloor({}, LoadScript())) {}
};

TEST_F(ScriptedParametersTest, AClientPredictsItsStaminaWithTheRulesOfTheServersScript) {
  Session& client = Join();
  Run(kSettleTicks);
  Command sprint;
  sprint.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  sprint.movement.sprint = true;

  Run(45, sprint);

  // Nothing else depletes a bar this fast and nothing refills it: the client is following the script.
  EXPECT_LT(states_.at(&client).local_body.stamina, 0.7F);
  EXPECT_NEAR(states_.at(&client).local_body.stamina, BodySeenBy(client, *client.GetSessionId())->stamina, 0.1F);
}

// A server speaking the protocol by hand to one client, to send what a real
// one would not: values that fail the checks, or bytes that are no message.
class ScriptedServer {
 public:
  explicit ScriptedServer(const Endpoint& listen) : server_(listen) {}

  // Connects session and answers its join with parameters, and reports whether
  // the session was admitted within wait.
  bool Admit(Session& session, const Parameters& parameters, std::chrono::milliseconds wait = kPollDeadline) {
    parameters_ = parameters;
    session.Connect();
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (!session.GetSessionId().has_value() && std::chrono::steady_clock::now() < deadline) {
      Pump();
      session.PumpEvents();
      session.ExchangeMessages();
      std::this_thread::sleep_for(kPollInterval);
    }
    return session.GetSessionId().has_value();
  }

  // Serves the connection and the join, and takes in whatever the client sent.
  void Pump() {
    for (const auto& event : server_.PumpEvents()) {
      if (event.type == augusta::networking::PeerEventType::kConnectRequested) {
        server_.Accept(event.peer);
      }
    }
    for (const auto& message : server_.ReceiveMessages()) {
      peer_ = message.from;
      const auto decoded = augusta::protocol::Decode(message.payload);
      if (decoded.has_value() && std::holds_alternative<augusta::protocol::JoinRequest>(*decoded)) {
        Send(augusta::protocol::JoinAccepted{
            .session = augusta::protocol::SessionId{1}, .tick_rate_hz = kTestTickRate, .parameters = parameters_});
      }
    }
  }

  void Send(const augusta::protocol::Message& message) { SendPayload(augusta::protocol::Encode(message)); }

  // Sends bytes as they are, whether or not they are a message.
  void SendPayload(const augusta::protocol::Bytes& payload) {
    server_.Send(*peer_, payload, augusta::networking::Reliability::kReliable);
  }

 private:
  augusta::networking::Server server_;
  std::optional<augusta::networking::PeerId> peer_;
  Parameters parameters_;
};

// A client admitted by a server that then sends it what a real one would not.
class ScriptedServerTest : public ::testing::Test {
 protected:
  // A bar that empties at deplete_per_second and never refills.
  static Parameters WithDeplete(float deplete_per_second) {
    return Parameters{
        .stamina = {.deplete_per_second = deplete_per_second, .regen_per_second = 0.0F, .forced_walk_below = 0.0F}};
  }

  ScriptedServerTest()
      : server_(Endpoint{.address = LoopbackAddress()}),
        session_(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, WorldWithFloorAt(0.0F)) {}

  void SetUp() override { ASSERT_TRUE(server_.Admit(session_, WithDeplete(0.0F))); }

  // Lets the client take in, or refuse, whatever was sent: long enough that a
  // message that was going to arrive has.
  void Settle() {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) {
      server_.Pump();
      session_.PumpEvents();
      session_.ExchangeMessages();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  // The stamina the client predicts after sprinting for ticks ticks.
  float PredictedStaminaAfterSprinting(int ticks) {
    Command sprint;
    sprint.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    sprint.movement.sprint = true;
    augusta::prediction::State state;
    for (int i = 0; i < ticks; ++i) {
      state = session_.Tick(sprint, kFixedTick);
    }
    return state.local_body.stamina;
  }

  ScriptedServer server_;
  Session session_;
};

TEST_F(ScriptedServerTest, AClientPredictsWithTheParametersItWasAdmittedWith) {
  ASSERT_TRUE(session_.GetParameters().has_value());
  EXPECT_FLOAT_EQ(session_.GetParameters()->stamina.deplete_per_second, 0.0F);
  EXPECT_GT(PredictedStaminaAfterSprinting(60), 0.99F);
}

TEST_F(ScriptedServerTest, BytesThatAreNoMessageChangeNothingAndTheClientKeepsRunning) {
  // What used to be a Parameters update, a type that is gone, and a type nobody has.
  for (const auto& payload :
       {augusta::protocol::Bytes{std::byte{6}, std::byte{2}}, augusta::protocol::Bytes{std::byte{0xFF}}}) {
    server_.SendPayload(payload);
    Settle();
  }

  EXPECT_FLOAT_EQ(session_.GetParameters()->stamina.deplete_per_second, 0.0F);
  EXPECT_GT(PredictedStaminaAfterSprinting(60), 0.99F);
}

// A client takes the parameters it joins with as the server's, so values that
// fail the range checks make it drop the Join accepted rather than predict on them.
TEST(InvalidParametersTest, AClientDropsAJoinAcceptedWhoseParametersFailTheRangeChecks) {
  Parameters threshold_of_one;
  threshold_of_one.stamina.forced_walk_below = 1.0F;
  for (const Parameters& bad :
       {Parameters{.stamina = {.deplete_per_second = -1.0F}},
        Parameters{.stamina = {.regen_per_second = std::numeric_limits<float>::quiet_NaN()}}, threshold_of_one}) {
    ScriptedServer server(Endpoint{.address = LoopbackAddress()});
    Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());

    EXPECT_FALSE(server.Admit(session, bad, std::chrono::milliseconds(500)));
    EXPECT_FALSE(session.GetParameters().has_value());
  }
}

// A server at 30 Hz: everything a client does with time it must take from what it is told.
class TickRateTest : public LoopbackMatch {
 protected:
  static constexpr float kServerRate = 30.0F;

  TickRateTest() : LoopbackMatch(OnTheFloor({}, kTestParameters, kServerRate)) {}
};

TEST_F(TickRateTest, AClientLearnsTheServersTickRateWhenItJoins) {
  Session& client = Join();

  ASSERT_TRUE(client.GetTickRate().has_value());
  EXPECT_FLOAT_EQ(*client.GetTickRate(), kServerRate);
}

TEST_F(TickRateTest, AClientTickingAtTheRateItWasToldAgreesWithTheServerWithoutCorrection) {
  Session& client = Join();
  const float client_delta = 1.0F / *client.GetTickRate();
  const float server_delta = 1.0F / kServerRate;
  Command walk;
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);

  augusta::prediction::State state;
  for (int i = 0; i < 90; ++i) {
    state = client.Tick(walk, client_delta);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    host_.Tick(server_delta);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
  }

  // Walking 3 s at 3 m/s: what the client covered is what the server did, so
  // reconciliation never had a jump to make.
  EXPECT_GT(state.local_body.position.x, 6.0F);
  EXPECT_NEAR(state.local_body.position.x, BodySeenBy(client, *client.GetSessionId())->position.x, 0.5F);
  EXPECT_NEAR(state.total_correction.x, 0.0F, 0.01F);
  EXPECT_NEAR(state.total_correction.z, 0.0F, 0.01F);
}

// A client that has not joined holds nothing a server decides.
TEST_F(SessionTest, AClientHoldsNoParametersUntilTheServerAdmitsIt) {
  EXPECT_FALSE(session_.GetParameters().has_value());
  EXPECT_FALSE(session_.GetTickRate().has_value());
  ASSERT_TRUE(ConnectSession());
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (!session_.GetSessionId().has_value() && std::chrono::steady_clock::now() < deadline) {
    host_.PumpNetwork();
    session_.PumpEvents();
    session_.ExchangeMessages();
    std::this_thread::sleep_for(kPollInterval);
  }

  ASSERT_TRUE(session_.GetSessionId().has_value());
  ASSERT_TRUE(session_.GetParameters().has_value());
  ASSERT_TRUE(session_.GetTickRate().has_value());
  EXPECT_FLOAT_EQ(*session_.GetTickRate(), kTestTickRate);
}

// A server whose tick rate is unusable (zero): the client drops the Join
// accepted rather than divide by it.
TEST(InvalidParametersTest, AClientDropsAJoinAcceptedWhoseTickRateFailsTheChecks) {
  Host host(HostConfig{
      .tick_rate_hz = 0.0F, .script_path = "scripts/round.lua", .listen = Endpoint{.address = LoopbackAddress()}});
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());
  session.Connect();

  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (std::chrono::steady_clock::now() < until) {
    host.PumpNetwork();
    session.PumpEvents();
    session.ExchangeMessages();
    std::this_thread::sleep_for(kPollInterval);
  }

  EXPECT_FALSE(session.GetSessionId().has_value());
  EXPECT_FALSE(session.GetTickRate().has_value());
  EXPECT_FALSE(session.GetParameters().has_value());
}

// What a client is told when its session ends on its own.
TEST(SessionFailureTest, ASessionThatNeverConnectedHasNoFailure) {
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());

  EXPECT_FALSE(session.GetFailure().has_value());
}

TEST(SessionFailureTest, AServerNobodyIsListeningAtIsUnreachable) {
  // Set before connecting: the timeout only reaches new connections.
  augusta::networking::SimulateNetworkConditions({.timeout_ms = 500});
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());
  session.Connect();

  std::optional<Failure> failure;
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (!failure.has_value() && std::chrono::steady_clock::now() < deadline) {
    session.PumpEvents();
    session.ExchangeMessages();
    failure = session.GetFailure();
    std::this_thread::sleep_for(kPollInterval);
  }
  augusta::networking::SimulateNetworkConditions({});

  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kServerUnreachable);
}

TEST(SessionFailureTest, AServerThatGoesAwayAfterAdmittingTheClientIsAConnectionLost) {
  auto host = std::make_unique<Host>(HostConfig{.tick_rate_hz = kTestTickRate,
                                                .parameters = kTestParameters,
                                                .script_path = "scripts/round.lua",
                                                .listen = Endpoint{.address = LoopbackAddress()}});
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());
  session.Connect();
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (!session.GetSessionId().has_value() && std::chrono::steady_clock::now() < deadline) {
    host->PumpNetwork();
    session.PumpEvents();
    session.ExchangeMessages();
    std::this_thread::sleep_for(kPollInterval);
  }
  ASSERT_TRUE(session.GetSessionId().has_value());
  ASSERT_FALSE(session.GetFailure().has_value());

  host.reset();
  std::optional<Failure> failure;
  const auto end = std::chrono::steady_clock::now() + kPollDeadline;
  while (!failure.has_value() && std::chrono::steady_clock::now() < end) {
    session.PumpEvents();
    session.ExchangeMessages();
    failure = session.GetFailure();
    std::this_thread::sleep_for(kPollInterval);
  }

  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kConnectionLost);
}

TEST(SessionFailureTest, EndingTheSessionOneselfIsNotAFailure) {
  Host host(HostConfig{.tick_rate_hz = kTestTickRate,
                       .parameters = kTestParameters,
                       .script_path = "scripts/round.lua",
                       .listen = Endpoint{.address = LoopbackAddress()}});
  Session session(SessionConfig{.server = Endpoint{.address = LoopbackAddress()}}, EmptyWorld());
  session.Connect();
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (session.GetState() != ConnectionState::kConnected && std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork();
    session.PumpEvents();
    std::this_thread::sleep_for(kPollInterval);
  }
  ASSERT_EQ(session.GetState(), ConnectionState::kConnected);

  session.Disconnect();
  session.PumpEvents();

  EXPECT_FALSE(session.GetFailure().has_value());
}

TEST(SessionFailureTest, EveryFailureIsDescribedForThePlayerAndARefusalSaysWhy) {
  const std::string unreachable = augusta::harness::DescribeFailure({.kind = FailureKind::kServerUnreachable});
  const std::string lost = augusta::harness::DescribeFailure({.kind = FailureKind::kConnectionLost});
  const std::string full =
      augusta::harness::DescribeFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kMatchFull});
  const std::string version =
      augusta::harness::DescribeFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kVersionMismatch});

  EXPECT_FALSE(unreachable.empty());
  EXPECT_FALSE(lost.empty());
  EXPECT_NE(unreachable, lost);
  EXPECT_NE(full, version);
  EXPECT_NE(full.find(std::string(augusta::protocol::DescribeJoinRefusal(JoinRefusal::kMatchFull))), std::string::npos)
      << full;
}

// The server against peers that misbehave or leave.
class RobustnessTest : public LoopbackMatch {
 protected:
  static constexpr auto kSilentPeerDeadline = std::chrono::seconds(20);

  RobustnessTest() : LoopbackMatch(OnTheFloor({})) {}

  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }

  // Ticks the server once, with the network work around it.
  augusta::simulation::State ServerTick() {
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    auto state = host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    return state;
  }

  static Command Forward() {
    Command command;
    command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    return command;
  }
};

TEST_F(RobustnessTest, GarbageFromAPeerIsDroppedAndTheMatchAndTheOtherClientsAreUnaffected) {
  Session& bystander = Join();
  Run(kSettleTicks);
  RawClient raw(Endpoint{.address = LoopbackAddress()});
  ASSERT_TRUE(raw.Join(host_));

  using augusta::protocol::Bytes;
  Bytes truncated_state = augusta::protocol::Encode(augusta::protocol::AuthoritativeState{.players = {{}}});
  truncated_state.resize(truncated_state.size() / 2);
  Bytes not_for_the_server = augusta::protocol::Encode(augusta::protocol::AuthoritativeState{});
  Bytes commands_with_trailing_bytes = augusta::protocol::Encode(augusta::protocol::Commands{});
  commands_with_trailing_bytes.push_back(std::byte{7});
  const Bytes garbage[] = {
      Bytes{},
      Bytes{std::byte{0}},
      Bytes{std::byte{0xFF}, std::byte{1}, std::byte{2}},
      truncated_state,
      not_for_the_server,
      commands_with_trailing_bytes,
      Bytes(64 * 1024, std::byte{0xAB}),
      Bytes(1000, std::byte{static_cast<unsigned char>(augusta::protocol::MessageType::kCommands)}),
  };
  for (const Bytes& payload : garbage) {
    raw.SendPayload(payload);
  }
  const auto before = bystander.GetAuthoritativeState();
  ASSERT_TRUE(before.has_value());

  // The match goes on: the server ticks, and the bystander keeps predicting and being reconciled.
  Run(kSettleTicks, Forward());

  const auto after = bystander.GetAuthoritativeState();
  ASSERT_TRUE(after.has_value());
  EXPECT_GT(after->tick, before->tick);
  EXPECT_EQ(after->players.size(), 2U);  // The bystander, and the raw peer that only joined.
  EXPECT_GT(BodySeenBy(bystander, *bystander.GetSessionId())->position.x, 1.0F);
  for (const auto& player : after->players) {
    EXPECT_TRUE(std::isfinite(player.body.position.x) && std::isfinite(player.body.position.y));
  }
  EXPECT_EQ(bystander.GetState(), ConnectionState::kConnected);
}

TEST_F(RobustnessTest, AfterAClientDisconnectsItsPlayerIsAbsentFromOthersStateAndANinthClientCanJoin) {
  for (std::size_t i = 0; i < augusta::protocol::kMaxPlayers; ++i) {
    Join();
  }
  Run(kSettleTicks);
  Session& watcher = *sessions_.front();
  ASSERT_EQ(watcher.GetAuthoritativeState()->players.size(), augusta::protocol::kMaxPlayers);
  const auto leaver = *sessions_.back()->GetSessionId();

  sessions_.back()->Disconnect();
  Run(kSettleTicks);

  EXPECT_EQ(watcher.GetAuthoritativeState()->players.size(), augusta::protocol::kMaxPlayers - 1);
  EXPECT_FALSE(PositionSeenBy(watcher, leaver).has_value());

  Session& ninth = Join();
  Run(kSettleTicks);

  EXPECT_TRUE(ninth.GetSessionId().has_value());
  EXPECT_FALSE(ninth.GetRefusal().has_value());
  EXPECT_EQ(watcher.GetAuthoritativeState()->players.size(), augusta::protocol::kMaxPlayers);
}

TEST_F(RobustnessTest, AClientThatDropsWithoutClosingKeepsTheServerTickingAndIsRemovedOnceItTimesOut) {
  // Set before the connection exists: the timeout only reaches new connections.
  augusta::networking::SimulateNetworkConditions({.timeout_ms = 500});
  Join();
  Run(kSettleTicks);

  // Every packet is lost from here on, so the client is gone without a word.
  augusta::networking::SimulateNetworkConditions({.loss_percent = 100.0F, .timeout_ms = 500});
  std::size_t players = 1;
  std::uint32_t ticks_run = 0;
  // The transport only notices a silent peer when it asks for a reply and gets none, which
  // on state updates alone (unreliable, so never acknowledged) takes several seconds.
  const auto deadline = std::chrono::steady_clock::now() + kSilentPeerDeadline;
  while (players > 0 && std::chrono::steady_clock::now() < deadline) {
    Step(Forward());
    players = ServerTick().players.size();
    ++ticks_run;
  }
  augusta::networking::SimulateNetworkConditions({});

  EXPECT_EQ(players, 0U);
  EXPECT_GT(ticks_run, 0U);
  // And the server still takes players.
  Session& next = Join();
  Run(kSettleTicks);
  EXPECT_TRUE(next.GetAuthoritativeState().has_value());
  EXPECT_EQ(next.GetAuthoritativeState()->players.size(), 1U);
}

}  // namespace
