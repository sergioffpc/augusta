#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
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

#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/identity.h"
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
#include "wire.h"

// The seam the M3 tickets test through (issue #73): a real server host and a
// real client session, both without a window, a GPU or a wall-clock loop, in
// one process over loopback. The tests drive both sides' network work and
// their ticks by hand.
namespace {

using augusta::harness::Failure;
using augusta::harness::FailureKind;
using augusta::harness::JoinRefusal;
using augusta::harness::Phase;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::identity::SessionId;
using augusta::input::Command;
using augusta::math::Length;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::protocol::SessionIdWire;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::Map;

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);
constexpr float kFixedTick = 1.0F / 60.0F;

// What a test's server runs on: NFR-01's 60 Hz and stamina rules that never
// drain, for a match of one player.
constexpr std::uint8_t kTestTickRate = 60;
constexpr Parameters kTestParameters{};

// The one character every test's server offers and every test's client picks,
// unless a test says otherwise.
constexpr const char* kCharacter = "characters/player";

// The test parameters, for a match of count players.
Parameters WithPlayerCount(std::uint8_t count) {
  Parameters parameters = kTestParameters;
  parameters.player_count = count;
  return parameters;
}

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

// A test server's settings: the test tick rate unless a test says otherwise.
HostConfig TestHostConfig(const Parameters& parameters = kTestParameters, std::uint8_t tick_rate_hz = kTestTickRate) {
  // The script path is the server's own placeholder (scripting::Engine ignores
  // it until Lua is embedded, ADR-0022); point it at a real script then.
  return HostConfig{.tick_rate_hz = tick_rate_hz,
                    .parameters = parameters,
                    .script_path = "scripts/round.lua",
                    .listen = Endpoint{.address = LoopbackAddress()}};
}

// A client of the test server playing character.
SessionConfig TestSessionConfig(const std::string& character = kCharacter) {
  return SessionConfig{.server = Endpoint{.address = LoopbackAddress()}, .character = character};
}

// Init and Shutdown once for the whole process, as in networking_test.cpp.
class SessionEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kSessionEnvironment =
    ::testing::AddGlobalTestEnvironment(new SessionEnvironment);

// A client speaking the protocol by hand, to send what a real one would not.
class RawClient {
 public:
  explicit RawClient(const Endpoint& server) { client_.Connect(server); }

  ~RawClient() { client_.Disconnect(); }

  RawClient(const RawClient&) = delete;
  RawClient& operator=(const RawClient&) = delete;
  RawClient(RawClient&&) = delete;
  RawClient& operator=(RawClient&&) = delete;

  // Runs host and client until the server has admitted this one to the Lobby.
  bool Join(Host& host) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (!accepted_ && std::chrono::steady_clock::now() < deadline) {
      host.PumpNetwork();
      Serve();
      std::this_thread::sleep_for(kPollInterval);
    }
    return accepted_;
  }

  // One round of this client's own network work: asks to join once connected,
  // takes in what has arrived, and reports ReadyWire for each Roster it is sent,
  // as a client that has loaded everyone does.
  void Serve() {
    client_.PumpEvents();
    if (!requested_ && client_.GetState() == ConnectionState::kConnected) {
      Send(augusta::protocol::JoinRequestWire{.engine_version = std::string(augusta::EngineVersion()),
                                              .character = kCharacter});
      requested_ = true;
    }
    Drain();
  }

  [[nodiscard]] bool InMatch() const { return in_match_; }

  void Send(const augusta::protocol::MessageWire& message) { SendPayload(augusta::protocol::Encode(message)); }

  // Sends bytes as they are, whether or not they are a message.
  void SendPayload(const augusta::protocol::BytesWire& payload) {
    client_.Send(payload, augusta::networking::Reliability::kReliable);
  }

  // The Authoritative State updates received since the last call.
  std::vector<augusta::protocol::AuthoritativeStateWire> Receive() {
    client_.PumpEvents();
    return Drain();
  }

 private:
  // Takes in what has arrived, answering each Roster with a ReadyWire, and returns
  // the Authoritative States among it.
  std::vector<augusta::protocol::AuthoritativeStateWire> Drain() {
    std::vector<augusta::protocol::AuthoritativeStateWire> states;
    for (const auto& payload : client_.ReceiveMessages()) {
      const auto message = augusta::protocol::Decode(payload);
      if (!message.has_value()) {
        continue;
      }
      if (std::holds_alternative<augusta::protocol::JoinAcceptedWire>(*message)) {
        accepted_ = true;
      } else if (const auto* lobby = std::get_if<augusta::protocol::LobbyWire>(&*message)) {
        Send(augusta::protocol::ReadyWire{.version = lobby->version});
      } else if (std::holds_alternative<augusta::protocol::MatchStartWire>(*message)) {
        in_match_ = true;
      } else if (const auto* state = std::get_if<augusta::protocol::AuthoritativeStateWire>(&*message)) {
        states.push_back(*state);
      }
    }
    return states;
  }

  augusta::networking::Client client_;
  bool requested_ = false;
  bool accepted_ = false;
  bool in_match_ = false;
};

// Runs host and every one of sessions (and raw, if any) - each reporting it
// has loaded every Roster it is sent, as a client does once it has - and ticks
// host, until all of them are in a match or the deadline passes. Returns
// whether they all are. Ticks as fast as the network allows, so the pause after
// a match (hundreds of ticks) passes in no more than a few seconds.
bool DriveIntoMatch(Host& host, const std::vector<Session*>& sessions, RawClient* raw = nullptr) {
  constexpr auto kTickInterval = std::chrono::milliseconds(1);
  constexpr auto kDeadline = std::chrono::seconds(15);
  std::map<const Session*, std::uint32_t> reported;
  const auto deadline = std::chrono::steady_clock::now() + kDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork();
    bool all_in_match = raw == nullptr || raw->InMatch();
    if (raw != nullptr) {
      raw->Serve();
    }
    for (Session* session : sessions) {
      session->PumpEvents();
      session->ExchangeMessages();
      const auto lobby = session->GetLobby();
      if (lobby.has_value() && session->GetPhase() == Phase::kLobby && reported[session] != lobby->version) {
        session->ReportReady(lobby->version);
        reported[session] = lobby->version;
      }
      all_in_match = all_in_match && session->GetPhase() == Phase::kMatch;
    }
    if (all_in_match) {
      return true;
    }
    host.Tick(kFixedTick);
    std::this_thread::sleep_for(kTickInterval);
  }
  return false;
}

// Runs host's and sessions' network work, without ticking host, until until()
// holds or the deadline passes; returns whether it held.
template <typename Condition>
bool ExchangeUntil(Host& host, const std::vector<Session*>& sessions, Condition until) {
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork();
    for (Session* session : sessions) {
      session->PumpEvents();
      session->ExchangeMessages();
    }
    if (until()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return false;
}

// Runs host's and sessions' network work, without ticking host, for long
// enough that a message that was going to arrive has.
void Settle(Host& host, const std::vector<Session*>& sessions) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  ExchangeUntil(host, sessions, [&] { return std::chrono::steady_clock::now() >= until; });
}

std::vector<Session*> Pointers(const std::vector<std::unique_ptr<Session>>& sessions) {
  std::vector<Session*> pointers;
  pointers.reserve(sessions.size());
  for (const auto& session : sessions) {
    pointers.push_back(session.get());
  }
  return pointers;
}

class SessionTest : public ::testing::Test {
 protected:
  SessionTest()
      : host_(TestHostConfig(), Map{.collision = {}, .spawn_points = {}, .characters = {kCharacter}}),
        session_(TestSessionConfig(), EmptyWorld()) {}

  // Runs both sides' network work until the session reports connected, or
  // the deadline passes.
  bool ConnectSession() {
    session_.Connect();
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork();
      session_.PumpEvents();
      session_.ExchangeMessages();
      if (session_.GetConnectionState() == ConnectionState::kConnected) {
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
  ASSERT_TRUE(DriveIntoMatch(host_, {&session_}));
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

  EXPECT_EQ(session_.GetConnectionState(), ConnectionState::kConnected);
}

// A host whose matches take every player the protocol allows, and however many
// clients a test starts, all driven by hand.
class JoinTest : public ::testing::Test {
 protected:
  JoinTest()
      : host_(TestHostConfig(WithPlayerCount(augusta::protocol::kMaxPlayers)),
              Map{.collision = {}, .spawn_points = {}, .characters = {kCharacter}, .client_pack = ClientPack(1)}) {}

  // A client pack hash told apart by its last byte.
  static augusta::assets::PackHash ClientPack(std::uint8_t last) {
    augusta::assets::PackHash hash{};
    hash.back() = std::byte{last};
    return hash;
  }

  // Starts connecting a new client that presents engine_version, asks to play
  // character and has loaded client_pack (by default the one the host's was cooked with).
  Session& AddClient(const std::string& engine_version = std::string(augusta::EngineVersion()),
                     const std::string& character = kCharacter,
                     const augusta::assets::PackHash& client_pack = ClientPack(1)) {
    sessions_.push_back(std::make_unique<Session>(SessionConfig{.server = Endpoint{.address = LoopbackAddress()},
                                                                .engine_version = engine_version,
                                                                .client_pack = client_pack,
                                                                .character = character},
                                                  EmptyWorld()));
    sessions_.back()->Connect();
    return *sessions_.back();
  }

  // Runs both sides' network work until every client has been answered, or the
  // deadline passes.
  bool WaitForAnswers() {
    return ExchangeUntil(host_, Pointers(sessions_), [&] {
      return std::ranges::all_of(sessions_, [](const auto& session) {
        return session->GetSessionId().has_value() || session->GetRefusal().has_value();
      });
    });
  }

  // Fills the Lobby with kMaxPlayers clients and starts their match.
  void StartAFullMatch() {
    for (std::size_t i = 0; i < augusta::protocol::kMaxPlayers; ++i) {
      AddClient();
    }
    ASSERT_TRUE(WaitForAnswers());
    ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_)));
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
};

TEST_F(JoinTest, AClientWithTheMatchingVersionIsAdmittedWithASessionId) {
  Session& client = AddClient();

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_TRUE(client.GetSessionId().has_value());
  EXPECT_FALSE(client.GetRefusal().has_value());
  EXPECT_EQ(client.GetPhase(), Phase::kLobby);
}

TEST_F(JoinTest, SessionIdsAreUniqueAmongConnectedClients) {
  constexpr int kClients = 3;
  for (int i = 0; i < kClients; ++i) {
    AddClient();
  }

  ASSERT_TRUE(WaitForAnswers());

  std::set<SessionId> ids;
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
  EXPECT_EQ(client.GetPhase(), Phase::kNotAdmitted);
}

TEST_F(JoinTest, ARefusedClientReportsTheRefusalAsItsFailure) {
  Session& client = AddClient("0.0.0-not-the-servers");

  ASSERT_TRUE(WaitForAnswers());

  const auto failure = client.GetFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kRefused);
  EXPECT_EQ(failure->refusal, JoinRefusal::kVersionMismatch);
}

TEST_F(JoinTest, AClientWithAnotherClientPackIsRefusedForThePack) {
  Session& client = AddClient(std::string(augusta::EngineVersion()), kCharacter, ClientPack(2));

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kPackMismatch);
  EXPECT_FALSE(client.GetSessionId().has_value());
}

TEST_F(JoinTest, AClientThatPicksACharacterTheScenarioLacksIsRefusedForIt) {
  Session& client = AddClient(std::string(augusta::EngineVersion()), "characters/nobody");

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kUnknownCharacter);
  EXPECT_FALSE(client.GetSessionId().has_value());
  const auto failure = client.GetFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kRefused);
  EXPECT_EQ(failure->refusal, JoinRefusal::kUnknownCharacter);
}

TEST_F(JoinTest, AWrongVersionIsReportedBeforeAnUnknownCharacter) {
  Session& client = AddClient("0.0.0-not-the-servers", "characters/nobody");

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kVersionMismatch);
}

TEST_F(JoinTest, AnAdmittedClientHasNoFailure) {
  Session& client = AddClient();

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_FALSE(client.GetFailure().has_value());
}

TEST_F(JoinTest, TheNinthClientIsRefusedBecauseTheLobbyIsFull) {
  for (std::size_t i = 0; i < augusta::protocol::kMaxPlayers; ++i) {
    AddClient();
  }
  ASSERT_TRUE(WaitForAnswers());
  for (const auto& session : sessions_) {
    ASSERT_TRUE(session->GetSessionId().has_value());
  }

  Session& ninth = AddClient();
  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(ninth.GetRefusal(), JoinRefusal::kLobbyFull);
  EXPECT_FALSE(ninth.GetSessionId().has_value());
}

TEST_F(JoinTest, AClientThatConnectsDuringAMatchIsRefusedBecauseOneIsInProgress) {
  StartAFullMatch();

  Session& late = AddClient();
  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(late.GetRefusal(), JoinRefusal::kMatchInProgress);
  const auto failure = late.GetFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kRefused);
  EXPECT_NE(augusta::harness::DescribeFailure(*failure).find("in progress"), std::string::npos);
}

// Waiting for the match to end is no use to a client that can never play here.
TEST_F(JoinTest, DuringAMatchAWrongVersionOrCharacterIsStillWhatAClientIsTold) {
  StartAFullMatch();

  Session& wrong_version = AddClient("0.0.0-not-the-servers");
  Session& unknown_character = AddClient(std::string(augusta::EngineVersion()), "characters/nobody");
  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(wrong_version.GetRefusal(), JoinRefusal::kVersionMismatch);
  EXPECT_EQ(unknown_character.GetRefusal(), JoinRefusal::kUnknownCharacter);
}

// M3 exit criteria (issue #84): 8 clients moving, sprinting and changing
// stance stay connected and keep up with the server for a full round's
// worth of ticks. "A full simulated round length" isn't a defined quantity
// yet (the round lifecycle is M5) - kRoundTicks stands in for it. The round
// is a match of eight, started once the Lobby is full (ADR-0043).
//
// Unlike this file's other tests, the loop below is paced to the real 60 Hz
// tick duration (sleep_until, the same pattern ServerRuntime::Run() and
// ClientRuntime's Prediction thread use) rather than run flat out - not a
// blind synchronization sleep, but the actual cadence NFR-01 asks the server
// to sustain, so it's the one thing this soak test needs to model for real.
// Running the 600 ticks with no pacing at all let the CPU race far ahead of
// what GameNetworkingSockets could actually flush over the loopback socket:
// on a fast CI runner the whole loop completed in ~150 ms and the server had
// only acknowledged 13 of 600 ticks by the time the test asserted - not a
// missed-tick bug, just the test not giving the network any real time to
// work in. Pacing to 60 Hz gives it that time throughout, the same as a real
// session would have.
TEST_F(JoinTest, EightClientsMoveSprintAndChangeStanceForARoundWithNoMissedTicks) {
  constexpr int kRoundTicks = 600;             // 10 real seconds at kTestTickRate, paced.
  constexpr std::uint32_t kAckTolerance = 20;  // A few round trips' worth still in flight.
  constexpr std::array<Stance, 3> kStanceCycle = {Stance::kStanding, Stance::kCrouching, Stance::kProne};
  constexpr int kStanceCycleTicks = 150;
  constexpr int kSprintBlockTicks = 100;

  StartAFullMatch();

  std::vector<Vec3> first_position(sessions_.size());
  std::vector<Vec3> last_position(sessions_.size());
  std::vector<std::uint32_t> max_acknowledged(sessions_.size(), 0);

  const auto tick_duration = std::chrono::duration<float>(kFixedTick);
  for (int tick = 0; tick < kRoundTicks; ++tick) {
    const auto tick_start = std::chrono::steady_clock::now();

    host_.Tick(kFixedTick);
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      // A per-client phase offset so 8 players don't all walk in lockstep.
      const float angle = (static_cast<float>(tick) * 0.05F) + static_cast<float>(i);
      Command command{};
      command.movement.direction = Vec3(std::cos(angle), 0.0F, std::sin(angle));
      command.movement.sprint = (tick / kSprintBlockTicks) % 2 == 0;
      command.movement.desired_stance = kStanceCycle.at((tick / kStanceCycleTicks) % kStanceCycle.size());

      const auto state = sessions_[i]->Tick(command, kFixedTick);
      if (tick == 0) {
        first_position[i] = state.local_body.position;
      }
      last_position[i] = state.local_body.position;
    }

    host_.PumpNetwork();
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      sessions_[i]->PumpEvents();
      sessions_[i]->ExchangeMessages();

      ASSERT_FALSE(sessions_[i]->GetFailure().has_value()) << "client " << i << " failed at tick " << tick;
      EXPECT_EQ(sessions_[i]->GetConnectionState(), ConnectionState::kConnected)
          << "client " << i << " dropped at tick " << tick;

      if (const auto authoritative = sessions_[i]->GetAuthoritativeState()) {
        max_acknowledged[i] = std::max(max_acknowledged[i], authoritative->acknowledged_sequence);
      }
    }

    std::this_thread::sleep_until(tick_start +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
  }

  for (std::size_t i = 0; i < sessions_.size(); ++i) {
    EXPECT_GE(max_acknowledged[i] + kAckTolerance, static_cast<std::uint32_t>(kRoundTicks))
        << "client " << i << " fell behind: server acknowledged only " << max_acknowledged[i] << " of " << kRoundTicks
        << " ticks";
    EXPECT_GT(Length(last_position[i] - first_position[i]), 0.5F) << "client " << i << " did not move over the round";
  }
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

// The body a client in a match of its own predicts after kFallTicks idle
// ticks, with collision as the map on both sides.
augusta::prediction::State FallenBody(const std::vector<CollisionMesh>& collision) {
  Host host(TestHostConfig(), Map{.collision = collision, .spawn_points = {}, .characters = {kCharacter}});
  augusta::prediction::World world = EmptyWorld();
  for (const CollisionMesh& mesh : collision) {
    EXPECT_TRUE(world.AddCollisionMesh(mesh).has_value());
  }
  Session session(TestSessionConfig(), std::move(world));
  session.Connect();
  EXPECT_TRUE(DriveIntoMatch(host, {&session}));

  augusta::prediction::State state;
  for (int i = 0; i < kFallTicks; ++i) {
    state = session.Tick(Command{}, kFixedTick);
  }
  return state;
}

TEST(MapSessionTest, ThePredictedBodyRestsOnTheMapsFloor) {
  EXPECT_NEAR(FallenBody({FloorAt(kFloorHeight)}).local_body.position.y, kFloorHeight, 0.2F);
}

TEST(MapSessionTest, WithoutAMapThePredictedBodyKeepsFalling) {
  EXPECT_LT(FallenBody({}).local_body.position.y, kFloorHeight - 5.0F);
}

TEST(MapSessionTest, APredictionWorldRefusesAMapMeshPhysicsRejects) {
  augusta::prediction::World world = EmptyWorld();

  const auto added = world.AddCollisionMesh(CollisionMesh{});

  ASSERT_FALSE(added.has_value());
  EXPECT_EQ(added.error(), augusta::physics::CollisionMeshError::kEmpty);
}

TEST(MapHostTest, AHostAcceptsAMapAndKeepsTicking) {
  Host host(TestHostConfig(), Map{.collision = {FloorAt(0.0F)}, .spawn_points = {}, .characters = {kCharacter}});

  for (int i = 0; i < 10; ++i) {
    host.Tick(kFixedTick);
  }
  SUCCEED();
}

TEST(MapHostTest, AHostRefusesAMapMeshPhysicsRejects) {
  EXPECT_THROW(
      Host(TestHostConfig(), Map{.collision = {CollisionMesh{}}, .spawn_points = {}, .characters = {kCharacter}}),
      std::runtime_error);
}

// A client in a match of its own on a host with flat ground, driven tick by tick.
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
      : host_(TestHostConfig(),
              Map{.collision = std::move(server_map), .spawn_points = {}, .characters = {kCharacter}}),
        session_(TestSessionConfig(), WorldWithFloorAt(kGroundHeight)) {}

  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }

  void SetUp() override {
    session_.Connect();
    ASSERT_TRUE(DriveIntoMatch(host_, {&session_}));
    // Match start puts the player in the world; let it settle on the floor.
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

  // CommandsWire the server never hears, then one that arrives together with them.
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

TEST(RawCommandsTest, CommandsThatAreOutOfOrderOrOutOfRangeAreDroppedWithoutAffectingTheWorld) {
  constexpr auto kNetworkDelay = std::chrono::milliseconds(8);
  Host host(TestHostConfig(), Map{.collision = {FloorAt(-0.5F)}, .spawn_points = {}, .characters = {kCharacter}});
  RawClient raw(Endpoint{.address = LoopbackAddress()});
  ASSERT_TRUE(raw.Join(host));
  ASSERT_TRUE(DriveIntoMatch(host, {}, &raw));

  const auto command = [](std::uint32_t sequence, float yaw = 0.0F, float pitch = 0.0F) {
    augusta::protocol::SequencedCommandWire sequenced{.sequence = sequence};
    sequenced.command.direction = Vec3(1.0F, 0.0F, 0.0F);
    sequenced.command.yaw = yaw;
    sequenced.command.pitch = pitch;
    return sequenced;
  };
  // Numbers the wire can carry and no client produces (a NaN cannot travel:
  // the codec sends it as 0).
  constexpr float kImpossibleYaw = 3.9F;
  constexpr float kImpossiblePitch = 3.0F;
  // 1 and 4 are good; 2 and 3 are numbers no client produces; 6 arrives before 5.
  raw.Send(augusta::protocol::CommandsWire{
      .commands = {command(1), command(2, kImpossibleYaw), command(3, 0.0F, kImpossiblePitch), command(4)}});
  raw.Send(augusta::protocol::CommandsWire{.commands = {command(6)}});
  raw.Send(augusta::protocol::CommandsWire{.commands = {command(5)}});

  std::vector<std::uint32_t> acknowledged;
  for (int i = 0; i < 12; ++i) {
    std::this_thread::sleep_for(kNetworkDelay);
    host.PumpNetwork();
    host.Tick(kFixedTick);
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

  // What a host needs, split the way Host's own constructor wants it: config
  // file/script settings, and the map, separately.
  struct HostSetup {
    HostConfig config;
    Map map;
  };

  explicit LoopbackMatch(HostSetup setup) : host_(setup.config, std::move(setup.map)) {}

  // A host setup for the floor with spawn_points, the parameters and the tick rate.
  static HostSetup OnTheFloor(std::vector<Vec3> spawn_points, const Parameters& parameters = kTestParameters,
                              std::uint8_t tick_rate_hz = kTestTickRate) {
    return HostSetup{
        .config = TestHostConfig(parameters, tick_rate_hz),
        .map =
            Map{.collision = {FloorAt(kFloorY)}, .spawn_points = std::move(spawn_points), .characters = {kCharacter}}};
  }

  // Connects a new client and runs the network until the server has answered
  // it, admitted or not. The client's own stamina rules are none: any it uses
  // came from the server.
  Session& Connect() {
    sessions_.push_back(std::make_unique<Session>(TestSessionConfig(), WorldWithFloorAt(kFloorY)));
    Session& client = *sessions_.back();
    client.Connect();
    ExchangeUntil(host_, Pointers(sessions_),
                  [&] { return client.GetSessionId().has_value() || client.GetRefusal().has_value(); });
    return client;
  }

  // Connects a new client that the server must admit to the Lobby.
  Session& Join() {
    Session& client = Connect();
    EXPECT_TRUE(client.GetSessionId().has_value());
    return client;
  }

  // Has every client report it has loaded each Roster it is sent, and ticks
  // until they are all in a match.
  bool StartMatch() { return DriveIntoMatch(host_, Pointers(sessions_)); }

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

  // Ticks the server once, with the network work around it.
  augusta::simulation::State ServerTick() {
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    auto state = host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    return state;
  }

  // Where the newest state client received puts session, or nullopt if it lists no such player.
  static std::optional<augusta::physics::BodyState> BodySeenBy(const Session& client, SessionId session) {
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

  static std::optional<Vec3> PositionSeenBy(const Session& client, SessionId session) {
    const auto body = BodySeenBy(client, session);
    return body.has_value() ? std::optional<Vec3>(body->position) : std::nullopt;
  }

  // Where client's own player spawned at the start of its last match.
  static Vec3 OwnSpawn(const Session& client) {
    for (const auto& player : client.GetMatchStart().value().players) {
      if (player.session == client.GetSessionId()) {
        return player.spawn;
      }
    }
    ADD_FAILURE() << "this client is not in its own match start";
    return {};
  }

  static Command Forward() {
    Command command;
    command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    return command;
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
  std::map<const Session*, augusta::prediction::State> states_;
};

// A host on the floor, with three spawn points, whose matches start with kPlayers players.
template <std::uint8_t kPlayers>
class MatchOf : public LoopbackMatch {
 protected:
  static std::vector<Vec3> SpawnPoints() {
    return {Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F), Vec3(30.0F, kFloorY, -5.0F)};
  }

  MatchOf() : LoopbackMatch(OnTheFloor(SpawnPoints(), WithPlayerCount(kPlayers))) {}

  // Every Session pointer of sessions_, for the free helpers.
  std::vector<Session*> All() { return Pointers(sessions_); }
};

using SpawnTest = MatchOf<2>;

TEST_F(SpawnTest, TwoClientsInAMatchSpawnAtDifferentSpawnPoints) {
  Session& first = Join();
  Session& second = Join();
  ASSERT_TRUE(StartMatch());

  Run(kSettleTicks);

  const auto first_position = PositionSeenBy(first, *first.GetSessionId());
  const auto second_position = PositionSeenBy(first, *second.GetSessionId());
  ASSERT_TRUE(first_position.has_value() && second_position.has_value());
  EXPECT_NEAR(first_position->x, SpawnPoints()[0].x, 0.1F);
  EXPECT_NEAR(first_position->z, SpawnPoints()[0].z, 0.1F);
  EXPECT_NEAR(second_position->x, SpawnPoints()[1].x, 0.1F);
  EXPECT_NEAR(second_position->z, SpawnPoints()[1].z, 0.1F);
}

TEST_F(SpawnTest, AClientsPredictionStartsAtTheSpawnPointMatchStartGaveIt) {
  Join();
  Session& second = Join();
  ASSERT_TRUE(StartMatch());

  Run(1);

  const augusta::physics::BodyState& body = states_.at(&second).local_body;
  EXPECT_NEAR(body.position.x, SpawnPoints()[1].x, 0.1F);
  EXPECT_NEAR(body.position.z, SpawnPoints()[1].z, 0.1F);
}

TEST_F(SpawnTest, EveryClientIsToldEveryPlayersCharacterAndSpawnPointAtMatchStart) {
  Session& first = Join();
  Session& second = Join();

  ASSERT_TRUE(StartMatch());

  for (const Session* client : {&first, &second}) {
    const auto start = client->GetMatchStart();
    ASSERT_TRUE(start.has_value());
    ASSERT_EQ(start->players.size(), 2U);
    EXPECT_EQ(start->players[0].session, *first.GetSessionId());
    EXPECT_EQ(start->players[0].character, 1U);
    EXPECT_EQ(start->players[0].spawn, SpawnPoints()[0]);
    EXPECT_EQ(start->players[1].session, *second.GetSessionId());
    EXPECT_EQ(start->players[1].character, 1U);
    EXPECT_EQ(start->players[1].spawn, SpawnPoints()[1]);
  }
}

using SpawnWrapTest = MatchOf<4>;

TEST_F(SpawnWrapTest, MorePlayersThanSpawnPointsWrapInsteadOfFailing) {
  for (int i = 0; i < 4; ++i) {
    Join();
  }
  ASSERT_TRUE(StartMatch());

  Run(kSettleTicks);

  Session& last = *sessions_.back();
  EXPECT_EQ(last.GetAuthoritativeState()->players.size(), 4U);
  const auto position = PositionSeenBy(last, *last.GetSessionId());
  ASSERT_TRUE(position.has_value());
  EXPECT_NEAR(position->x, SpawnPoints()[0].x, 0.1F);
}

// A Lobby of three, which a test fills or not (ADR-0043).
using LobbyTest = MatchOf<3>;

TEST_F(LobbyTest, AdmittedClientsAreToldWhoIsInTheLobbyAndWithWhichCharacter) {
  Session& first = Join();
  Session& second = Join();

  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return first.GetLobby().has_value() && first.GetLobby()->roster.size() == 2 && second.GetLobby().has_value();
  }));

  for (const Session* client : {&first, &second}) {
    const auto lobby = client->GetLobby();
    ASSERT_TRUE(lobby.has_value());
    ASSERT_EQ(lobby->roster.size(), 2U);
    EXPECT_EQ(lobby->roster[0].session, *first.GetSessionId());
    EXPECT_EQ(lobby->roster[0].character, 1U);
    EXPECT_EQ(lobby->roster[1].session, *second.GetSessionId());
    EXPECT_EQ(lobby->roster[1].character, 1U);
    EXPECT_EQ(client->GetPhase(), Phase::kLobby);
  }
  EXPECT_EQ(first.GetLobby()->version, second.GetLobby()->version);
}

TEST_F(LobbyTest, TheRosterVersionGrowsOnEveryJoinAndLeave) {
  Session& first = Join();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return first.GetLobby().has_value(); }));
  const auto alone = first.GetLobby()->version;

  Session& second = Join();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return first.GetLobby()->roster.size() == 2; }));
  const auto joined = first.GetLobby()->version;
  const SessionId leaver = *second.GetSessionId();
  second.Disconnect();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return first.GetLobby()->roster.size() == 1; }));

  EXPECT_GT(joined, alone);
  EXPECT_GT(first.GetLobby()->version, joined);
  EXPECT_NE(first.GetLobby()->roster[0].session, leaver);
}

TEST_F(LobbyTest, NoMatchStartsBeforeTheLobbyHoldsThePlayerCountEvenWithEveryoneReady) {
  Join();
  Join();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_,
                               [](const auto& s) { return s->GetLobby() && s->GetLobby()->roster.size() == 2; });
  }));
  for (const auto& session : sessions_) {
    session->ReportReady(session->GetLobby()->version);
  }
  Settle(host_, All());

  for (int i = 0; i < kSettleTicks; ++i) {
    EXPECT_TRUE(ServerTick().players.empty()) << "a body at tick " << i;
  }
  for (const auto& session : sessions_) {
    EXPECT_EQ(session->GetPhase(), Phase::kLobby);
    EXPECT_FALSE(session->GetAuthoritativeState().has_value());
  }
}

TEST_F(LobbyTest, AMatchStartsOnTheTickTheLastClientOfAFullLobbyIsReady) {
  for (int i = 0; i < 3; ++i) {
    Join();
  }
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_,
                               [](const auto& s) { return s->GetLobby() && s->GetLobby()->roster.size() == 3; });
  }));
  sessions_[0]->ReportReady(sessions_[0]->GetLobby()->version);
  sessions_[1]->ReportReady(sessions_[1]->GetLobby()->version);
  Settle(host_, All());
  EXPECT_TRUE(host_.Tick(kFixedTick).players.empty()) << "started with a client not ReadyWire";

  sessions_[2]->ReportReady(sessions_[2]->GetLobby()->version);
  Settle(host_, All());

  EXPECT_EQ(host_.Tick(kFixedTick).players.size(), 3U);
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [](const auto& s) { return s->GetPhase() == Phase::kMatch; });
  }));
}

using ReadyTest = MatchOf<2>;

TEST_F(ReadyTest, ANewcomerMakesTheClientsAlreadyThereNotReadyUntilTheyReportTheNewRoster) {
  Session& first = Join();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return first.GetLobby().has_value(); }));
  const auto before = first.GetLobby()->version;
  first.ReportReady(before);
  Settle(host_, All());

  Session& second = Join();
  ASSERT_TRUE(ExchangeUntil(host_, All(),
                            [&] { return first.GetLobby()->roster.size() == 2 && second.GetLobby().has_value(); }));
  second.ReportReady(second.GetLobby()->version);
  // The harness sends nothing for a Roster older than its newest.
  first.ReportReady(before);
  Settle(host_, All());
  EXPECT_TRUE(host_.Tick(kFixedTick).players.empty()) << "started on a ReadyWire for an older Roster";

  first.ReportReady(first.GetLobby()->version);
  Settle(host_, All());

  EXPECT_EQ(host_.Tick(kFixedTick).players.size(), 2U);
}

TEST_F(ReadyTest, AClientInTheLobbyNeitherPredictsNorSendsCommands) {
  Session& first = Join();
  const augusta::prediction::State idle = first.Tick(Command{}, kFixedTick);
  Run(kSettleTicks, Forward());

  EXPECT_EQ(states_.at(&first).local_body.position, idle.local_body.position);
  EXPECT_FALSE(first.GetAuthoritativeState().has_value());

  Join();
  ASSERT_TRUE(StartMatch());
  constexpr int kSteps = 10;
  Run(kSteps, Forward());
  // Sequences start with the match: nothing was sent from the Lobby.
  EXPECT_LE(first.GetAuthoritativeState()->acknowledged_sequence, static_cast<std::uint32_t>(kSteps));
  EXPECT_GT(states_.at(&first).local_body.position.x, OwnSpawn(first).x);
}

using MidMatchTest = MatchOf<3>;

TEST_F(MidMatchTest, APlayerWhoDisconnectsLeavesTheOthersStateAndTheSimulationAndTheMatchGoesOn) {
  for (int i = 0; i < 3; ++i) {
    Join();
  }
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  Session& watcher = *sessions_.front();
  ASSERT_EQ(watcher.GetAuthoritativeState()->players.size(), 3U);
  const SessionId leaver = *sessions_.back()->GetSessionId();

  sessions_.back()->Disconnect();
  Run(kSettleTicks);

  EXPECT_EQ(watcher.GetAuthoritativeState()->players.size(), 2U);
  EXPECT_FALSE(PositionSeenBy(watcher, leaver).has_value());
  EXPECT_EQ(ServerTick().players.size(), 2U);
  EXPECT_EQ(watcher.GetPhase(), Phase::kMatch);
}

// Two players through a match, back to the Lobby and into the next (ADR-0043).
using MatchCycleTest = MatchOf<2>;

// The ticks of the pause after a match at the test tick rate.
std::uint32_t PauseTicks() {
  return static_cast<std::uint32_t>(
      std::ceil(std::chrono::duration<float>(augusta::server::kMatchPause).count() * kTestTickRate));
}

TEST_F(MatchCycleTest, EndingTheMatchSendsEveryoneBackToTheLobbyUnderANewRoster) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  const auto version = sessions_[0]->GetLobby()->version;

  host_.EndMatch();

  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [](const auto& s) { return s->GetPhase() == Phase::kLobby; });
  }));
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [&](const auto& s) { return s->GetLobby()->version > version; });
  }));
  for (const auto& session : sessions_) {
    EXPECT_EQ(session->GetLobby()->roster.size(), 2U);
  }
}

TEST_F(MatchCycleTest, AfterMatchEndNoBodyIsSimulatedAndNoStateReachesAClient) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks, Forward());

  host_.EndMatch();

  EXPECT_TRUE(ServerTick().players.empty());
  Run(kSettleTicks, Forward());
  for (const auto& session : sessions_) {
    EXPECT_EQ(session->GetPhase(), Phase::kLobby);
    EXPECT_FALSE(session->GetAuthoritativeState().has_value());
  }
}

TEST_F(MatchCycleTest, TheNextMatchStartsExactlyThePauseAfterTheLastEndedAndNotOneTickEarlier) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  const auto version = sessions_[0]->GetLobby()->version;
  host_.EndMatch();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [&](const auto& s) { return s->GetLobby()->version > version; });
  }));
  for (const auto& session : sessions_) {
    session->ReportReady(session->GetLobby()->version);
  }
  Settle(host_, All());

  for (std::uint32_t i = 1; i < PauseTicks(); ++i) {
    ASSERT_TRUE(host_.Tick(kFixedTick).players.empty()) << "started " << PauseTicks() - i << " ticks early";
  }

  EXPECT_EQ(host_.Tick(kFixedTick).players.size(), 2U);
}

TEST_F(MatchCycleTest, PlayersKeepTheirSessionAndCharacterAndTheNextMatchTakesTheNextSpawnPoints) {
  Session& first = Join();
  Session& second = Join();
  ASSERT_TRUE(StartMatch());
  const SessionId first_session = *first.GetSessionId();
  const SessionId second_session = *second.GetSessionId();
  host_.EndMatch();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [](const auto& s) { return s->GetPhase() == Phase::kLobby; });
  }));

  ASSERT_TRUE(StartMatch());
  Run(1);

  EXPECT_EQ(first.GetSessionId(), first_session);
  EXPECT_EQ(second.GetSessionId(), second_session);
  const auto start = first.GetMatchStart();
  ASSERT_EQ(start->players.size(), 2U);
  EXPECT_EQ(start->players[0].session, first_session);
  EXPECT_EQ(start->players[0].character, 1U);
  EXPECT_EQ(start->players[0].spawn, SpawnPoints()[2]);
  EXPECT_EQ(start->players[1].session, second_session);
  EXPECT_EQ(start->players[1].spawn, SpawnPoints()[0]);
  // Each client's prediction starts over where the new match put it.
  EXPECT_NEAR(states_.at(&first).local_body.position.x, SpawnPoints()[2].x, 0.1F);
  EXPECT_NEAR(states_.at(&second).local_body.position.x, SpawnPoints()[0].x, 0.1F);
}

TEST_F(MatchCycleTest, AMatchWhoseLastPlayerLeavesEndsOnItsOwnAndTheLobbyTakesPlayersAgain) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);

  for (const auto& session : sessions_) {
    session->Disconnect();
  }
  Run(kSettleTicks);

  EXPECT_TRUE(ServerTick().players.empty());
  Session& next = Connect();
  EXPECT_TRUE(next.GetSessionId().has_value())
      << "refused: "
      << augusta::harness::DescribeJoinRefusal(next.GetRefusal().value_or(JoinRefusal::kVersionMismatch));
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
    ASSERT_TRUE(StartMatch());
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

// A body and a command travel on grids (ADR-0038), and every number here is
// off them: the direction, the view, and the stamina the rules drain and
// refill. A client that predicts correctly rounds as the server did, so
// reconciliation never has a jump to make for rounding.
TEST_F(StaminaTest, AClientThatPredictsCorrectlyNeverCorrectsForTheRoundingOnTheWire) {
  Command command = Moving(/*sprint=*/true);
  command.movement.direction = Vec3(0.3F, 0.0F, 0.7F);
  command.yaw = 0.123456F;
  command.pitch = -0.0654321F;
  const Vec3 before = states_.at(client_).total_correction;

  Run(120, command);

  EXPECT_EQ(states_.at(client_).total_correction, before);
}

// The server's Parameters come from a script (ADR-0039), loaded the way augustad
// loads the one in its pack. A bar that empties in a second of sprinting and never refills.
class ScriptedParametersTest : public LoopbackMatch {
 protected:
  static augusta::parameters::Parameters LoadScript() {
    const auto loaded = augusta::parameters::Load(
        "local sprint_seconds = 1\n"
        "return {\n"
        "  player_count = 1,\n"
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
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  Command sprint;
  sprint.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  sprint.movement.sprint = true;

  Run(45, sprint);

  // Nothing else depletes a bar this fast and nothing refills it: the client is following the script.
  EXPECT_LT(states_.at(&client).local_body.stamina, 0.7F);
  EXPECT_NEAR(states_.at(&client).local_body.stamina, BodySeenBy(client, *client.GetSessionId())->stamina, 0.1F);
}

// The session a ScriptedServer admits its client under.
constexpr SessionIdWire kScriptedSession{1};

// A server speaking the protocol by hand to one client, to send what a real
// one would not: values that fail the checks, messages out of turn, or bytes
// that are no message.
class ScriptedServer {
 public:
  explicit ScriptedServer(const Endpoint& listen) : server_(listen) {}

  // Connects session and answers its join with parameters, then with a Match
  // start of it alone if start_match, and reports whether the session was
  // admitted within wait.
  bool Admit(Session& session, const Parameters& parameters, std::chrono::milliseconds wait = kPollDeadline,
             bool start_match = true) {
    parameters_ = parameters;
    start_match_ = start_match;
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
      if (!decoded.has_value()) {
        continue;
      }
      if (std::holds_alternative<augusta::protocol::JoinRequestWire>(*decoded)) {
        Send(augusta::protocol::JoinAcceptedWire{.session = kScriptedSession,
                                                 .tick_rate_hz = kTestTickRate,
                                                 .parameters = augusta::server::ToWire(parameters_),
                                                 .character = 1});
        if (start_match_) {
          Send(StartOfAlone());
        }
      } else if (std::holds_alternative<augusta::protocol::CommandsWire>(*decoded)) {
        ++commands_received_;
      } else if (const auto* ready = std::get_if<augusta::protocol::ReadyWire>(&*decoded)) {
        readies_.push_back(ready->version);
      }
    }
  }

  // A Match start of the admitted client alone, at the origin.
  static augusta::protocol::MatchStartWire StartOfAlone() {
    return augusta::protocol::MatchStartWire{.players = {{.spawn = {}, .session = kScriptedSession, .character = 1}}};
  }

  // An Authoritative State of tick listing sessions, each at the origin.
  static augusta::protocol::AuthoritativeStateWire StateOf(std::uint32_t tick,
                                                           const std::vector<SessionIdWire>& sessions) {
    augusta::protocol::AuthoritativeStateWire state{.tick = tick, .acknowledged_sequence = 0, .players = {}};
    for (const SessionIdWire session : sessions) {
      state.players.push_back({.session = session, .body = {}});
    }
    return state;
  }

  void Send(const augusta::protocol::MessageWire& message) { SendPayload(augusta::protocol::Encode(message)); }

  // Sends bytes as they are, whether or not they are a message.
  void SendPayload(const augusta::protocol::BytesWire& payload) {
    server_.Send(*peer_, payload, augusta::networking::Reliability::kReliable);
  }

  // How many CommandsWire messages the client has sent.
  [[nodiscard]] int CommandsReceived() const { return commands_received_; }

  // The Roster versions the client has reported ReadyWire for, in order.
  [[nodiscard]] const std::vector<std::uint32_t>& Readies() const { return readies_; }

 private:
  augusta::networking::Server server_;
  std::optional<augusta::networking::PeerId> peer_;
  Parameters parameters_;
  bool start_match_ = true;
  int commands_received_ = 0;
  std::vector<std::uint32_t> readies_;
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
      : server_(Endpoint{.address = LoopbackAddress()}), session_(TestSessionConfig(), WorldWithFloorAt(0.0F)) {}

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
  Settle();
  ASSERT_EQ(session_.GetPhase(), Phase::kMatch);
  EXPECT_GT(PredictedStaminaAfterSprinting(60), 0.99F);
}

TEST_F(ScriptedServerTest, BytesThatAreNoMessageChangeNothingAndTheClientKeepsRunning) {
  // A Lobby cut short, and a type nobody has.
  for (const auto& payload :
       {augusta::protocol::BytesWire{std::byte{6}, std::byte{2}}, augusta::protocol::BytesWire{std::byte{0xFF}}}) {
    server_.SendPayload(payload);
    Settle();
  }

  EXPECT_FLOAT_EQ(session_.GetParameters()->stamina.deplete_per_second, 0.0F);
  EXPECT_EQ(session_.GetPhase(), Phase::kMatch);
  EXPECT_GT(PredictedStaminaAfterSprinting(60), 0.99F);
}

TEST_F(ScriptedServerTest, AStateNamingAPlayerNotInTheMatchIsDropped) {
  Settle();

  server_.Send(ScriptedServer::StateOf(1, {kScriptedSession, SessionIdWire{99}}));
  Settle();
  EXPECT_FALSE(session_.GetAuthoritativeState().has_value());

  server_.Send(ScriptedServer::StateOf(2, {kScriptedSession}));
  Settle();
  EXPECT_TRUE(session_.GetAuthoritativeState().has_value());
}

TEST_F(ScriptedServerTest, AStateThatArrivesAfterMatchEndIsDroppedAndTheClientIsBackInTheLobby) {
  server_.Send(ScriptedServer::StateOf(1, {kScriptedSession}));
  Settle();
  ASSERT_TRUE(session_.GetAuthoritativeState().has_value());

  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ScriptedServer::StateOf(2, {kScriptedSession}));
  Settle();

  EXPECT_EQ(session_.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(session_.GetAuthoritativeState().has_value());
}

TEST_F(ScriptedServerTest, AfterMatchEndTheClientStopsPredictingAndSendingCommands) {
  Settle();
  PredictedStaminaAfterSprinting(10);
  server_.Send(augusta::protocol::MatchEndWire{});
  Settle();
  const int sent_in_the_match = server_.CommandsReceived();
  ASSERT_GT(sent_in_the_match, 0);
  const augusta::prediction::State last = session_.Tick(Command{}, kFixedTick);

  Command sprint;
  sprint.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  const augusta::prediction::State after = session_.Tick(sprint, kFixedTick);
  Settle();

  EXPECT_EQ(after.local_body.position, last.local_body.position);
  EXPECT_EQ(server_.CommandsReceived(), sent_in_the_match);
}

// A client the scripted server admits to the Lobby and starts no match for.
class ScriptedLobbyTest : public ScriptedServerTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(server_.Admit(session_, WithDeplete(0.0F), kPollDeadline, /*start_match=*/false));
  }
};

TEST_F(ScriptedLobbyTest, AStateThatArrivesBeforeMatchStartIsDropped) {
  server_.Send(ScriptedServer::StateOf(1, {kScriptedSession}));
  Settle();
  EXPECT_EQ(session_.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(session_.GetAuthoritativeState().has_value());

  server_.Send(ScriptedServer::StartOfAlone());
  server_.Send(ScriptedServer::StateOf(2, {kScriptedSession}));
  Settle();
  EXPECT_EQ(session_.GetPhase(), Phase::kMatch);
  EXPECT_TRUE(session_.GetAuthoritativeState().has_value());
}

TEST_F(ScriptedLobbyTest, AMatchStartThatLeavesThisClientOutIsDropped) {
  server_.Send(
      augusta::protocol::MatchStartWire{.players = {{.spawn = {}, .session = SessionIdWire{2}, .character = 1}}});
  Settle();

  EXPECT_EQ(session_.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(session_.GetMatchStart().has_value());
}

TEST_F(ScriptedLobbyTest, NothingIsSentBeforeMatchStartAndTheFirstTickInAMatchStartsAtItsSpawnPoint) {
  Command walk;
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  for (int i = 0; i < 10; ++i) {
    session_.Tick(walk, kFixedTick);
  }
  Settle();
  EXPECT_EQ(server_.CommandsReceived(), 0);

  const Vec3 spawn(5.0F, 0.0F, -3.0F);
  server_.Send(
      augusta::protocol::MatchStartWire{.players = {{.spawn = spawn, .session = kScriptedSession, .character = 1}}});
  Settle();
  const augusta::prediction::State first = session_.Tick(Command{}, kFixedTick);
  Settle();

  EXPECT_NEAR(first.local_body.position.x, spawn.x, 0.1F);
  EXPECT_NEAR(first.local_body.position.z, spawn.z, 0.1F);
  EXPECT_GT(server_.CommandsReceived(), 0);
}

TEST_F(ScriptedLobbyTest, ReadyIsSentOnlyWhenToldAndOnlyForTheNewestRoster) {
  server_.Send(augusta::protocol::LobbyWire{.version = 1, .roster = {{.session = kScriptedSession, .character = 1}}});
  server_.Send(augusta::protocol::LobbyWire{
      .version = 2,
      .roster = {{.session = kScriptedSession, .character = 1}, {.session = SessionIdWire{2}, .character = 1}}});
  Settle();
  ASSERT_EQ(session_.GetLobby()->version, 2U);
  EXPECT_TRUE(server_.Readies().empty()) << "sent ReadyWire on its own";

  session_.ReportReady(1);
  session_.ReportReady(2);
  Settle();

  EXPECT_EQ(server_.Readies(), std::vector<std::uint32_t>{2});
}

// A client takes the parameters it joins with as the server's, so values that
// fail the range checks make it drop the Join accepted rather than predict on them.
TEST(InvalidParametersTest, AClientDropsAJoinAcceptedWhoseParametersFailTheRangeChecks) {
  ScriptedServer server(Endpoint{.address = LoopbackAddress()});
  Parameters threshold_of_one;
  threshold_of_one.stamina.forced_walk_below = 1.0F;
  for (const Parameters& bad :
       {Parameters{.stamina = {.deplete_per_second = -1.0F}},
        Parameters{.stamina = {.regen_per_second = std::numeric_limits<float>::quiet_NaN()}}, threshold_of_one,
        Parameters{.player_count = 0}, Parameters{.player_count = augusta::protocol::kMaxPlayers + 1}}) {
    // One server for every case: it answers whichever session sent last, and a
    // server bound anew each time would find the port not yet released.
    Session session(TestSessionConfig(), EmptyWorld());

    EXPECT_FALSE(server.Admit(session, bad, std::chrono::milliseconds(500)));
    EXPECT_FALSE(session.GetParameters().has_value());
  }
}

// A server at 30 Hz: everything a client does with time it must take from what it is told.
class TickRateTest : public LoopbackMatch {
 protected:
  static constexpr std::uint8_t kServerRate = 30;

  TickRateTest() : LoopbackMatch(OnTheFloor({}, kTestParameters, kServerRate)) {}
};

TEST_F(TickRateTest, AClientLearnsTheServersTickRateWhenItJoins) {
  Session& client = Join();

  ASSERT_TRUE(client.GetTickRate().has_value());
  EXPECT_EQ(*client.GetTickRate(), kServerRate);
}

TEST_F(TickRateTest, AClientTickingAtTheRateItWasToldAgreesWithTheServerWithoutCorrection) {
  Session& client = Join();
  ASSERT_TRUE(StartMatch());
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
  ASSERT_TRUE(ExchangeUntil(host_, {&session_}, [&] { return session_.GetSessionId().has_value(); }));

  ASSERT_TRUE(session_.GetParameters().has_value());
  ASSERT_TRUE(session_.GetTickRate().has_value());
  EXPECT_EQ(*session_.GetTickRate(), kTestTickRate);
}

// A server whose tick rate is unusable (zero): the client drops the Join
// accepted rather than divide by it.
TEST(InvalidParametersTest, AClientDropsAJoinAcceptedWhoseTickRateFailsTheChecks) {
  Host host(TestHostConfig(kTestParameters, 0), Map{.collision = {}, .spawn_points = {}, .characters = {kCharacter}});
  Session session(TestSessionConfig(), EmptyWorld());
  session.Connect();

  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  ExchangeUntil(host, {&session}, [&] { return std::chrono::steady_clock::now() >= until; });

  EXPECT_FALSE(session.GetSessionId().has_value());
  EXPECT_FALSE(session.GetTickRate().has_value());
  EXPECT_FALSE(session.GetParameters().has_value());
}

// What a client is told when its session ends on its own.
TEST(SessionFailureTest, ASessionThatNeverConnectedHasNoFailure) {
  Session session(TestSessionConfig(), EmptyWorld());

  EXPECT_FALSE(session.GetFailure().has_value());
}

TEST(SessionFailureTest, AServerNobodyIsListeningAtIsUnreachable) {
  // Set before connecting: the timeout only reaches new connections.
  augusta::networking::SimulateNetworkConditions({.timeout_ms = 500});
  Session session(TestSessionConfig(), EmptyWorld());
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

TEST(SessionFailureTest, AServerThatGoesAwayDuringAMatchIsAConnectionLost) {
  auto host =
      std::make_unique<Host>(TestHostConfig(), Map{.collision = {}, .spawn_points = {}, .characters = {kCharacter}});
  Session session(TestSessionConfig(), EmptyWorld());
  session.Connect();
  ASSERT_TRUE(DriveIntoMatch(*host, {&session}));
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
  Host host(TestHostConfig(), Map{.collision = {}, .spawn_points = {}, .characters = {kCharacter}});
  Session session(TestSessionConfig(), EmptyWorld());
  session.Connect();
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (session.GetConnectionState() != ConnectionState::kConnected && std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork();
    session.PumpEvents();
    std::this_thread::sleep_for(kPollInterval);
  }
  ASSERT_EQ(session.GetConnectionState(), ConnectionState::kConnected);

  session.Disconnect();
  session.PumpEvents();

  EXPECT_FALSE(session.GetFailure().has_value());
}

TEST(SessionFailureTest, EveryFailureIsDescribedForThePlayerAndARefusalSaysWhy) {
  const std::string unreachable = augusta::harness::DescribeFailure({.kind = FailureKind::kServerUnreachable});
  const std::string lost = augusta::harness::DescribeFailure({.kind = FailureKind::kConnectionLost});
  const std::string full =
      augusta::harness::DescribeFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kLobbyFull});
  const std::string version =
      augusta::harness::DescribeFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kVersionMismatch});
  const std::string in_progress =
      augusta::harness::DescribeFailure({.kind = FailureKind::kRefused, .refusal = JoinRefusal::kMatchInProgress});

  EXPECT_FALSE(unreachable.empty());
  EXPECT_FALSE(lost.empty());
  EXPECT_NE(unreachable, lost);
  EXPECT_NE(full, version);
  EXPECT_NE(full, in_progress);
  EXPECT_NE(full.find(std::string(augusta::harness::DescribeJoinRefusal(JoinRefusal::kLobbyFull))), std::string::npos)
      << full;
}

// The server against peers that misbehave or leave.
template <std::uint8_t kPlayers>
class RobustnessOf : public MatchOf<kPlayers> {
 protected:
  static constexpr auto kSilentPeerDeadline = std::chrono::seconds(20);

  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }
};

using RobustnessTest = RobustnessOf<1>;
using GarbageTest = RobustnessOf<2>;
using FullMatchRobustnessTest = RobustnessOf<augusta::protocol::kMaxPlayers>;

TEST_F(GarbageTest, GarbageFromAPeerIsDroppedAndTheMatchAndTheOtherClientsAreUnaffected) {
  Session& bystander = Join();
  RawClient raw(Endpoint{.address = LoopbackAddress()});
  ASSERT_TRUE(raw.Join(host_));
  ASSERT_TRUE(DriveIntoMatch(host_, All(), &raw));
  Run(kSettleTicks);

  using augusta::protocol::BytesWire;
  BytesWire truncated_state = augusta::protocol::Encode(augusta::protocol::AuthoritativeStateWire{.players = {{}}});
  truncated_state.resize(truncated_state.size() / 2);
  BytesWire not_for_the_server = augusta::protocol::Encode(augusta::protocol::AuthoritativeStateWire{});
  BytesWire commands_with_trailing_bytes = augusta::protocol::Encode(augusta::protocol::CommandsWire{});
  commands_with_trailing_bytes.push_back(std::byte{7});
  const BytesWire garbage[] = {
      BytesWire{},
      BytesWire{std::byte{0}},
      BytesWire{std::byte{0xFF}, std::byte{1}, std::byte{2}},
      truncated_state,
      not_for_the_server,
      commands_with_trailing_bytes,
      augusta::protocol::Encode(augusta::protocol::ReadyWire{.version = 12345}),
      BytesWire(64 * 1024, std::byte{0xAB}),
      BytesWire(1000, std::byte{static_cast<unsigned char>(augusta::protocol::MessageTypeWire::kCommands)}),
  };
  for (const BytesWire& payload : garbage) {
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
  EXPECT_GT(BodySeenBy(bystander, *bystander.GetSessionId())->position.x, SpawnPoints()[0].x + 1.0F);
  for (const auto& player : after->players) {
    EXPECT_TRUE(std::isfinite(player.body.position.x) && std::isfinite(player.body.position.y));
  }
  EXPECT_EQ(bystander.GetConnectionState(), ConnectionState::kConnected);
}

TEST_F(FullMatchRobustnessTest, AfterAClientDisconnectsItsPlayerIsAbsentFromOthersStateAndNoOneTakesItsPlace) {
  for (std::size_t i = 0; i < augusta::protocol::kMaxPlayers; ++i) {
    Join();
  }
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  Session& watcher = *sessions_.front();
  ASSERT_EQ(watcher.GetAuthoritativeState()->players.size(), augusta::protocol::kMaxPlayers);
  const auto leaver = *sessions_.back()->GetSessionId();

  sessions_.back()->Disconnect();
  Run(kSettleTicks);

  EXPECT_EQ(watcher.GetAuthoritativeState()->players.size(), augusta::protocol::kMaxPlayers - 1);
  EXPECT_FALSE(PositionSeenBy(watcher, leaver).has_value());

  Session& ninth = Connect();
  Run(kSettleTicks);

  EXPECT_EQ(ninth.GetRefusal(), JoinRefusal::kMatchInProgress);
  EXPECT_EQ(watcher.GetAuthoritativeState()->players.size(), augusta::protocol::kMaxPlayers - 1);
}

TEST_F(RobustnessTest, AClientThatDropsWithoutClosingKeepsTheServerTickingAndIsRemovedOnceItTimesOut) {
  // Set before the connection exists: the timeout only reaches new connections.
  augusta::networking::SimulateNetworkConditions({.timeout_ms = 500});
  Join();
  ASSERT_TRUE(StartMatch());
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
  // Its match ended with it, and the next starts with whoever comes.
  Session& next = Join();
  ASSERT_TRUE(DriveIntoMatch(host_, {&next}));
  Run(kSettleTicks);
  EXPECT_TRUE(next.GetAuthoritativeState().has_value());
  EXPECT_EQ(next.GetAuthoritativeState()->players.size(), 1U);
}

}  // namespace
