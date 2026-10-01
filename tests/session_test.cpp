#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
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
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "augusta/weapon.h"
#include "host.h"
#include "match.h"
#include "parameters_loader.h"
#include "wire.h"

// The seam the M3 tickets test through (issue #73): a real server host and a
// real client session, both without a window, a GPU or a wall-clock loop, in
// one process over loopback. The tests drive both sides' network work and
// their ticks by hand.
namespace {

using augusta::command::Command;
using augusta::harness::Death;
using augusta::harness::EntityId;
using augusta::harness::Failure;
using augusta::harness::FailureKind;
using augusta::harness::HitConfirmation;
using augusta::harness::JoinRefusal;
using augusta::harness::Phase;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::harness::SessionId;
using augusta::harness::Shot;
using augusta::math::Length;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::protocol::EntityIdWire;
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
const Parameters kTestParameters{};

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
  return HostConfig{
      .tick_rate_hz = tick_rate_hz, .parameters = parameters, .listen = Endpoint{.address = LoopbackAddress()}};
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
      : host_(TestHostConfig(),
              Map{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}}),
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
              Map{.collision = {},
                  .spawn_points = {},
                  .characters = {{.path = kCharacter, .hitboxes = {}}},
                  .client_pack = ClientPack(1)}) {}

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
  Host host(TestHostConfig(),
            Map{.collision = collision, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
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
  Host host(
      TestHostConfig(),
      Map{.collision = {FloorAt(0.0F)}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});

  for (int i = 0; i < 10; ++i) {
    host.Tick(kFixedTick);
  }
  SUCCEED();
}

TEST(MapHostTest, AHostRefusesACharacterHitboxThatIsNotAWholeTriangleList) {
  // Three points and an index past them.
  const augusta::assets::HitboxData hitbox{
      .part = augusta::assets::BodyPart::kTorso,
      .mesh = {.points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)},
               .indices = {0, 1, 3}}};

  EXPECT_THROW(
      Host(TestHostConfig(),
           Map{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {hitbox}}}}),
      std::runtime_error);
}

TEST(MapHostTest, AHostRefusesAMapMeshPhysicsRejects) {
  EXPECT_THROW(Host(TestHostConfig(), Map{.collision = {CollisionMesh{}},
                                          .spawn_points = {},
                                          .characters = {{.path = kCharacter, .hitboxes = {}}}}),
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
      : host_(TestHostConfig(), Map{.collision = std::move(server_map),
                                    .spawn_points = {},
                                    .characters = {{.path = kCharacter, .hitboxes = {}}}}),
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
      for (const auto& body : state->bodies) {
        if (body.entity == *session_.GetEntityId()) {
          return body.body;
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

// A client whose clock runs at another rate than the server's, pacing its ticks
// by the queue depth each Authoritative State update tells it (ADR-0038). Time is
// simulated: the server ticks every kFixedTick, the client every paced tick of
// its own clock, and every message is delivered before the next tick of either.
class PacingTest : public MovementTest {
 protected:
  // What the client learned from one server tick.
  struct Told {
    std::uint8_t queued_commands = 0;
    std::uint32_t acknowledged_sequence = 0;
  };

  // Runs the match for ticks server ticks with a client whose clock runs
  // clock_rate times as fast as the server's; returns what each tick told it.
  std::vector<Told> RunPaced(double clock_rate, int ticks) {
    const auto nominal =
        std::chrono::duration_cast<augusta::tick::Clock::duration>(std::chrono::duration<double>(kFixedTick));
    const auto self = static_cast<augusta::server::SessionId>(std::to_underlying(*session_.GetSessionId()));
    std::chrono::duration<double> server_next = nominal;
    std::chrono::duration<double> client_next{};
    std::vector<Told> told;
    while (std::cmp_less(told.size(), ticks)) {
      if (client_next <= server_next) {
        const std::size_t queued = host_.QueuedCommands(self);
        session_.Tick(Walking(), kFixedTick);
        DeliverUntil(
            [&] { return host_.QueuedCommands(self) > queued || queued == augusta::server::kMaxQueuedCommands; });
        client_next +=
            augusta::tick::PacedTickDuration(nominal, session_.GetAuthoritativeState()->queued_commands) / clock_rate;
      } else {
        const std::uint32_t last_tick = session_.GetAuthoritativeState()->tick;
        host_.Tick(kFixedTick);
        DeliverUntil([&] { return session_.GetAuthoritativeState()->tick > last_tick; });
        const auto state = session_.GetAuthoritativeState();
        told.push_back(
            {.queued_commands = state->queued_commands, .acknowledged_sequence = state->acknowledged_sequence});
        server_next += nominal;
      }
    }
    return told;
  }

  // Runs both sides' network work until delivered() holds: what was just sent has
  // arrived. Polls often, since the run waits on it twice a tick.
  template <typename Condition>
  void DeliverUntil(Condition delivered) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork();
      session_.PumpEvents();
      session_.ExchangeMessages();
      if (delivered()) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ADD_FAILURE() << "a message was not delivered";
  }

  // Past a second of settling, the server holds one or two of the client's
  // commands after every tick, and consumes exactly one per tick.
  static void ExpectPacedOnTarget(const std::vector<Told>& told) {
    constexpr std::size_t kSettling = 60;
    for (std::size_t i = kSettling; i < told.size(); ++i) {
      EXPECT_GE(told[i].queued_commands, 1U) << "tick " << i;
      EXPECT_LE(told[i].queued_commands, 2U) << "tick " << i;
      EXPECT_EQ(told[i].acknowledged_sequence, told[i - 1].acknowledged_sequence + 1) << "tick " << i;
    }
  }

  static constexpr int kPacedTicks = 240;  // Four seconds at 60 Hz.
};

TEST_F(PacingTest, AClientWhoseClockRunsTwoPercentFastKeepsTheServersQueueOfItsCommandsShort) {
  ExpectPacedOnTarget(RunPaced(1.02, kPacedTicks));
}

TEST_F(PacingTest, AClientWhoseClockRunsTwoPercentSlowKeepsTheServerFromRunningOutOfItsCommands) {
  ExpectPacedOnTarget(RunPaced(0.98, kPacedTicks));
}

TEST(RawCommandsTest, CommandsThatAreOutOfOrderOrOutOfRangeAreDroppedWithoutAffectingTheWorld) {
  constexpr auto kNetworkDelay = std::chrono::milliseconds(8);
  Host host(
      TestHostConfig(),
      Map{.collision = {FloorAt(-0.5F)}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
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
      for (const auto& body : state.bodies) {
        EXPECT_TRUE(std::isfinite(body.body.position.x) && std::isfinite(body.body.position.y));
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
  // How long Step waits for the commands it sent to reach the server: far more
  // than loopback delivery takes, even under the sanitizers.
  static constexpr auto kStepPatience = std::chrono::milliseconds(250);

  // What a host needs, split the way Host's own constructor wants it: config
  // file/script settings, the map and the scenario's Game policy, separately.
  struct HostSetup {
    HostConfig config;
    Map map;
    augusta::scripting::Engine policy;
  };

  explicit LoopbackMatch(HostSetup setup) : host_(setup.config, std::move(setup.map), std::move(setup.policy)) {}

  // A host setup for the floor with spawn_points, the parameters and the tick rate.
  static HostSetup OnTheFloor(std::vector<Vec3> spawn_points, const Parameters& parameters = kTestParameters,
                              std::uint8_t tick_rate_hz = kTestTickRate) {
    return HostSetup{.config = TestHostConfig(parameters, tick_rate_hz),
                     .map = Map{.collision = {FloorAt(kFloorY)},
                                .spawn_points = std::move(spawn_points),
                                .characters = {{.path = kCharacter, .hitboxes = {}}}},
                     .policy = {}};
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

  // One tick of the whole match: every client predicts and sends its command
  // of commands (one per client, in the order they connected), the server ticks
  // once every command a connected client in the match sent has reached it (or
  // that client has since heard its match end, or kStepPatience has passed),
  // the states come back. Returns the server's. Waiting for the commands rather
  // than for a fixed time keeps a slow run (the sanitizers build) from ticking
  // the server before a command arrives, which would hold that player's last
  // movement (ADR-0038) and turn into a correction no real mismatch caused. The
  // patience bounds a test that loses commands on purpose.
  augusta::simulation::State StepEach(const std::vector<Command>& commands) {
    std::vector<std::pair<const Session*, augusta::server::SessionId>> sending;
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      const auto& session = sessions_[i];
      if (session->GetPhase() == Phase::kMatch && session->GetConnectionState() == ConnectionState::kConnected) {
        sending.emplace_back(session.get(),
                             static_cast<augusta::server::SessionId>(std::to_underlying(*session->GetSessionId())));
      }
      states_[session.get()] = session->Tick(commands.at(i), kFixedTick);
    }
    const auto give_up = std::chrono::steady_clock::now() + kStepPatience;
    ExchangeUntil(host_, Pointers(sessions_), [&] {
      return std::chrono::steady_clock::now() >= give_up || std::ranges::all_of(sending, [&](const auto& sent) {
               return host_.QueuedCommands(sent.second) > 0 || sent.first->GetPhase() != Phase::kMatch;
             });
    });
    augusta::simulation::State state = host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    return state;
  }

  // A StepEach on which every client does command.
  augusta::simulation::State Step(const Command& command = Command{}) {
    return StepEach(std::vector<Command>(sessions_.size(), command));
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

  // Where the newest state client received puts entity, or nullopt if it lists no such body.
  static std::optional<augusta::physics::BodyState> BodySeenBy(const Session& client, EntityId entity) {
    const auto state = client.GetAuthoritativeState();
    if (state.has_value()) {
      for (const auto& body : state->bodies) {
        if (body.entity == entity) {
          return body.body;
        }
      }
    }
    return std::nullopt;
  }

  static std::optional<Vec3> PositionSeenBy(const Session& client, EntityId entity) {
    const auto body = BodySeenBy(client, entity);
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

TEST_F(SpawnTest, EachClientIsToldTheBodyItControlsAndSeesEveryBodyByIt) {
  Session& first = Join();
  Session& second = Join();
  ASSERT_TRUE(StartMatch());

  Run(kSettleTicks);

  ASSERT_TRUE(first.GetEntityId().has_value() && second.GetEntityId().has_value());
  EXPECT_NE(*first.GetEntityId(), *second.GetEntityId());
  EXPECT_TRUE(PositionSeenBy(first, *second.GetEntityId()).has_value());
  EXPECT_TRUE(PositionSeenBy(second, *first.GetEntityId()).has_value());
}

TEST_F(SpawnTest, TwoClientsInAMatchSpawnAtDifferentSpawnPoints) {
  Session& first = Join();
  Session& second = Join();
  ASSERT_TRUE(StartMatch());

  Run(kSettleTicks);

  const auto first_position = PositionSeenBy(first, *first.GetEntityId());
  const auto second_position = PositionSeenBy(first, *second.GetEntityId());
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

// Three players on the floor, whose scenario's behaviours.lua hands the Map's
// three Spawn points out backwards: the last player in the Match takes the first.
class PolicySpawnTest : public LoopbackMatch {
 protected:
  static std::vector<Vec3> SpawnPoints() {
    return {Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F), Vec3(30.0F, kFloorY, -5.0F)};
  }

  static augusta::scripting::Engine Backwards() {
    auto policy = augusta::scripting::Engine::Load({.objectives = std::nullopt, .behaviours = R"(
      function assign_spawns(match)
        local assignment = {}
        for i, player in ipairs(match.players) do
          assignment[i] = {session = player.session, spawn_point = match.spawn_points - i + 1}
        end
        return assignment
      end
    )"});
    EXPECT_TRUE(policy.has_value());
    return policy ? *std::move(policy) : augusta::scripting::Engine{};
  }

  static HostSetup Setup() {
    HostSetup setup = OnTheFloor(SpawnPoints(), WithPlayerCount(3));
    setup.policy = Backwards();
    return setup;
  }

  PolicySpawnTest() : LoopbackMatch(Setup()) {}
};

TEST_F(PolicySpawnTest, EveryClientIsToldTheDistinctSpawnPointPolicyGaveEachPlayer) {
  for (int i = 0; i < 3; ++i) {
    Join();
  }

  ASSERT_TRUE(StartMatch());

  for (const auto& client : sessions_) {
    const auto start = client->GetMatchStart();
    ASSERT_TRUE(start.has_value());
    ASSERT_EQ(start->players.size(), 3U);
    EXPECT_EQ(start->players[0].spawn, SpawnPoints()[2]);
    EXPECT_EQ(start->players[1].spawn, SpawnPoints()[1]);
    EXPECT_EQ(start->players[2].spawn, SpawnPoints()[0]);
  }
}

TEST_F(PolicySpawnTest, TheFirstAuthoritativeStatePlacesEachBodyAtItsSpawnPointAtFullHealth) {
  for (int i = 0; i < 3; ++i) {
    Join();
  }
  ASSERT_TRUE(StartMatch());

  const augusta::simulation::State first = ServerTick();

  ASSERT_EQ(first.bodies.size(), 3U);
  for (const auto& client : sessions_) {
    const auto state = client->GetAuthoritativeState();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->health, kTestParameters.starting_health);
    const Vec3 spawn = OwnSpawn(*client);
    const auto position = PositionSeenBy(*client, *client->GetEntityId());
    ASSERT_TRUE(position.has_value());
    EXPECT_NEAR(position->x, spawn.x, 0.1F);
    EXPECT_NEAR(position->z, spawn.z, 0.1F);
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
  EXPECT_EQ(last.GetAuthoritativeState()->bodies.size(), 4U);
  const auto position = PositionSeenBy(last, *last.GetEntityId());
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
    EXPECT_TRUE(ServerTick().bodies.empty()) << "a body at tick " << i;
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
  EXPECT_TRUE(host_.Tick(kFixedTick).bodies.empty()) << "started with a client not ReadyWire";

  sessions_[2]->ReportReady(sessions_[2]->GetLobby()->version);
  Settle(host_, All());

  EXPECT_EQ(host_.Tick(kFixedTick).bodies.size(), 3U);
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
  EXPECT_TRUE(host_.Tick(kFixedTick).bodies.empty()) << "started on a ReadyWire for an older Roster";

  first.ReportReady(first.GetLobby()->version);
  Settle(host_, All());

  EXPECT_EQ(host_.Tick(kFixedTick).bodies.size(), 2U);
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
  ASSERT_EQ(watcher.GetAuthoritativeState()->bodies.size(), 3U);
  const EntityId leaver = *sessions_.back()->GetEntityId();

  sessions_.back()->Disconnect();
  Run(kSettleTicks);

  EXPECT_EQ(watcher.GetAuthoritativeState()->bodies.size(), 2U);
  EXPECT_FALSE(PositionSeenBy(watcher, leaver).has_value());
  EXPECT_EQ(ServerTick().bodies.size(), 2U);
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

  EXPECT_TRUE(ServerTick().bodies.empty());
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
    ASSERT_TRUE(host_.Tick(kFixedTick).bodies.empty()) << "started " << PauseTicks() - i << " ticks early";
  }

  EXPECT_EQ(host_.Tick(kFixedTick).bodies.size(), 2U);
}

TEST_F(MatchCycleTest, PlayersKeepTheirSessionAndCharacterAndTheNextMatchHandsOutTheSpawnPointsAfresh) {
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
  EXPECT_EQ(start->players[0].spawn, SpawnPoints()[0]);
  EXPECT_EQ(start->players[1].session, second_session);
  EXPECT_EQ(start->players[1].spawn, SpawnPoints()[1]);
  // Each client's prediction starts over where the new match put it.
  EXPECT_NEAR(states_.at(&first).local_body.position.x, SpawnPoints()[0].x, 0.1F);
  EXPECT_NEAR(states_.at(&second).local_body.position.x, SpawnPoints()[1].x, 0.1F);
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

  EXPECT_TRUE(ServerTick().bodies.empty());
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
    return BodySeenBy(*client_, *client_->GetEntityId()).value();
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

TEST_F(StaminaTest, SustainedSprintRunsStaminaOutAndTheServerForcesAWalkUntilItRecoversAboveTheThreshold) {
  const float fresh_speed = MeanSpeedOver(10, Moving(/*sprint=*/true));
  // A second of sprinting runs the bar out.
  Run(55, Moving(/*sprint=*/true));
  ASSERT_TRUE(Authoritative().exhausted);

  // Every tick of the exhaustion is walked, sprint held or not, until the bar
  // is back above the threshold: 0.2 of it at 0.25 a second is 48 ticks.
  int walked = 0;
  for (; walked < 120 && Authoritative().exhausted; ++walked) {
    Step(Moving(/*sprint=*/true));
    EXPECT_NEAR(Speed(Authoritative()), kWalkSpeed, 0.2F) << "tick " << walked << " of the walk";
  }
  EXPECT_GT(walked, 40);
  EXPECT_GT(Authoritative().stamina, kForcedWalkBelow);

  EXPECT_NEAR(fresh_speed, kSprintSpeed, 0.3F);
  EXPECT_NEAR(MeanSpeedOver(5, Moving(/*sprint=*/true)), kSprintSpeed, 0.3F);
}

// US-05: the client predicts the forced walk exactly as the server applies it.
TEST_F(StaminaTest, AClientThatSprintsToExhaustionAndOnThroughRecoveryNeverCorrects) {
  const Vec3 before = states_.at(client_).total_correction;
  bool was_exhausted = false;
  bool recovered = false;

  for (int i = 0; i < 180; ++i) {
    Step(Moving(/*sprint=*/true));
    const bool exhausted = states_.at(client_).local_body.exhausted;
    recovered = recovered || (was_exhausted && !exhausted);
    was_exhausted = was_exhausted || exhausted;
  }

  ASSERT_TRUE(was_exhausted);
  ASSERT_TRUE(recovered);
  EXPECT_EQ(states_.at(client_).total_correction, before);
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
        "},\n"
        "  rifle = { magazine_capacity = 30, rounds_per_minute = 600, muzzle_velocity = 800, reload_seconds = 2.5,\n"
        "    recoil_pattern = {}, recoil_recovery_per_second = 0, ads_recoil_scale = 1, ads_field_of_view = 0.7 },\n"
        "  ammo = { gravity = 9.81, max_range = 1000, damage = { head = 100, torso = 34, limb = 25 } },\n"
        "  starting_health = 100,\n"
        "}");
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
  EXPECT_NEAR(states_.at(&client).local_body.stamina, BodySeenBy(client, *client.GetEntityId())->stamina, 0.1F);
}

TEST_F(ScriptedParametersTest, AClientPredictsItsRifleWithTheValuesOfTheServersScript) {
  Session& client = Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  Command fire{};
  fire.fire = true;

  // A second of fire.
  Run(60, fire);

  // The script's magazine of 30 at its 600 rounds a minute, ten of them gone: a
  // client on values of its own has one round and fires it once a minute.
  EXPECT_EQ(states_.at(&client).rifle.rounds, 20);
  ASSERT_TRUE(client.GetAuthoritativeState().has_value());
  EXPECT_EQ(client.GetAuthoritativeState()->rifle.rounds, 20);
  EXPECT_EQ(states_.at(&client).rifle_corrections, 0U);
}

// The session a ScriptedServer admits its client under.
constexpr SessionIdWire kScriptedSession{1};

// The body a ScriptedServer gives its client in Match start: not its session's number.
constexpr EntityIdWire kScriptedEntity{101};

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
    return augusta::protocol::MatchStartWire{
        .players = {{.spawn = {}, .session = kScriptedSession, .entity = kScriptedEntity, .character = 1}}};
  }

  // An Authoritative State of tick listing entities, each at the origin, with
  // the client's player unhurt.
  static augusta::protocol::AuthoritativeStateWire StateOf(std::uint32_t tick,
                                                           const std::vector<EntityIdWire>& entities) {
    augusta::protocol::AuthoritativeStateWire state{
        .tick = tick, .acknowledged_sequence = 0, .bodies = {}, .rifle = {}, .health = 100.0F};
    for (const EntityIdWire entity : entities) {
      state.bodies.push_back({.entity = entity, .body = {}});
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

// The scripted server's rifle holds one round. The client fires it, and the
// server says it refused that round: the rifle is still loaded and ready.
TEST_F(ScriptedServerTest, ARifleTheServerSaysDiffersFromThePredictedOneIsPutAtTheServers) {
  Settle();
  Command fire{};
  fire.fire = true;
  const augusta::prediction::State fired = session_.Tick(fire, kFixedTick);
  ASSERT_EQ(fired.rounds_fired, 1);
  ASSERT_EQ(fired.rifle.rounds, 0);

  augusta::protocol::AuthoritativeStateWire refused = ScriptedServer::StateOf(1, {kScriptedEntity});
  refused.acknowledged_sequence = 1;
  refused.rifle = {.cooldown = 0.0F, .reload_remaining = 0.0F, .rounds = 1};
  server_.Send(refused);
  Settle();

  // With the round back in the magazine, the next tick fires it again.
  const augusta::prediction::State corrected = session_.Tick(fire, kFixedTick);
  EXPECT_EQ(corrected.rifle_corrections, 1U);
  EXPECT_EQ(corrected.rounds_fired, 1);
  EXPECT_EQ(corrected.rifle.rounds, 0);
}

TEST_F(ScriptedServerTest, AStateNamingABodyNotInTheMatchIsDropped) {
  Settle();

  server_.Send(ScriptedServer::StateOf(1, {kScriptedEntity, EntityIdWire{99}}));
  Settle();
  EXPECT_FALSE(session_.GetAuthoritativeState().has_value());

  server_.Send(ScriptedServer::StateOf(2, {kScriptedEntity}));
  Settle();
  EXPECT_TRUE(session_.GetAuthoritativeState().has_value());
}

TEST_F(ScriptedServerTest, AStateThatArrivesAfterMatchEndIsDroppedAndTheClientIsBackInTheLobby) {
  server_.Send(ScriptedServer::StateOf(1, {kScriptedEntity}));
  Settle();
  ASSERT_TRUE(session_.GetAuthoritativeState().has_value());

  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ScriptedServer::StateOf(2, {kScriptedEntity}));
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
  server_.Send(ScriptedServer::StateOf(1, {kScriptedEntity}));
  Settle();
  EXPECT_EQ(session_.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(session_.GetAuthoritativeState().has_value());

  server_.Send(ScriptedServer::StartOfAlone());
  server_.Send(ScriptedServer::StateOf(2, {kScriptedEntity}));
  Settle();
  EXPECT_EQ(session_.GetPhase(), Phase::kMatch);
  EXPECT_TRUE(session_.GetAuthoritativeState().has_value());
}

TEST_F(ScriptedLobbyTest, AMatchStartThatLeavesThisClientOutIsDropped) {
  server_.Send(augusta::protocol::MatchStartWire{
      .players = {{.spawn = {}, .session = SessionIdWire{2}, .entity = EntityIdWire{102}, .character = 1}}});
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
  server_.Send(augusta::protocol::MatchStartWire{
      .players = {{.spawn = spawn, .session = kScriptedSession, .entity = kScriptedEntity, .character = 1}}});
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
  EXPECT_NEAR(state.local_body.position.x, BodySeenBy(client, *client.GetEntityId())->position.x, 0.5F);
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
  Host host(TestHostConfig(kTestParameters, 0),
            Map{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
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
  auto host = std::make_unique<Host>(
      TestHostConfig(), Map{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
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
  Host host(TestHostConfig(),
            Map{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
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
  BytesWire truncated_state = augusta::protocol::Encode(augusta::protocol::AuthoritativeStateWire{.bodies = {{}}});
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
  EXPECT_EQ(after->bodies.size(), 2U);  // The bystander, and the raw peer that only joined.
  EXPECT_GT(BodySeenBy(bystander, *bystander.GetEntityId())->position.x, SpawnPoints()[0].x + 1.0F);
  for (const auto& body : after->bodies) {
    EXPECT_TRUE(std::isfinite(body.body.position.x) && std::isfinite(body.body.position.y));
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
  ASSERT_EQ(watcher.GetAuthoritativeState()->bodies.size(), augusta::protocol::kMaxPlayers);
  const auto leaver = *sessions_.back()->GetEntityId();

  sessions_.back()->Disconnect();
  Run(kSettleTicks);

  EXPECT_EQ(watcher.GetAuthoritativeState()->bodies.size(), augusta::protocol::kMaxPlayers - 1);
  EXPECT_FALSE(PositionSeenBy(watcher, leaver).has_value());

  Session& ninth = Connect();
  Run(kSettleTicks);

  EXPECT_EQ(ninth.GetRefusal(), JoinRefusal::kMatchInProgress);
  EXPECT_EQ(watcher.GetAuthoritativeState()->bodies.size(), augusta::protocol::kMaxPlayers - 1);
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
    players = ServerTick().bodies.size();
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
  EXPECT_EQ(next.GetAuthoritativeState()->bodies.size(), 1U);
}

// A match of kPlayers on the floor, each with the test rifle: 600 rounds a
// minute, a round every six ticks at the test tick rate, and a magazine of 15
// that takes half a second, 30 ticks, to reload. Its recoil pattern is three
// kicks, each a whole count of the angle grid's step so their sums are exact,
// halved in ADS, and its Recoil offset recovers by 0.00625 rad a tick.
template <std::uint8_t kPlayers>
class FireMatchOf : public LoopbackMatch {
 protected:
  static constexpr std::uint8_t kMagazine = 15;
  static constexpr int kTicksPerRound = 6;
  static constexpr int kReloadTicks = 30;
  // The test character's eye, standing, above its feet.
  static constexpr float kEyeHeight = 1.6F;
  static constexpr float kFirstPitch = 1.0F / 64.0F;
  static constexpr float kSecondPitch = 1.0F / 64.0F;
  static constexpr float kSecondYaw = 1.0F / 256.0F;
  static constexpr float kThirdPitch = 1.0F / 32.0F;
  static constexpr float kThirdYaw = -1.0F / 128.0F;

  static HostSetup Armed() {
    Parameters parameters = WithPlayerCount(kPlayers);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = kMagazine;
    parameters.rifle.reload_seconds = 0.5F;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.rifle.recoil_pattern = {{.pitch = kFirstPitch, .yaw = 0.0F},
                                       {.pitch = kSecondPitch, .yaw = kSecondYaw},
                                       {.pitch = kThirdPitch, .yaw = kThirdYaw}};
    parameters.rifle.recoil_recovery_per_second = 0.375F;
    parameters.rifle.ads_recoil_scale = 0.5F;
    parameters.ammo.max_range = 1000.0F;
    HostSetup setup = OnTheFloor({Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F)}, parameters);
    setup.map.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    return setup;
  }

  FireMatchOf() : LoopbackMatch(Armed()) {}

  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }

  void SetUp() override {
    for (std::uint8_t i = 0; i < kPlayers; ++i) {
      Join();
    }
    ASSERT_TRUE(StartMatch());
    Run(kSettleTicks);
  }

  static Command Firing() {
    Command command{};
    command.fire = true;
    return command;
  }

  // Reload pressed, with fire held or not.
  static Command Reloading(bool fire) {
    Command command{};
    command.reload = true;
    command.fire = fire;
    return command;
  }

  // Adds the Shots each client has received since last asked to what it had.
  void Collect() {
    for (const auto& session : sessions_) {
      std::vector<Shot>& shots = shots_[session.get()];
      const std::vector<Shot> taken = session->TakeShots();
      shots.insert(shots.end(), taken.begin(), taken.end());
    }
  }

  // Runs the network until every client has received count Shots in all, or the
  // deadline passes; returns whether they all have.
  bool ReceiveShots(std::size_t count) {
    return ExchangeUntil(host_, Pointers(sessions_), [&] {
      Collect();
      return std::ranges::all_of(sessions_, [&](const auto& session) { return shots_[session.get()].size() >= count; });
    });
  }

  // The Shots client has received in all, once the network has had the time to deliver any on its way.
  const std::vector<Shot>& ShotsOf(const Session& client) {
    Settle(host_, Pointers(sessions_));
    Collect();
    return shots_[&client];
  }

  std::map<const Session*, std::vector<Shot>> shots_;
};

using FireTest = FireMatchOf<1>;

TEST_F(FireTest, HoldingFireFiresAtTheFireRateUntilTheMagazineIsEmpty) {
  Session& client = *sessions_.front();

  // A second of fire: ten rounds.
  Run(60, Firing());
  EXPECT_EQ(ShotsOf(client).size(), 10U);

  // Two more: the five rounds left, and then nothing.
  Run(120, Firing());
  EXPECT_EQ(ShotsOf(client).size(), kMagazine);
}

TEST_F(FireTest, AOneTickPressOfFireGivesExactlyOneShot) {
  Session& client = *sessions_.front();

  Step(Firing());
  Run(30);

  EXPECT_EQ(ShotsOf(client).size(), 1U);
}

TEST_F(FireTest, NoShotIsFiredWhileFireIsNotHeld) {
  Session& client = *sessions_.front();

  Run(30);

  EXPECT_TRUE(ShotsOf(client).empty());
}

TEST_F(FireTest, AfterAReloadOfAnEmptyMagazineTheShotsResumeForAFullMagazine) {
  Session& client = *sessions_.front();
  Run(120, Firing());
  ASSERT_EQ(ShotsOf(client).size(), kMagazine);

  // The press and the 29 ticks after it are the reload's half second.
  Step(Reloading(/*fire=*/true));
  Run(kReloadTicks - 1, Firing());
  EXPECT_EQ(ShotsOf(client).size(), kMagazine);

  Step(Firing());
  EXPECT_EQ(ShotsOf(client).size(), kMagazine + 1U);

  // Long enough for two magazines: only the one the reload gave is fired.
  Run(240, Firing());
  EXPECT_EQ(ShotsOf(client).size(), 2U * kMagazine);
}

TEST_F(FireTest, AReloadPressWithAFullMagazineStartsNothingAndFireContinues) {
  Session& client = *sessions_.front();

  // A second of fire, as without the press: ten rounds.
  Step(Reloading(/*fire=*/true));
  Run(59, Firing());

  EXPECT_EQ(ShotsOf(client).size(), 10U);
}

// A Command's reload is a press (the sampler sets it on one tick), but the
// server does not count on it: sent on every tick, it still starts one reload.
TEST_F(FireTest, ReloadSentOnEveryTickStartsOneReloadNotOneEveryTick) {
  Session& client = *sessions_.front();
  Run(120, Firing());
  ASSERT_EQ(ShotsOf(client).size(), kMagazine);

  // Started over on every tick, the reload would not be done after its half second.
  Run(kReloadTicks, Reloading(/*fire=*/false));
  Step(Firing());
  EXPECT_EQ(ShotsOf(client).size(), kMagazine + 1U);

  Run(120, Firing());
  EXPECT_EQ(ShotsOf(client).size(), 2U * kMagazine);
}

using FireDuelTest = FireMatchOf<2>;

TEST_F(FireDuelTest, EveryClientIsToldOfAShotWithItsShooterTickOriginAndDirection) {
  Session& shooter = *sessions_[0];
  Session& bystander = *sessions_[1];
  Settle(host_, Pointers(sessions_));
  const std::uint32_t last_tick = shooter.GetAuthoritativeState()->tick;
  const Vec3 feet = PositionSeenBy(shooter, *shooter.GetEntityId()).value();
  Command command = Firing();
  command.yaw = 0.75F;
  command.pitch = -0.25F;
  const auto shooter_session = static_cast<augusta::server::SessionId>(std::to_underlying(*shooter.GetSessionId()));

  // One tick, by hand: the server takes in the fire command on its next tick.
  shooter.Tick(command, kFixedTick);
  bystander.Tick(Command{}, kFixedTick);
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return host_.QueuedCommands(shooter_session) > 0; }));
  ASSERT_EQ(host_.Tick(kFixedTick).shots.size(), 1U);

  ASSERT_TRUE(ReceiveShots(1));
  for (const Session* client : {&shooter, &bystander}) {
    ASSERT_EQ(shots_[client].size(), 1U);
    const Shot& shot = shots_[client].front();
    EXPECT_EQ(shot.shooter, *shooter.GetEntityId());
    EXPECT_EQ(shot.tick, last_tick + 1);
    EXPECT_NEAR(shot.origin.x, feet.x, 0.002F);
    EXPECT_NEAR(shot.origin.y, feet.y + kEyeHeight, 0.002F);
    EXPECT_NEAR(shot.origin.z, feet.z, 0.002F);
    EXPECT_EQ(shot.yaw, 0.75F);
    EXPECT_EQ(shot.pitch, -0.25F);
  }
}

// A Shot is reliable (ADR-0044): under the packet loss of the movement tests,
// every client still hears every round, in the order they were fired.
TEST_F(FireDuelTest, WithPacketLossEveryClientStillReceivesEveryShot) {
  constexpr float kLossPercent = 20.0F;
  constexpr int kTicks = 90;
  augusta::networking::SimulateNetworkConditions({.loss_percent = kLossPercent});

  // What the server fired, which is what every client must be told.
  std::vector<augusta::simulation::Shot> fired;
  for (int i = 0; i < kTicks; ++i) {
    for (const auto& session : sessions_) {
      session->Tick(Firing(), kFixedTick);
    }
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    const augusta::simulation::State state = host_.Tick(kFixedTick);
    fired.insert(fired.end(), state.shots.begin(), state.shots.end());
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
  }
  augusta::networking::SimulateNetworkConditions({});

  ASSERT_GT(fired.size(), 10U);
  ASSERT_TRUE(ReceiveShots(fired.size()));
  for (const auto& session : sessions_) {
    const std::vector<Shot>& received = shots_[session.get()];
    ASSERT_EQ(received.size(), fired.size());
    for (std::size_t i = 0; i < fired.size(); ++i) {
      EXPECT_EQ(std::to_underlying(received[i].shooter), std::to_underlying(fired[i].shooter)) << "shot " << i;
      EXPECT_EQ(received[i].origin, fired[i].origin) << "shot " << i;
      EXPECT_EQ(received[i].yaw, fired[i].yaw) << "shot " << i;
      EXPECT_EQ(received[i].pitch, fired[i].pitch) << "shot " << i;
      // The server's ticks are not the test's to number, but both clients are told the same one.
      EXPECT_EQ(received[i].tick, shots_[sessions_.front().get()][i].tick) << "shot " << i;
    }
  }
}

// One client that predicts its own fire and reload (ADR-0024) against a server
// running the same rifle, compared command by command: the server's update
// tells the client the rifle it had after the newest command it took in.
class PredictedFireTest : public FireMatchOf<1> {
 protected:
  void SetUp() override {
    FireMatchOf<1>::SetUp();
    client_ = sessions_.front().get();
    // The settling ticks went out under sequences 1 to kSettleTicks.
    ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return Acknowledged() == sequence_; }));
  }

  // The newest of the client's commands the server has told it of.
  [[nodiscard]] std::uint32_t Acknowledged() const {
    const auto state = client_->GetAuthoritativeState();
    return state.has_value() ? state->acknowledged_sequence : 0U;
  }

  // What the client predicts on its next tick, on command, kept under the
  // sequence that command goes out with.
  void Predict(const Command& command) {
    states_[client_] = client_->Tick(command, kFixedTick);
    predicted_[++sequence_] = states_[client_].rifle;
  }

  // Holds the rifle of the newest update the client has against the one it
  // predicted after the command that update acknowledges.
  void CompareWithTheServer() {
    const auto state = client_->GetAuthoritativeState();
    ASSERT_TRUE(state.has_value());
    const auto predicted = predicted_.find(state->acknowledged_sequence);
    if (predicted == predicted_.end()) {
      return;
    }
    EXPECT_EQ(+state->rifle.rounds, +predicted->second.rounds) << "after command " << state->acknowledged_sequence;
    EXPECT_EQ(state->rifle.recoil.pitch, predicted->second.recoil.pitch)
        << "after command " << state->acknowledged_sequence;
    EXPECT_EQ(state->rifle.recoil.yaw, predicted->second.recoil.yaw)
        << "after command " << state->acknowledged_sequence;
    compared_.insert(state->acknowledged_sequence);
  }

  // One tick of the client on command, with the server ticking once for every
  // command that has reached it and never without one: every input is
  // delivered to a tick of its own, however late or however many at once, as
  // they are when the client's pacing holds (ADR-0038).
  void PredictAndDeliver(const Command& command) {
    Predict(command);
    ServeDelivered();
  }

  // Ticks the server through the commands that reach it within a tick's time,
  // then lets its answers reach the client.
  void ServeDelivered() {
    const auto self = static_cast<augusta::server::SessionId>(std::to_underlying(*client_->GetSessionId()));
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    while (host_.QueuedCommands(self) > 0) {
      host_.Tick(kFixedTick);
    }
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    CompareWithTheServer();
  }

  // A burst, a reload of the part-empty magazine, and a burst from the full
  // one: seven rounds, then four.
  template <typename Tick>
  void FireReloadAndFire(Tick tick) {
    for (int i = 0; i < 40; ++i) {
      tick(Firing());
    }
    tick(Reloading(/*fire=*/true));
    for (int i = 1; i < kReloadTicks; ++i) {
      tick(Firing());
    }
    for (int i = 0; i < 20; ++i) {
      tick(Firing());
    }
  }

  // Serves the commands still on their way, until the server has answered the
  // last one the client sent or the deadline passes; returns whether it has.
  bool ServeUntilAnswered() {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (Acknowledged() < sequence_ && std::chrono::steady_clock::now() < deadline) {
      ServeDelivered();
    }
    return Acknowledged() == sequence_;
  }

  Session* client_ = nullptr;
  std::uint32_t sequence_ = kSettleTicks;
  // The rifle the client predicted after each command, by its sequence, and
  // the sequences the server's answer has been compared at.
  std::map<std::uint32_t, augusta::weapon::State> predicted_;
  std::set<std::uint32_t> compared_;
};

// US-08: the ammo count and the reload, predicted exactly as the server applies them.
TEST_F(PredictedFireTest, AClientThatFiresAndReloadsPredictsTheAmmoTheServerHasAfterEveryCommand) {
  std::vector<std::uint8_t> rounds;
  FireReloadAndFire([&](const Command& command) {
    Step(command);
    predicted_[++sequence_] = states_.at(client_).rifle;
    rounds.push_back(states_.at(client_).rifle.rounds);
    CompareWithTheServer();
  });

  // A round on the first tick and every sixth after it; the reload fills the
  // magazine with its thirtieth tick.
  EXPECT_EQ(rounds[0], kMagazine - 1);
  EXPECT_EQ(rounds[39], kMagazine - 7);
  EXPECT_EQ(rounds[40 + kReloadTicks - 2], kMagazine - 7);
  EXPECT_EQ(rounds[40 + kReloadTicks - 1], kMagazine);
  EXPECT_EQ(rounds.back(), kMagazine - 4);
  ASSERT_TRUE(ServeUntilAnswered());
  EXPECT_GT(compared_.size(), rounds.size() / 2);
  EXPECT_TRUE(compared_.contains(sequence_));
  EXPECT_EQ(states_.at(client_).rifle_corrections, 0U);
}

// The M3 stamina tests' "never corrects", for the rifle: the client runs a
// round trip ahead of the server, and what comes back never takes a round or a
// reload back.
TEST_F(PredictedFireTest, AtAHundredMillisecondsOfLatencyAClientWhoseInputsAllArriveNeverCorrectsItsRifle) {
  constexpr int kOneWayLatencyMs = 50;
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs});

  FireReloadAndFire([&](const Command& command) { PredictAndDeliver(command); });

  ASSERT_TRUE(ServeUntilAnswered());
  EXPECT_GT(compared_.size(), 40U);
  EXPECT_EQ(states_.at(client_).rifle.rounds, kMagazine - 4);
  EXPECT_EQ(states_.at(client_).rifle_corrections, 0U);
}

// A lost datagram's commands arrive with the next one (ADR-0038), late and
// several at once, and still each on a tick of its own.
TEST_F(PredictedFireTest, WithPacketLossAClientWhoseInputsAllArriveNeverCorrectsItsRifle) {
  constexpr float kLossPercent = 20.0F;
  augusta::networking::SimulateNetworkConditions({.loss_percent = kLossPercent});

  FireReloadAndFire([&](const Command& command) { PredictAndDeliver(command); });

  // The last commands sent under loss may be lost for good, with nothing newer
  // to repeat them; a few more over a clean network carry them.
  augusta::networking::SimulateNetworkConditions({});
  constexpr int kRecoveryTicks = 10;
  for (int i = 0; i < kRecoveryTicks; ++i) {
    PredictAndDeliver(Command{});
  }
  ASSERT_TRUE(ServeUntilAnswered());
  EXPECT_GT(compared_.size(), 40U);
  EXPECT_EQ(states_.at(client_).rifle.rounds, kMagazine - 4);
  EXPECT_EQ(states_.at(client_).rifle_corrections, 0U);
}

// US-09 for the recoil: a burst from the hip, a moment off the trigger that
// recovers only part of it, a burst in ADS and a rest, all predicted a round
// trip ahead of the server, and never taken back.
TEST_F(PredictedFireTest, AtAHundredMillisecondsOfLatencyAClientWhoseInputsAllArriveNeverCorrectsItsRecoil) {
  constexpr int kOneWayLatencyMs = 50;
  constexpr int kBurstTicks = 20;
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs});
  Command aiming = Firing();
  aiming.ads = true;
  float highest = 0.0F;
  const auto tick = [&](const Command& command) {
    PredictAndDeliver(command);
    highest = std::max(highest, states_.at(client_).rifle.recoil.pitch);
  };

  for (int i = 0; i < kBurstTicks; ++i) {
    tick(Firing());
  }
  for (int i = 0; i < 3; ++i) {
    tick(Command{});
  }
  for (int i = 0; i < kBurstTicks; ++i) {
    tick(aiming);
  }
  for (int i = 0; i < 40; ++i) {
    tick(Command{});
  }

  ASSERT_TRUE(ServeUntilAnswered());
  EXPECT_GT(compared_.size(), 40U);
  // Four rounds from the hip, less what three ticks recovered, and four in ADS.
  EXPECT_GT(highest, 0.1F);
  EXPECT_EQ(states_.at(client_).rifle.recoil, augusta::weapon::RecoilOffset{});
  EXPECT_EQ(states_.at(client_).rifle_corrections, 0U);
}

// US-09 end to end: what the recoil does to the Shots every client is told of.
class RecoilTest : public FireMatchOf<1> {
 protected:
  // The view every burst is fired from.
  static constexpr float kViewYaw = 0.75F;
  static constexpr float kViewPitch = -0.25F;

  // Fire held from the hip, or in ADS, looking along the tests' view.
  static Command FiringFromTheView(bool ads = false) {
    Command command = Firing();
    command.yaw = kViewYaw;
    command.pitch = kViewPitch;
    command.ads = ads;
    return command;
  }

  // Holds command until the server has fired the given number of rounds on it,
  // and returns their Shots as the client was told of them. The trigger is
  // still held when it returns.
  std::vector<Shot> Burst(std::size_t rounds, const Command& command = FiringFromTheView()) {
    const std::size_t before = fired_;
    for (std::size_t i = 0; i < rounds * kTicksPerRound && fired_ < before + rounds; ++i) {
      fired_ += Step(command).shots.size();
    }
    EXPECT_EQ(fired_, before + rounds);
    EXPECT_TRUE(ReceiveShots(fired_));
    const std::vector<Shot>& shots = shots_[sessions_.front().get()];
    EXPECT_EQ(shots.size(), fired_);
    return shots.size() < rounds ? std::vector<Shot>(rounds)
                                 : std::vector<Shot>(shots.end() - static_cast<std::ptrdiff_t>(rounds), shots.end());
  }

  // How many rounds the server has fired in all.
  std::size_t fired_ = 0;
};

TEST_F(RecoilTest, TheShotsOfAHeldBurstFollowThePatternCumulativelyAndASecondBurstFromTheSameViewRepeatsThem) {
  const std::vector<Shot> first = Burst(5);
  // Long enough off the trigger for the recoil to recover.
  Run(60);
  const std::vector<Shot> second = Burst(5);

  EXPECT_EQ(first[0].yaw, kViewYaw);
  EXPECT_EQ(first[0].pitch, kViewPitch);
  EXPECT_EQ(first[1].yaw, kViewYaw);
  EXPECT_EQ(first[1].pitch, kViewPitch + kFirstPitch);
  EXPECT_EQ(first[2].yaw, kViewYaw + kSecondYaw);
  EXPECT_EQ(first[2].pitch, kViewPitch + kFirstPitch + kSecondPitch);
  EXPECT_EQ(first[3].yaw, kViewYaw + kSecondYaw + kThirdYaw);
  EXPECT_EQ(first[3].pitch, kViewPitch + kFirstPitch + kSecondPitch + kThirdPitch);
  // Past the pattern's last kick, the last repeats.
  EXPECT_EQ(first[4].yaw, kViewYaw + kSecondYaw + (2.0F * kThirdYaw));
  EXPECT_EQ(first[4].pitch, kViewPitch + kFirstPitch + kSecondPitch + (2.0F * kThirdPitch));
  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(second[i].yaw, first[i].yaw) << "round " << i;
    EXPECT_EQ(second[i].pitch, first[i].pitch) << "round " << i;
  }
}

TEST_F(RecoilTest, ReleasingAndFiringAgainAtOnceRestartsThePatternOnTopOfWhatHasNotRecovered) {
  Burst(3);
  Step(Command{});

  const std::vector<Shot> shots = Burst(2);

  // The three kicks, less what the ticks off the trigger recovered of them.
  const float left = shots[0].pitch - kViewPitch;
  EXPECT_GT(left, 0.0F);
  EXPECT_LT(left, kFirstPitch + kSecondPitch + kThirdPitch);
  // Then the first kick again, not the fourth round's.
  EXPECT_EQ(shots[1].pitch - shots[0].pitch, kFirstPitch);
  EXPECT_EQ(shots[1].yaw - shots[0].yaw, 0.0F);
}

TEST_F(RecoilTest, TheSameBurstInAdsClimbsByTheScaledKicks) {
  const std::vector<Shot> shots = Burst(4, FiringFromTheView(/*ads=*/true));

  EXPECT_EQ(shots[0].yaw, kViewYaw);
  EXPECT_EQ(shots[0].pitch, kViewPitch);
  EXPECT_EQ(shots[1].pitch, kViewPitch + (kFirstPitch / 2.0F));
  EXPECT_EQ(shots[2].yaw, kViewYaw + (kSecondYaw / 2.0F));
  EXPECT_EQ(shots[2].pitch, kViewPitch + ((kFirstPitch + kSecondPitch) / 2.0F));
  EXPECT_EQ(shots[3].yaw, kViewYaw + ((kSecondYaw + kThirdYaw) / 2.0F));
  EXPECT_EQ(shots[3].pitch, kViewPitch + ((kFirstPitch + kSecondPitch + kThirdPitch) / 2.0F));
}

// The Recoil offset is the rifle's, on top of the view: the client predicts it
// and never sends it, so the server turns the body by the view alone, and adds
// the recoil to it once, itself.
TEST_F(RecoilTest, TheViewAClientSendsNeverIncludesItsRecoil) {
  Session& client = *sessions_.front();

  const std::vector<Shot> shots = Burst(4);
  Run(1, FiringFromTheView());

  EXPECT_NE(states_.at(&client).rifle.recoil.yaw, 0.0F);
  EXPECT_NE(shots[3].yaw, kViewYaw);
  const auto state = client.GetAuthoritativeState();
  ASSERT_TRUE(state.has_value());
  ASSERT_EQ(state->bodies.size(), 1U);
  EXPECT_EQ(state->bodies.front().yaw, kViewYaw);
}

// A hitbox for part: the box from low to high, as the twelve triangles of its faces.
augusta::assets::HitboxData BoxHitbox(augusta::assets::BodyPart part, const Vec3& low, const Vec3& high) {
  return augusta::assets::HitboxData{
      .part = part,
      .mesh = {.points = {Vec3(low.x, low.y, low.z), Vec3(high.x, low.y, low.z), Vec3(high.x, high.y, low.z),
                          Vec3(low.x, high.y, low.z), Vec3(low.x, low.y, high.z), Vec3(high.x, low.y, high.z),
                          Vec3(high.x, high.y, high.z), Vec3(low.x, high.y, high.z)},
               .indices = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                           3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2}}};
}

// The hitboxes of the character the hit tests play: 1.8 m tall, with a head, a
// torso and legs on its axis and a right arm (a limb) beside the torso, at +X
// when it faces yaw 0.
std::vector<augusta::assets::HitboxData> HumanHitboxes() {
  return {BoxHitbox(augusta::assets::BodyPart::kHead, Vec3(-0.1F, 1.5F, -0.1F), Vec3(0.1F, 1.8F, 0.1F)),
          BoxHitbox(augusta::assets::BodyPart::kTorso, Vec3(-0.2F, 0.9F, -0.1F), Vec3(0.2F, 1.5F, 0.1F)),
          BoxHitbox(augusta::assets::BodyPart::kLimb, Vec3(-0.2F, 0.0F, -0.1F), Vec3(0.2F, 0.9F, 0.1F)),
          BoxHitbox(augusta::assets::BodyPart::kLimb, Vec3(0.3F, 0.9F, -0.1F), Vec3(0.4F, 1.5F, 0.1F))};
}

// A match of kPlayers standing in a line down -Z, 10 m apart, on the floor:
// the first, at the origin, is the shooter, and a view of yaw 0 looks from it
// at the others. Everyone plays the character of HumanHitboxes, which sees from
// inside its head. A round takes 50 of a player's 100 of health at the head, 20
// at the torso and 10 at a limb, and reaches the nearest target on the tick it
// is fired.
template <std::uint8_t kPlayers>
class HitMatchOf : public LoopbackMatch {
 protected:
  using BodyPart = augusta::ballistics::BodyPart;

  static constexpr float kEyeHeight = 1.6F;
  static constexpr float kSpacing = 10.0F;
  static constexpr float kWallZ = -5.0F;
  static constexpr float kHeadDamage = 50.0F;
  static constexpr float kTorsoDamage = 20.0F;
  static constexpr float kLimbDamage = 10.0F;
  // Where a shot at each part of a standing target is aimed, above its feet.
  static constexpr float kHeadHeight = 1.65F;
  static constexpr float kTorsoHeight = 1.2F;
  static constexpr float kLegsHeight = 0.45F;

  // walled puts a wall across the line, halfway between the shooter and the
  // nearest target; policy is the scenario's Game policy.
  static HostSetup InALine(bool walled, augusta::scripting::Engine policy) {
    Parameters parameters = WithPlayerCount(kPlayers);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = 30;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.ammo.max_range = 200.0F;
    parameters.ammo.damage = {.head = kHeadDamage, .torso = kTorsoDamage, .limb = kLimbDamage};
    parameters.starting_health = 100.0F;
    std::vector<Vec3> spawn_points;
    for (std::uint8_t i = 0; i < kPlayers; ++i) {
      spawn_points.emplace_back(0.0F, kFloorY, -kSpacing * static_cast<float>(i));
    }
    HostSetup setup = OnTheFloor(std::move(spawn_points), parameters);
    setup.policy = std::move(policy);
    setup.map.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    setup.map.characters.front().hitboxes = HumanHitboxes();
    if (walled) {
      setup.map.collision.push_back(CollisionMesh{.points = {Vec3(-20.0F, kFloorY, kWallZ), Vec3(-20.0F, 5.0F, kWallZ),
                                                             Vec3(20.0F, 5.0F, kWallZ), Vec3(20.0F, kFloorY, kWallZ)},
                                                  .indices = {0, 1, 2, 0, 2, 3}});
    }
    return setup;
  }

  explicit HitMatchOf(bool walled = false, augusta::scripting::Engine policy = {})
      : LoopbackMatch(InALine(walled, std::move(policy))) {}

  void SetUp() override {
    for (std::uint8_t i = 0; i < kPlayers; ++i) {
      Join();
    }
    ASSERT_TRUE(StartMatch());
    commands_.assign(sessions_.size(), Command{});
    Fight(kSettleTicks);
  }

  // The client whose player stands rank places down the line: 0 is the shooter.
  Session& Standing(int rank) {
    for (const auto& session : sessions_) {
      if (std::abs(OwnSpawn(*session).z + (kSpacing * static_cast<float>(rank))) < 0.5F) {
        return *session;
      }
    }
    ADD_FAILURE() << "no client spawned " << rank << " places down the line";
    return *sessions_.front();
  }

  // What client sends on every tick from now on.
  Command& CommandOf(const Session& client) {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      if (sessions_[i].get() == &client) {
        return commands_[i];
      }
    }
    ADD_FAILURE() << "not a client of this match";
    return commands_.front();
  }

  // Runs the match for the given number of ticks, each client on its command,
  // keeping what the server resolved on them.
  void Fight(int ticks) {
    for (int i = 0; i < ticks; ++i) {
      const augusta::simulation::State state = StepEach(commands_);
      hits_.insert(hits_.end(), state.hits.begin(), state.hits.end());
      map_impacts_.insert(map_impacts_.end(), state.map_impacts.begin(), state.map_impacts.end());
      shots_fired_.insert(shots_fired_.end(), state.shots.begin(), state.shots.end());
      deaths_.insert(deaths_.end(), state.deaths.begin(), state.deaths.end());
      if (state.match_end.has_value()) {
        match_ends_.emplace_back(state.tick, *state.match_end);
      }
    }
  }

  // Turns command's view to look along aim: from the eye to the point aimed at.
  static void AimAt(Command& command, const Vec3& aim) {
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / Length(aim));
  }

  // The shooter taps fire once, aimed from its eye at the point offset from
  // target's feet as the newest update it has shows them, which is the view
  // its Command reports, and the match runs until its rifle is ready again.
  void ShootAt(const Session& target, const Vec3& offset) {
    ShootThrough(PositionSeenBy(Standing(0), *target.GetEntityId()).value() + offset);
  }

  // As ShootAt, aimed at point, whatever is there.
  void ShootThrough(const Vec3& point) {
    Session& shooter = Standing(0);
    const Vec3 eye = PositionSeenBy(shooter, *shooter.GetEntityId()).value() + Vec3(0.0F, kEyeHeight, 0.0F);
    Command& command = CommandOf(shooter);
    AimAt(command, point - eye);
    command.view_tick = shooter.GetAuthoritativeState().value().tick;
    command.fire = true;
    Fight(1);
    command.fire = false;
    Fight(8);
  }

  // The Hit confirmations client has received in all, once the network has had
  // the time to deliver any on its way.
  const std::vector<HitConfirmation>& ConfirmationsOf(const Session& client) {
    Settle(host_, Pointers(sessions_));
    for (const auto& session : sessions_) {
      std::vector<HitConfirmation>& confirmations = confirmations_[session.get()];
      const std::vector<HitConfirmation> taken = session->TakeHitConfirmations();
      confirmations.insert(confirmations.end(), taken.begin(), taken.end());
    }
    return confirmations_[&client];
  }

  // The Deaths client has received in all, once the network has had the time
  // to deliver any on its way.
  const std::vector<Death>& DeathsOf(const Session& client) {
    Settle(host_, Pointers(sessions_));
    CollectDeaths();
    return deaths_received_[&client];
  }

  // Adds the Deaths each client has received since last asked to what it had.
  void CollectDeaths() {
    for (const auto& session : sessions_) {
      std::vector<Death>& deaths = deaths_received_[session.get()];
      const std::vector<Death> taken = session->TakeDeaths();
      deaths.insert(deaths.end(), taken.begin(), taken.end());
    }
  }

  // Two head shots: the second takes target's health of 100 to zero.
  void Kill(const Session& target) {
    ShootAt(target, Vec3(0.0F, kHeadHeight, 0.0F));
    ShootAt(target, Vec3(0.0F, kHeadHeight, 0.0F));
  }

  // What each client sends on a tick, in the order of sessions_.
  std::vector<Command> commands_;
  // Every hit, Map impact, round and death the server has resolved.
  std::vector<augusta::simulation::Hit> hits_;
  std::vector<Vec3> map_impacts_;
  std::vector<augusta::simulation::Shot> shots_fired_;
  std::vector<augusta::simulation::Death> deaths_;
  // Every Match end Game policy has decided, with the tick it decided it on.
  std::vector<std::pair<std::uint32_t, augusta::simulation::MatchEnd>> match_ends_;
  std::map<const Session*, std::vector<HitConfirmation>> confirmations_;
  std::map<const Session*, std::vector<Death>> deaths_received_;
};

using HitLineTest = HitMatchOf<3>;

TEST_F(HitLineTest, ShootingAStandingTargetsHeadTorsoAndLimbConfirmsEachToTheShooterAlone) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  Session& bystander = Standing(2);

  ShootAt(target, Vec3(0.0F, kHeadHeight, 0.0F));
  ShootAt(target, Vec3(0.0F, kTorsoHeight, 0.0F));
  ShootAt(target, Vec3(0.0F, kLegsHeight, 0.0F));

  const std::vector<HitConfirmation>& confirmations = ConfirmationsOf(shooter);
  ASSERT_EQ(confirmations.size(), 3U);
  for (const HitConfirmation& confirmation : confirmations) {
    EXPECT_EQ(confirmation.target, *target.GetEntityId());
  }
  EXPECT_EQ(confirmations[0].part, BodyPart::kHead);
  EXPECT_EQ(confirmations[0].damage, kHeadDamage);
  EXPECT_EQ(confirmations[1].part, BodyPart::kTorso);
  EXPECT_EQ(confirmations[1].damage, kTorsoDamage);
  EXPECT_EQ(confirmations[2].part, BodyPart::kLimb);
  EXPECT_EQ(confirmations[2].damage, kLimbDamage);
  EXPECT_TRUE(ConfirmationsOf(target).empty());
  EXPECT_TRUE(ConfirmationsOf(bystander).empty());
}

// The third player stands behind the second, on the line the shooter fires along.
TEST_F(HitLineTest, WithTwoTargetsInLineOnlyTheNearerIsHit) {
  Session& near = Standing(1);

  ShootAt(near, Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits_.size(), 1U);
  EXPECT_EQ(std::to_underlying(hits_[0].target), std::to_underlying(*near.GetEntityId()));
  ASSERT_EQ(ConfirmationsOf(Standing(0)).size(), 1U);
  EXPECT_EQ(ConfirmationsOf(Standing(0))[0].target, *near.GetEntityId());
}

using HitDuelTest = HitMatchOf<2>;

// A body 2.1 m tall standing is 1.3 m tall crouched and 0.7 m prone, and its hitboxes with it.
TEST_F(HitDuelTest, AShotWhereAStandingHeadWouldBeMissesACrouchedOrProneTargetAndOneAtItsLoweredBodyHits) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  struct Lowered {
    Stance stance;
    float height;
  };

  for (const Lowered& lowered : {Lowered{Stance::kCrouching, 1.3F}, Lowered{Stance::kProne, 0.7F}}) {
    CommandOf(target).movement.desired_stance = lowered.stance;
    Fight(kSettleTicks);
    ASSERT_EQ(BodySeenBy(shooter, *target.GetEntityId())->stance, lowered.stance);

    ShootAt(target, Vec3(0.0F, kHeadHeight, 0.0F));
    EXPECT_TRUE(hits_.empty()) << "stance " << static_cast<int>(lowered.stance);

    ShootAt(target, Vec3(0.0F, kTorsoHeight * lowered.height / 2.1F, 0.0F));
    ASSERT_EQ(hits_.size(), 1U) << "stance " << static_cast<int>(lowered.stance);
    EXPECT_EQ(hits_[0].part, BodyPart::kTorso);
    hits_.clear();
  }

  const std::vector<HitConfirmation>& confirmations = ConfirmationsOf(shooter);
  ASSERT_EQ(confirmations.size(), 2U);
  EXPECT_EQ(confirmations[0].part, BodyPart::kTorso);
  EXPECT_EQ(confirmations[1].part, BodyPart::kTorso);
}

// The target's right arm is at +X while it faces yaw 0, and at -X once it has
// turned half a turn: every client is told where it faces, and its hitboxes
// turn with it.
TEST_F(HitDuelTest, AShotAtATargetsSideHitsAccordingToItsReplicatedFacing) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  const Vec3 right_of_it(0.35F, kTorsoHeight, 0.0F);
  const Vec3 left_of_it(-0.35F, kTorsoHeight, 0.0F);
  const auto yaw_seen = [&] {
    for (const auto& body : shooter.GetAuthoritativeState().value().bodies) {
      if (body.entity == *target.GetEntityId()) {
        return body.yaw;
      }
    }
    return std::numeric_limits<float>::quiet_NaN();
  };

  EXPECT_EQ(yaw_seen(), 0.0F);
  ShootAt(target, left_of_it);
  EXPECT_TRUE(hits_.empty());
  ShootAt(target, right_of_it);
  ASSERT_EQ(hits_.size(), 1U);
  EXPECT_EQ(hits_[0].part, BodyPart::kLimb);
  hits_.clear();

  constexpr float kHalfATurn = std::numbers::pi_v<float>;
  CommandOf(target).yaw = kHalfATurn;
  Fight(kSettleTicks);
  EXPECT_EQ(yaw_seen(), augusta::math::SnapAngle(kHalfATurn));

  ShootAt(target, right_of_it);
  EXPECT_TRUE(hits_.empty());
  ShootAt(target, left_of_it);
  ASSERT_EQ(hits_.size(), 1U);
  EXPECT_EQ(hits_[0].part, BodyPart::kLimb);
  EXPECT_EQ(ConfirmationsOf(shooter).size(), 2U);
}

// The same duel with a wall across the line, halfway to the target.
class WalledHitTest : public HitMatchOf<2> {
 protected:
  WalledHitTest() : HitMatchOf<2>(/*walled=*/true) {}
};

TEST_F(WalledHitTest, ATargetBehindAWallIsNotHitAndTheWallIs) {
  Session& shooter = Standing(0);

  ShootAt(Standing(1), Vec3(0.0F, kTorsoHeight, 0.0F));

  EXPECT_TRUE(hits_.empty());
  ASSERT_EQ(map_impacts_.size(), 1U);
  EXPECT_NEAR(map_impacts_[0].z, kWallZ, 0.01F);
  EXPECT_TRUE(ConfirmationsOf(shooter).empty());
}

using LoneHitTest = HitMatchOf<1>;

// The eye is inside the shooter's own head hitbox, which every round leaves through.
TEST_F(LoneHitTest, AShooterFiringForwardWhileMovingNeverHitsItself) {
  Session& shooter = Standing(0);
  Command& command = CommandOf(shooter);
  command.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
  command.fire = true;

  Fight(60);

  Settle(host_, Pointers(sessions_));
  EXPECT_EQ(shooter.TakeShots().size(), 10U);
  EXPECT_TRUE(hits_.empty());
  EXPECT_TRUE(ConfirmationsOf(shooter).empty());
}

// The duel at 100 ms of latency, with the target walking across the shooter's
// view. The clients run a round trip ahead of the server, as real ones do. The
// test stands in for the shooter's presentation: it keeps what each update it
// is sent shows of the target, and shows it the Interpolation delay behind the
// newest.
class LagCompensatedHitTest : public HitMatchOf<2> {
 protected:
  static constexpr int kOneWayLatencyMs = 50;
  // The Interpolation delay, about 100 ms, in ticks of kFixedTick.
  static constexpr std::uint32_t kInterpolationTicks = 6;

  void TearDown() override { augusta::networking::SimulateNetworkConditions({}); }

  // Whether the server holds a command of every client for its next tick.
  [[nodiscard]] bool EveryoneHasACommandQueued() const {
    return std::ranges::all_of(sessions_, [&](const auto& session) {
      return host_.QueuedCommands(
                 static_cast<augusta::server::SessionId>(std::to_underlying(*session->GetSessionId()))) > 0;
    });
  }

  // One tick of every client on its command, with the server ticking once for
  // every tick's worth of commands that has reached it and never without one,
  // keeping what it resolved and where the shooter's newest update puts the
  // target's feet.
  void PlayAhead(const Session& shooter, const Session& target) {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      states_[sessions_[i].get()] = sessions_[i]->Tick(commands_.at(i), kFixedTick);
    }
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    while (EveryoneHasACommandQueued()) {
      const augusta::simulation::State state = host_.Tick(kFixedTick);
      hits_.insert(hits_.end(), state.hits.begin(), state.hits.end());
    }
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    if (const auto feet = PositionSeenBy(shooter, *target.GetEntityId()); feet.has_value()) {
      seen_[shooter.GetAuthoritativeState()->tick] = *feet;
    }
  }

  // Where each update the shooter was sent put the target's feet, by its tick.
  std::map<std::uint32_t, Vec3> seen_;
};

// US-11, ADR-0044: a shot that hits on the shooter's screen hits on the server.
TEST_F(LagCompensatedHitTest, AClientFiringAtAStrafingTargetUnderItsCrosshairInTheShownViewGetsAHitConfirmation) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  CommandOf(target).movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs});
  for (int i = 0; i < 60; ++i) {
    PlayAhead(shooter, target);
  }

  // The view: the newest update kept that is the Interpolation delay or more
  // behind the newest of all, and halfway to the next one if that is kept too.
  ASSERT_FALSE(seen_.empty());
  const std::uint32_t newest = seen_.rbegin()->first;
  ASSERT_GT(newest, kInterpolationTicks);
  auto shown = seen_.upper_bound(newest - kInterpolationTicks);
  ASSERT_NE(shown, seen_.begin());
  --shown;
  const auto next = seen_.find(shown->first + 1);
  const float fraction = next == seen_.end() ? 0.0F : 0.5F;
  const Vec3 feet = next == seen_.end() ? shown->second : augusta::math::Lerp(shown->second, next->second, fraction);
  // The target has since walked clear of where the view shows its torso, 0.4 m wide.
  ASSERT_GT(seen_.rbegin()->second.x - feet.x, 0.25F);

  const Vec3 eye = PositionSeenBy(shooter, *shooter.GetEntityId()).value() + Vec3(0.0F, kEyeHeight, 0.0F);
  Command& command = CommandOf(shooter);
  AimAt(command, feet + Vec3(0.0F, kTorsoHeight, 0.0F) - eye);
  command.view_tick = shown->first;
  command.view_fraction = fraction;
  command.fire = true;
  PlayAhead(shooter, target);
  command.fire = false;
  for (int i = 0; i < 30; ++i) {
    PlayAhead(shooter, target);
  }

  ASSERT_EQ(hits_.size(), 1U);
  EXPECT_EQ(hits_[0].part, BodyPart::kTorso);
  const std::vector<HitConfirmation>& confirmations = ConfirmationsOf(shooter);
  ASSERT_EQ(confirmations.size(), 1U);
  EXPECT_EQ(confirmations[0].target, *target.GetEntityId());
  EXPECT_EQ(confirmations[0].part, BodyPart::kTorso);
  EXPECT_TRUE(ConfirmationsOf(target).empty());
}

// A full match in four pairs, 5 m apart along X: the two of a pair stand 10 m
// apart along Z, facing each other. Everyone plays the character of
// HumanHitboxes and carries a rifle of 600 rounds a minute whose magazine of 15
// takes half a second to reload, and whose every round kicks the aim up by
// 1/256 rad, about 4 cm at the partner. No one has health enough to die.
class FullAutoMatchTest : public LoopbackMatch {
 protected:
  static constexpr std::size_t kPlayers = augusta::protocol::kMaxPlayers;
  static constexpr float kEyeHeight = 1.6F;
  static constexpr float kPairSpacing = 5.0F;
  static constexpr float kPairDistance = 10.0F;

  static HostSetup InFacingPairs() {
    Parameters parameters = WithPlayerCount(kPlayers);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = 15;
    parameters.rifle.reload_seconds = 0.5F;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.rifle.recoil_pattern = {{.pitch = 1.0F / 256.0F, .yaw = 0.0F}};
    parameters.rifle.recoil_recovery_per_second = 0.375F;
    parameters.ammo.gravity = 9.81F;
    parameters.ammo.max_range = 200.0F;
    parameters.ammo.damage = {.head = 50.0F, .torso = 20.0F, .limb = 10.0F};
    // So much that no one dies of ten seconds of fire, and everyone fires throughout.
    parameters.starting_health = 100000.0F;
    std::vector<Vec3> spawn_points;
    for (std::size_t i = 0; i < kPlayers; ++i) {
      spawn_points.emplace_back(kPairSpacing * static_cast<float>(i / 2), kFloorY, i % 2 == 0 ? 0.0F : -kPairDistance);
    }
    HostSetup setup = OnTheFloor(std::move(spawn_points), parameters);
    setup.map.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    setup.map.characters.front().hitboxes = HumanHitboxes();
    return setup;
  }

  FullAutoMatchTest() : LoopbackMatch(InFacingPairs()) {}

  void SetUp() override {
    for (std::size_t i = 0; i < kPlayers; ++i) {
      Join();
    }
    ASSERT_TRUE(StartMatch());
  }

  // The yaw that looks from client's spawn point at its partner's.
  static float FacingItsPartner(const Session& client) {
    return OwnSpawn(client).z > -kPairDistance / 2.0F ? 0.0F : std::numbers::pi_v<float>;
  }
};

// NFR-01 for combat (issue #228), extending the M3 soak test above and paced to
// the real 60 Hz for the reason given there: eight clients strafe, sprint and
// change stance, all the same way so each pair stays face to face, holding
// fire for the whole match and reloading whenever their magazine is empty, and
// each reports the view it was last sent, so the hits are lag compensated. The
// server keeps up with every client's commands, tells every client of every
// Shot, and every shooter of every one of its hits.
TEST_F(FullAutoMatchTest, EightClientsMoveFireFullAutoReloadAndHitEachOtherForAMatchWithNoMissedTicks) {
  constexpr int kMatchTicks = 600;             // 10 real seconds at kTestTickRate, paced.
  constexpr std::uint32_t kAckTolerance = 20;  // A few round trips' worth still in flight.
  constexpr std::array<Stance, 3> kStanceCycle = {Stance::kStanding, Stance::kCrouching, Stance::kProne};
  constexpr int kStanceCycleTicks = 150;
  constexpr int kSprintBlockTicks = 100;
  // A block strafing one way, a block still, a block the other way, a block still.
  constexpr int kStrafeBlockTicks = 30;
  constexpr std::array<float, 4> kStrafeCycle = {1.0F, 0.0F, -1.0F, 0.0F};

  struct Client {
    float yaw = 0.0F;
    // Its rifle as it last predicted it: what it decides to reload by.
    augusta::weapon::State rifle{};
    float first_x = 0.0F;
    float farthest = 0.0F;
    std::uint32_t acknowledged = 0;
    std::size_t shots = 0;
    std::size_t confirmations = 0;
  };
  std::vector<Client> clients(sessions_.size());
  for (std::size_t i = 0; i < sessions_.size(); ++i) {
    clients[i].yaw = FacingItsPartner(*sessions_[i]);
  }
  // What the server fired, and the hits it resolved by the body that fired them.
  std::size_t fired = 0;
  std::map<std::uint32_t, std::size_t> hits_by;
  const auto collect = [&] {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      clients[i].shots += sessions_[i]->TakeShots().size();
      clients[i].confirmations += sessions_[i]->TakeHitConfirmations().size();
    }
  };

  const auto tick_duration = std::chrono::duration<float>(kFixedTick);
  for (int tick = 0; tick < kMatchTicks; ++tick) {
    const auto tick_start = std::chrono::steady_clock::now();

    const augusta::simulation::State state = host_.Tick(kFixedTick);
    fired += state.shots.size();
    for (const augusta::simulation::Hit& hit : state.hits) {
      ++hits_by[std::to_underlying(hit.shooter)];
    }
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      Client& client = clients[i];
      Command command{};
      command.movement.direction = Vec3(kStrafeCycle.at((tick / kStrafeBlockTicks) % kStrafeCycle.size()), 0.0F, 0.0F);
      command.movement.sprint = (tick / kSprintBlockTicks) % 2 == 0;
      command.movement.desired_stance = kStanceCycle.at((tick / kStanceCycleTicks) % kStanceCycle.size());
      command.yaw = client.yaw;
      command.fire = true;
      command.reload = tick > 0 && client.rifle.rounds == 0 && client.rifle.reload_remaining <= 0.0F;
      if (const auto shown = sessions_[i]->GetAuthoritativeState()) {
        command.view_tick = shown->tick;
      }

      const auto predicted = sessions_[i]->Tick(command, kFixedTick);
      client.rifle = predicted.rifle;
      if (tick == 0) {
        client.first_x = predicted.local_body.position.x;
      }
      client.farthest = std::max(client.farthest, std::abs(predicted.local_body.position.x - client.first_x));
    }

    host_.PumpNetwork();
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      sessions_[i]->PumpEvents();
      sessions_[i]->ExchangeMessages();

      ASSERT_FALSE(sessions_[i]->GetFailure().has_value()) << "client " << i << " failed at tick " << tick;
      EXPECT_EQ(sessions_[i]->GetConnectionState(), ConnectionState::kConnected)
          << "client " << i << " dropped at tick " << tick;

      if (const auto authoritative = sessions_[i]->GetAuthoritativeState()) {
        clients[i].acknowledged = std::max(clients[i].acknowledged, authoritative->acknowledged_sequence);
      }
    }
    collect();

    std::this_thread::sleep_until(tick_start +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
  }

  // A Shot and a Hit confirmation are reliable: the last of them are on their way.
  EXPECT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] {
    collect();
    return std::ranges::all_of(clients, [&](const Client& client) { return client.shots >= fired; });
  }));
  Settle(host_, Pointers(sessions_));
  collect();

  // Five magazines each, less the rounds the first commands' way to the server cost.
  EXPECT_GT(fired, kPlayers * 60U);
  for (std::size_t i = 0; i < sessions_.size(); ++i) {
    const Client& client = clients[i];
    EXPECT_GE(client.acknowledged + kAckTolerance, static_cast<std::uint32_t>(kMatchTicks))
        << "client " << i << " fell behind: server acknowledged only " << client.acknowledged << " of " << kMatchTicks
        << " ticks";
    EXPECT_GT(client.farthest, 0.5F) << "client " << i << " did not move over the match";
    EXPECT_EQ(client.shots, fired) << "client " << i << " was not told of every Shot";
    EXPECT_GT(client.confirmations, 0U) << "client " << i << " never hit its partner";
    EXPECT_EQ(client.confirmations, hits_by[std::to_underlying(*sessions_[i]->GetEntityId())])
        << "client " << i << " was not told of every hit of its own";
  }
}

// A Hit confirmation of the scripted server's one player hitting whoever it names.
augusta::protocol::HitConfirmationWire HitOn(EntityIdWire target) {
  return augusta::protocol::HitConfirmationWire{
      .target = target, .damage = 37.5F, .part = augusta::protocol::BodyPartWire::kHead};
}

TEST_F(ScriptedServerTest, AHitConfirmationOfABodyInTheMatchIsHandedOutOnceAsItWasSent) {
  server_.Send(HitOn(kScriptedEntity));
  Settle();

  const std::vector<HitConfirmation> hits = session_.TakeHitConfirmations();

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].target, *session_.GetEntityId());
  EXPECT_EQ(hits[0].part, augusta::ballistics::BodyPart::kHead);
  EXPECT_EQ(hits[0].damage, 37.5F);
  EXPECT_TRUE(session_.TakeHitConfirmations().empty());
}

TEST_F(ScriptedServerTest, AHitConfirmationNamingABodyNotInTheMatchIsDropped) {
  server_.Send(HitOn(EntityIdWire{99}));
  Settle();

  EXPECT_TRUE(session_.TakeHitConfirmations().empty());
}

TEST_F(ScriptedServerTest, AHitConfirmationThatArrivesAfterMatchEndIsDropped) {
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(HitOn(kScriptedEntity));
  Settle();

  EXPECT_TRUE(session_.TakeHitConfirmations().empty());
}

TEST_F(ScriptedServerTest, AHitConfirmationNobodyAskedForIsNotHandedOutInTheNextMatch) {
  server_.Send(HitOn(kScriptedEntity));
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ScriptedServer::StartOfAlone());
  Settle();
  ASSERT_EQ(session_.GetPhase(), Phase::kMatch);

  EXPECT_TRUE(session_.TakeHitConfirmations().empty());
}

TEST_F(ScriptedLobbyTest, AHitConfirmationThatArrivesBeforeMatchStartIsDropped) {
  server_.Send(HitOn(kScriptedEntity));
  Settle();
  server_.Send(ScriptedServer::StartOfAlone());
  Settle();

  EXPECT_TRUE(session_.TakeHitConfirmations().empty());
}

// A client nobody asks keeps the newest Hit confirmations, not all of them for ever.
TEST_F(ScriptedServerTest, AClientThatIsNeverAskedKeepsOnlyTheNewestHitConfirmations) {
  const std::size_t sent = augusta::harness::kMaxPendingHitConfirmations + 10;
  for (std::size_t i = 1; i <= sent; ++i) {
    augusta::protocol::HitConfirmationWire hit = HitOn(kScriptedEntity);
    hit.damage = static_cast<float>(i);
    server_.Send(hit);
  }
  Settle();

  const std::vector<HitConfirmation> hits = session_.TakeHitConfirmations();

  ASSERT_EQ(hits.size(), augusta::harness::kMaxPendingHitConfirmations);
  EXPECT_EQ(hits.front().damage, 11.0F);
  EXPECT_EQ(hits.back().damage, static_cast<float>(sent));
}

// A Shot of the scripted server's one player, or of whoever else it names.
augusta::protocol::ShotWire ShotBy(EntityIdWire shooter) {
  return augusta::protocol::ShotWire{
      .origin = Vec3(1.0F, 1.5F, -2.0F), .shooter = shooter, .tick = 7, .yaw = 0.5F, .pitch = -0.125F};
}

TEST_F(ScriptedServerTest, AShotOfABodyInTheMatchIsHandedOutOnceAsItWasSent) {
  server_.Send(ShotBy(kScriptedEntity));
  Settle();

  const std::vector<Shot> shots = session_.TakeShots();

  ASSERT_EQ(shots.size(), 1U);
  EXPECT_EQ(shots[0].shooter, *session_.GetEntityId());
  EXPECT_EQ(shots[0].tick, 7U);
  EXPECT_EQ(shots[0].origin, Vec3(1.0F, 1.5F, -2.0F));
  EXPECT_EQ(shots[0].yaw, 0.5F);
  EXPECT_EQ(shots[0].pitch, -0.125F);
  EXPECT_TRUE(session_.TakeShots().empty());
}

TEST_F(ScriptedServerTest, ShotsAreHandedOutInTheOrderTheyArrived) {
  for (std::uint32_t tick = 1; tick <= 3; ++tick) {
    augusta::protocol::ShotWire shot = ShotBy(kScriptedEntity);
    shot.tick = tick;
    server_.Send(shot);
  }
  Settle();

  const std::vector<Shot> shots = session_.TakeShots();

  ASSERT_EQ(shots.size(), 3U);
  EXPECT_EQ(shots[0].tick, 1U);
  EXPECT_EQ(shots[1].tick, 2U);
  EXPECT_EQ(shots[2].tick, 3U);
}

TEST_F(ScriptedServerTest, AShotNamingABodyNotInTheMatchIsDropped) {
  server_.Send(ShotBy(EntityIdWire{99}));
  Settle();

  EXPECT_TRUE(session_.TakeShots().empty());
}

TEST_F(ScriptedServerTest, AShotThatArrivesAfterMatchEndIsDropped) {
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ShotBy(kScriptedEntity));
  Settle();

  EXPECT_TRUE(session_.TakeShots().empty());
}

TEST_F(ScriptedServerTest, AShotNobodyAskedForIsNotHandedOutInTheNextMatch) {
  server_.Send(ShotBy(kScriptedEntity));
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ScriptedServer::StartOfAlone());
  Settle();
  ASSERT_EQ(session_.GetPhase(), Phase::kMatch);

  EXPECT_TRUE(session_.TakeShots().empty());
}

TEST_F(ScriptedLobbyTest, AShotThatArrivesBeforeMatchStartIsDropped) {
  server_.Send(ShotBy(kScriptedEntity));
  Settle();
  EXPECT_TRUE(session_.TakeShots().empty());

  server_.Send(ScriptedServer::StartOfAlone());
  Settle();
  EXPECT_TRUE(session_.TakeShots().empty());
}

// A client nobody asks keeps the newest Shots, not all of them for ever.
TEST_F(ScriptedServerTest, AClientThatIsNeverAskedKeepsOnlyTheNewestShots) {
  const auto sent = static_cast<std::uint32_t>(augusta::harness::kMaxPendingShots + 10);
  for (std::uint32_t tick = 1; tick <= sent; ++tick) {
    augusta::protocol::ShotWire shot = ShotBy(kScriptedEntity);
    shot.tick = tick;
    server_.Send(shot);
  }
  Settle();

  const std::vector<Shot> shots = session_.TakeShots();

  ASSERT_EQ(shots.size(), augusta::harness::kMaxPendingShots);
  EXPECT_EQ(shots.front().tick, 11U);
  EXPECT_EQ(shots.back().tick, sent);
}

using DeathTest = HitMatchOf<3>;

TEST_F(DeathTest, APlayerShotToZeroHealthDiesAndEveryClientIsToldWhoKilledItAndWhere) {
  Session& shooter = Standing(0);
  Session& victim = Standing(1);
  Session& bystander = Standing(2);

  Kill(victim);

  ASSERT_EQ(deaths_.size(), 1U);
  for (const Session* client : {&shooter, &victim, &bystander}) {
    const std::vector<Death>& deaths = DeathsOf(*client);
    ASSERT_EQ(deaths.size(), 1U);
    EXPECT_EQ(deaths[0].victim, *victim.GetEntityId());
    EXPECT_EQ(deaths[0].killer, *shooter.GetEntityId());
    EXPECT_EQ(deaths[0].part, BodyPart::kHead);
    EXPECT_EQ(deaths[0].yaw, deaths_[0].yaw);
    EXPECT_EQ(deaths[0].pitch, deaths_[0].pitch);
  }
}

TEST_F(DeathTest, TheDeadBodyIsGoneFromEveryClientsStateFromThenOn) {
  Session& victim = Standing(1);

  Kill(victim);
  Fight(30);

  for (const auto& client : sessions_) {
    EXPECT_FALSE(BodySeenBy(*client, *victim.GetEntityId()).has_value());
    EXPECT_TRUE(BodySeenBy(*client, *Standing(0).GetEntityId()).has_value());
    EXPECT_TRUE(BodySeenBy(*client, *Standing(2).GetEntityId()).has_value());
  }
}

// A Death is reliable: under the packet loss of the movement tests, every
// client is still told of it.
TEST_F(DeathTest, WithPacketLossEveryClientStillReceivesTheDeath) {
  constexpr float kLossPercent = 20.0F;
  Session& victim = Standing(1);
  augusta::networking::SimulateNetworkConditions({.loss_percent = kLossPercent});

  // A round's fire command can be lost for good: the shooter fires until the server has a death.
  for (int attempt = 0; attempt < 10 && deaths_.empty(); ++attempt) {
    ShootAt(victim, Vec3(0.0F, kHeadHeight, 0.0F));
  }
  Fight(30);
  augusta::networking::SimulateNetworkConditions({});

  ASSERT_EQ(deaths_.size(), 1U);
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] {
    CollectDeaths();
    return std::ranges::all_of(sessions_,
                               [&](const auto& session) { return !deaths_received_[session.get()].empty(); });
  }));
  for (const auto& client : sessions_) {
    const std::vector<Death>& deaths = DeathsOf(*client);
    ASSERT_EQ(deaths.size(), 1U);
    EXPECT_EQ(deaths[0].victim, *victim.GetEntityId());
    EXPECT_EQ(deaths[0].killer, *Standing(0).GetEntityId());
    EXPECT_EQ(deaths[0].part, BodyPart::kHead);
  }
}

// The dead player walks at the shooter, turned to face it, firing.
TEST_F(DeathTest, ADeadPlayersFireAndMovementChangeNothingOnTheServer) {
  Session& shooter = Standing(0);
  Session& victim = Standing(1);
  Kill(victim);
  const std::size_t rounds = shots_fired_.size();
  Command& command = CommandOf(victim);
  command.movement.direction = Vec3(0.0F, 0.0F, 1.0F);
  command.yaw = std::numbers::pi_v<float>;
  command.fire = true;

  Fight(60);

  EXPECT_EQ(shots_fired_.size(), rounds);
  EXPECT_EQ(hits_.size(), 2U);
  EXPECT_FALSE(BodySeenBy(shooter, *victim.GetEntityId()).has_value());
  EXPECT_EQ(shooter.GetHealth(), 100.0F);
}

// The third player stands behind the second, on the line the shooter fires along.
TEST_F(DeathTest, ABulletAimedThroughWhereTheDeadPlayerStoodHitsWhatIsBehindIt) {
  Session& victim = Standing(1);
  Session& behind = Standing(2);
  const Vec3 stood = PositionSeenBy(Standing(0), *victim.GetEntityId()).value();
  Kill(victim);

  ShootThrough(stood + Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits_.size(), 3U);
  EXPECT_EQ(std::to_underlying(hits_.back().target), std::to_underlying(*behind.GetEntityId()));
}

TEST_F(DeathTest, ADeadPlayersOwnClientKnowsItIsDeadAndItsHealthAndStopsPredicting) {
  Session& victim = Standing(1);
  Settle(host_, Pointers(sessions_));
  ASSERT_TRUE(victim.IsAlive());
  ASSERT_EQ(victim.GetHealth(), 100.0F);

  ShootAt(victim, Vec3(0.0F, kHeadHeight, 0.0F));
  Settle(host_, Pointers(sessions_));
  EXPECT_TRUE(victim.IsAlive());
  EXPECT_EQ(victim.GetHealth(), 50.0F);

  ShootAt(victim, Vec3(0.0F, kHeadHeight, 0.0F));
  Settle(host_, Pointers(sessions_));
  EXPECT_FALSE(victim.IsAlive());
  EXPECT_EQ(victim.GetHealth(), 0.0F);

  const augusta::prediction::State dead = states_[&victim];
  Command& command = CommandOf(victim);
  command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  command.fire = true;
  Fight(30);
  EXPECT_EQ(states_[&victim].local_body.position, dead.local_body.position);
  EXPECT_EQ(states_[&victim].total_rounds_fired, dead.total_rounds_fired);
  EXPECT_TRUE(Standing(0).IsAlive());
}

using NextMatchTest = HitMatchOf<2>;

// Nothing carries over (US-03): the shooter emptied rounds and the victim died,
// yet both start the next Match alive, at full health, with full rifles.
TEST_F(NextMatchTest, TheSecondMatchOfARunStartsWithFullHealthAndFullMagazines) {
  Session& victim = Standing(1);
  Kill(victim);
  ASSERT_EQ(deaths_.size(), 1U);
  host_.EndMatch();
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] {
    return std::ranges::all_of(sessions_, [](const auto& s) { return s->GetPhase() == Phase::kLobby; });
  }));

  ASSERT_TRUE(StartMatch());
  const augusta::simulation::State first = ServerTick();

  ASSERT_EQ(first.bodies.size(), 2U);
  for (const auto& client : sessions_) {
    const auto state = client->GetAuthoritativeState();
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(client->IsAlive());
    EXPECT_EQ(state->health, 100.0F);
    EXPECT_EQ(state->rifle.rounds, 30U);
    EXPECT_EQ(state->rifle.reload_remaining, 0.0F);
    EXPECT_EQ(state->rifle.burst_index, 0U);
    EXPECT_EQ(state->rifle.recoil, augusta::weapon::RecoilOffset{});
  }
}

// A Death of victim, killed by killer with a round to the torso.
augusta::protocol::DeathWire DeathOf(EntityIdWire victim, EntityIdWire killer) {
  return augusta::protocol::DeathWire{.victim = victim,
                                      .killer = killer,
                                      .yaw = 0.5F,
                                      .pitch = -0.125F,
                                      .part = augusta::protocol::BodyPartWire::kTorso};
}

TEST_F(ScriptedServerTest, ADeathNamingABodyNotInTheMatchIsDropped) {
  Settle();

  server_.Send(DeathOf(EntityIdWire{99}, kScriptedEntity));
  server_.Send(DeathOf(kScriptedEntity, EntityIdWire{99}));
  Settle();
  EXPECT_TRUE(session_.TakeDeaths().empty());
  EXPECT_TRUE(session_.IsAlive());

  server_.Send(DeathOf(kScriptedEntity, kScriptedEntity));
  Settle();
  EXPECT_EQ(session_.TakeDeaths().size(), 1U);
  EXPECT_FALSE(session_.IsAlive());
}

// Told of its own death, a client predicts no more, even before an update says so.
TEST_F(ScriptedServerTest, AClientToldOfItsOwnDeathStopsPredicting) {
  Settle();
  Command walk;
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  walk.fire = true;
  session_.Tick(walk, kFixedTick);

  server_.Send(DeathOf(kScriptedEntity, kScriptedEntity));
  Settle();
  const augusta::prediction::State dead = session_.Tick(walk, kFixedTick);
  const augusta::prediction::State after = session_.Tick(walk, kFixedTick);

  EXPECT_EQ(after.local_body.position, dead.local_body.position);
  EXPECT_EQ(after.total_rounds_fired, dead.total_rounds_fired);
}

TEST_F(ScriptedLobbyTest, ADeathThatArrivesOutsideAMatchIsDropped) {
  EXPECT_FALSE(session_.IsAlive());
  server_.Send(DeathOf(kScriptedEntity, kScriptedEntity));
  Settle();
  EXPECT_TRUE(session_.TakeDeaths().empty());

  server_.Send(ScriptedServer::StartOfAlone());
  Settle();
  EXPECT_TRUE(session_.TakeDeaths().empty());
  EXPECT_TRUE(session_.IsAlive());
  EXPECT_FALSE(session_.GetHealth().has_value());
}

// The example scenario's objectives.lua, last player standing (US-14), loaded as
// the server's Game policy.
augusta::scripting::Engine ExampleObjectives() {
  const std::ifstream file(AUGUSTA_EXAMPLE_OBJECTIVES);
  std::stringstream text;
  text << file.rdbuf();
  auto policy = augusta::scripting::Engine::Load({.objectives = text.str(), .behaviours = std::nullopt});
  EXPECT_TRUE(policy.has_value()) << augusta::scripting::DescribeLoadError(policy.error());
  return policy ? *std::move(policy) : augusta::scripting::Engine{};
}

// A match of kPlayers in the line, under the example scenario's objectives:
// the last player standing wins.
template <std::uint8_t kPlayers>
class LastStandingMatchOf : public HitMatchOf<kPlayers> {
 protected:
  LastStandingMatchOf() : HitMatchOf<kPlayers>(false, ExampleObjectives()) {}

  // Runs the network until every connected client is back in the Lobby and has
  // been told how the match ended; returns whether they all were.
  bool EveryoneToldTheMatchEnded() {
    return ExchangeUntil(this->host_, Pointers(this->sessions_), [&] {
      return std::ranges::all_of(this->sessions_, [](const auto& session) {
        return session->GetConnectionState() != ConnectionState::kConnected ||
               (session->GetPhase() == Phase::kLobby && session->GetMatchEnd().has_value());
      });
    });
  }

  // shooter aims at target's head, as the newest update shooter has shows it,
  // and holds fire.
  void AimAtHead(Session& shooter, const Session& target) {
    const Vec3 eye = this->PositionSeenBy(shooter, *shooter.GetEntityId()).value() + Vec3(0.0F, this->kEyeHeight, 0.0F);
    const Vec3 head =
        this->PositionSeenBy(shooter, *target.GetEntityId()).value() + Vec3(0.0F, this->kHeadHeight, 0.0F);
    Command& command = this->CommandOf(shooter);
    this->AimAt(command, head - eye);
    command.view_tick = shooter.GetAuthoritativeState().value().tick;
    command.fire = true;
  }

  // The first two down the line shoot each other's head on the same tick, and
  // the match runs until their rifles are ready again.
  void Volley() {
    Session& first = this->Standing(0);
    Session& second = this->Standing(1);
    AimAtHead(first, second);
    AimAtHead(second, first);
    this->Fight(1);
    this->CommandOf(first).fire = false;
    this->CommandOf(second).fire = false;
    this->Fight(8);
  }
};

using LastStandingDuelTest = LastStandingMatchOf<2>;

TEST_F(LastStandingDuelTest, OneKillingTheOtherEndsTheMatchAndBothAreToldTheKillerWonAndAreBackInTheLobby) {
  Session& shooter = Standing(0);
  Session& victim = Standing(1);

  Kill(victim);

  ASSERT_EQ(match_ends_.size(), 1U);
  ASSERT_TRUE(EveryoneToldTheMatchEnded());
  for (const Session* client : {&shooter, &victim}) {
    EXPECT_EQ(client->GetPhase(), Phase::kLobby);
    EXPECT_EQ(client->GetMatchEnd()->winner, shooter.GetSessionId());
  }
}

// Match end is reliable: under the packet loss of the movement tests, both
// clients are still told it, and who won.
TEST_F(LastStandingDuelTest, WithPacketLossBothAreStillToldTheMatchEndedAndWhoWon) {
  constexpr float kLossPercent = 20.0F;
  Session& shooter = Standing(0);
  Session& victim = Standing(1);
  augusta::networking::SimulateNetworkConditions({.loss_percent = kLossPercent});

  // A round's fire command can be lost for good: the shooter fires until the match is over.
  for (int attempt = 0; attempt < 20 && match_ends_.empty(); ++attempt) {
    ShootAt(victim, Vec3(0.0F, kHeadHeight, 0.0F));
  }
  Fight(30);
  augusta::networking::SimulateNetworkConditions({});

  ASSERT_EQ(match_ends_.size(), 1U);
  ASSERT_TRUE(EveryoneToldTheMatchEnded());
  for (const Session* client : {&shooter, &victim}) {
    EXPECT_EQ(client->GetMatchEnd()->winner, shooter.GetSessionId());
  }
}

// Each takes the other to 50 of health, then both die on the same tick.
TEST_F(LastStandingDuelTest, TheLastTwoDyingOnTheSameTickIsADraw) {
  Volley();
  ASSERT_TRUE(match_ends_.empty());

  Volley();

  ASSERT_EQ(deaths_.size(), 2U);
  ASSERT_EQ(match_ends_.size(), 1U);
  EXPECT_FALSE(match_ends_[0].second.winner.has_value());
  ASSERT_TRUE(EveryoneToldTheMatchEnded());
  for (const auto& client : sessions_) {
    EXPECT_FALSE(client->GetMatchEnd()->winner.has_value());
  }
}

// The players of the next match start it afresh: full health and full magazines.
TEST_F(LastStandingDuelTest, TheNextMatchStartsOnItsOwnOnceEveryoneIsReadyAgainNoSoonerThanThePauseAfterTheLast) {
  constexpr auto kDeadline = std::chrono::seconds(15);
  Kill(Standing(1));
  ASSERT_EQ(match_ends_.size(), 1U);
  const std::uint32_t ended = match_ends_[0].first;

  // As a client does, each reports Ready for every Roster it is sent; nothing else happens.
  std::map<const Session*, std::uint32_t> reported;
  std::uint32_t started = 0;
  const auto deadline = std::chrono::steady_clock::now() + kDeadline;
  while (started == 0 && std::chrono::steady_clock::now() < deadline) {
    Exchange();
    for (const auto& session : sessions_) {
      const auto lobby = session->GetLobby();
      if (session->GetPhase() == Phase::kLobby && lobby.has_value() && reported[session.get()] != lobby->version) {
        session->ReportReady(lobby->version);
        reported[session.get()] = lobby->version;
      }
    }
    const augusta::simulation::State state = host_.Tick(kFixedTick);
    started = state.bodies.empty() ? 0 : state.tick;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_NE(started, 0U) << "no next match started";
  EXPECT_GE(started - ended, PauseTicks());
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] {
    return std::ranges::all_of(sessions_, [](const auto& session) { return session->GetPhase() == Phase::kMatch; });
  }));
  Run(kSettleTicks);
  for (const auto& client : sessions_) {
    EXPECT_TRUE(client->IsAlive());
    EXPECT_EQ(client->GetHealth(), 100.0F);
    EXPECT_EQ(client->GetAuthoritativeState()->rifle.rounds, 30U);
    EXPECT_FALSE(client->GetMatchEnd().has_value());
  }
}

TEST_F(LastStandingDuelTest, EveryMatchEndIsLoggedWithItsWinnerItsDurationAndItsReason) {
  augusta::logging::Init();
  Session& shooter = Standing(0);

  testing::internal::CaptureStdout();
  Kill(Standing(1));
  const std::string log = testing::internal::GetCapturedStdout();

  ASSERT_EQ(match_ends_.size(), 1U);
  EXPECT_NE(log.find("event=match_ended reason=\"win condition\" winner=" +
                     std::to_string(std::to_underlying(*shooter.GetSessionId())) + " ticks="),
            std::string::npos)
      << log;
}

// Those who leave are out of the match (US-20): the one left standing wins.
using LastStandingTrioTest = LastStandingMatchOf<3>;

TEST_F(LastStandingTrioTest, WhenAllButOnePlayerDisconnectTheOneLeftWins) {
  Session& survivor = Standing(2);
  Standing(0).Disconnect();
  Standing(1).Disconnect();

  Fight(kSettleTicks);

  ASSERT_EQ(match_ends_.size(), 1U);
  ASSERT_TRUE(EveryoneToldTheMatchEnded());
  EXPECT_EQ(survivor.GetPhase(), Phase::kLobby);
  EXPECT_EQ(survivor.GetMatchEnd()->winner, survivor.GetSessionId());
}

// A Match of one, for development (ADR-0043). In v1 only rounds kill and none
// hits its own shooter, so a lone player cannot die over the network: that its
// death is a draw is checked through SimulationWorld
// (example_objectives_test.cpp); here, that a lone player plays on, and that a
// draw reaches it as one.
class SoloMatchTest : public LoopbackMatch {
 protected:
  explicit SoloMatchTest(augusta::scripting::Engine policy) : LoopbackMatch(Alone(std::move(policy))) {}

  static HostSetup Alone(augusta::scripting::Engine policy) {
    HostSetup setup = OnTheFloor({Vec3(10.0F, kFloorY, 0.0F)}, WithPlayerCount(1));
    setup.policy = std::move(policy);
    return setup;
  }
};

class SoloLastStandingTest : public SoloMatchTest {
 protected:
  SoloLastStandingTest() : SoloMatchTest(ExampleObjectives()) {}
};

TEST_F(SoloLastStandingTest, ALonePlayerPlaysOnUnderTheExampleObjectives) {
  Session& client = Join();
  ASSERT_TRUE(StartMatch());

  Run(2 * kTestTickRate);

  EXPECT_EQ(client.GetPhase(), Phase::kMatch);
  EXPECT_FALSE(client.GetMatchEnd().has_value());
}

class SoloDrawTest : public SoloMatchTest {
 protected:
  SoloDrawTest() : SoloMatchTest(Objectives("function on_tick() return {draw = true} end")) {}

  static augusta::scripting::Engine Objectives(const char* objectives) {
    auto policy = augusta::scripting::Engine::Load({.objectives = objectives, .behaviours = std::nullopt});
    EXPECT_TRUE(policy.has_value());
    return policy ? *std::move(policy) : augusta::scripting::Engine{};
  }
};

TEST_F(SoloDrawTest, ASoloMatchEndedAsADrawIsToldToItsPlayerAsADrawAndItIsBackInTheLobby) {
  Session& client = Join();
  ASSERT_TRUE(StartMatch() || client.GetMatchEnd().has_value());

  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return client.GetMatchEnd().has_value(); }));

  EXPECT_EQ(client.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(client.GetMatchEnd()->winner.has_value());
}

}  // namespace
