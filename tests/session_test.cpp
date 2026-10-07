#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <prometheus/client_metric.h>
#include <prometheus/metric_family.h>

#include "admission.h"
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
#include "augusta/policy_actions.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "augusta/weapon.h"
#include "command_queue.h"
#include "connection_health.h"
#include "connection_sample.h"
#include "content.h"
#include "heartbeat.h"
#include "host.h"
#include "host_log.h"
#include "host_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "parameters_loader.h"
#include "policy_loader.h"
#include "recording.h"
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
using augusta::harness::ServerView;
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
namespace protocol = augusta::protocol;
using augusta::server::ConnectionHealth;
using augusta::server::ConnectionSample;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::Scenario;

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);
constexpr float kFixedTick = 1.0F / 60.0F;

// What a test's server runs on: NFR-01's 60 Hz and stamina rules that never
// drain, for a match of one player.
constexpr std::uint8_t kTestTickRate = 60;
const Parameters kTestParameters{};

// The one character every test's server offers and every test's client picks,
// unless a test says otherwise.
constexpr const char* kCharacter = "soldier";

// The test parameters, for a match of count players.
Parameters WithPlayerCount(std::uint8_t count) {
  Parameters parameters = kTestParameters;
  parameters.player_count = count;
  return parameters;
}

// A PredictionWorld with no map, and nothing decided yet: a server decides it on the client's join.
augusta::prediction::World EmptyWorld() { return augusta::prediction::World(); }

// ctest runs every test case in its own process, possibly in parallel, so a
// fixed port would collide: each server binds port 0, a free one of its own
// choosing, and its clients connect to the one the server reports.
constexpr const char* kLoopbackAnyPort = "127.0.0.1:0";

// An address nobody listens at: the one a server chose and has since closed.
// Another process's server could draw it again, but only one port in 16384 does.
Endpoint UnusedLoopbackEndpoint() {
  const augusta::networking::Server server(Endpoint{.address = kLoopbackAnyPort});
  return server.LocalEndpoint();
}

// A test server's settings: the test tick rate unless a test says otherwise.
HostConfig TestHostConfig(const Parameters& parameters = kTestParameters, std::uint8_t tick_rate_hz = kTestTickRate) {
  return HostConfig{
      .tick_rate_hz = tick_rate_hz,
      .parameters = parameters,
      .listen = Endpoint{.address = kLoopbackAnyPort},
      .recording = {},
      .server_pack = {},
  };
}

// A client of the server at server playing character.
SessionConfig TestSessionConfig(const Endpoint& server, const std::string& character = kCharacter) {
  return SessionConfig{.server = server, .character = character};
}

// Init and Shutdown once for the whole process, as in networking_test.cpp.
class SessionEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kSessionEnvironment =
    ::testing::AddGlobalTestEnvironment(new SessionEnvironment);

// The JoinRequestWire a real client of the test server sends.
augusta::protocol::JoinRequestWire HonestJoinRequest() {
  return augusta::protocol::JoinRequestWire{.engine_version = std::string(augusta::EngineVersion()),
                                            .character = kCharacter};
}

// A client speaking the protocol by hand, to send what a real one would not.
class RawClient {
 public:
  // A cooperative client asks to join once connected and reports Ready for each
  // Roster it is sent, as a client that has loaded everyone does; a scripted one
  // does neither, and its test sends every message itself.
  enum class Mode : std::uint8_t { kCooperative, kScripted };

  explicit RawClient(const Endpoint& server, Mode mode = Mode::kCooperative) : mode_(mode) { client_.Connect(server); }

  ~RawClient() { client_.Disconnect(); }

  RawClient(const RawClient&) = delete;
  RawClient& operator=(const RawClient&) = delete;
  RawClient(RawClient&&) = delete;
  RawClient& operator=(RawClient&&) = delete;

  // Runs host - its clock stopped at host_time, if given - and client until
  // until() holds or the deadline passes; returns whether it held.
  template <typename Condition>
  bool ServeUntil(Host& host, Condition until,
                  std::optional<std::chrono::steady_clock::time_point> host_time = std::nullopt) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host.PumpNetwork(host_time.value_or(std::chrono::steady_clock::now()));
      Serve();
      if (until()) {
        return true;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  // Runs host and client until the connection is up.
  bool Connect(Host& host) {
    return ServeUntil(host, [&] { return client_.GetState() == ConnectionState::kConnected; });
  }

  // Runs host and client until the server has admitted this one to the Lobby.
  bool Join(Host& host) {
    return ServeUntil(host, [&] { return !ReceivedOf<augusta::protocol::JoinAcceptedWire>().empty(); });
  }

  // One round of this client's own network work: takes in what has arrived and,
  // if cooperative, asks to join once connected.
  void Serve() {
    client_.PumpEvents();
    if (mode_ == Mode::kCooperative && !requested_ && client_.GetState() == ConnectionState::kConnected) {
      Send(HonestJoinRequest());
      requested_ = true;
    }
    Drain();
  }

  [[nodiscard]] bool InMatch() const { return !ReceivedOf<augusta::protocol::MatchStartWire>().empty(); }

  [[nodiscard]] ConnectionState GetConnectionState() const { return client_.GetState(); }

  // The session the server admitted this client as, if it has.
  [[nodiscard]] std::optional<SessionIdWire> GetSessionId() const {
    const auto accepted = ReceivedOf<augusta::protocol::JoinAcceptedWire>();
    return accepted.empty() ? std::nullopt : std::optional<SessionIdWire>(accepted.front().session);
  }

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

  // Every message of type T received so far, in the order it arrived.
  template <typename T>
  [[nodiscard]] std::vector<T> ReceivedOf() const {
    std::vector<T> found;
    for (const auto& message : received_) {
      if (const auto* typed = std::get_if<T>(&message)) {
        found.push_back(*typed);
      }
    }
    return found;
  }

  // How many messages of any type have arrived so far.
  [[nodiscard]] std::size_t ReceivedCount() const { return received_.size(); }

  // The newest Authoritative State received so far.
  [[nodiscard]] std::optional<augusta::protocol::AuthoritativeStateWire> NewestState() const { return newest_; }

  // The body this client controls, as the last Match start named it.
  [[nodiscard]] std::optional<EntityIdWire> Entity() const {
    const auto accepted = ReceivedOf<augusta::protocol::JoinAcceptedWire>();
    const auto starts = ReceivedOf<augusta::protocol::MatchStartWire>();
    if (accepted.empty() || starts.empty()) {
      return std::nullopt;
    }
    for (const auto& player : starts.back().players) {
      if (player.session == accepted.front().session) {
        return player.entity;
      }
    }
    return std::nullopt;
  }

 private:
  // Takes in what has arrived, answering each Roster with a ReadyWire if
  // cooperative, and returns the Authoritative States among it.
  std::vector<augusta::protocol::AuthoritativeStateWire> Drain() {
    std::vector<augusta::protocol::AuthoritativeStateWire> states;
    for (const auto& payload : client_.ReceiveMessages()) {
      const auto message = augusta::protocol::Decode(payload);
      if (!message.has_value()) {
        continue;
      }
      received_.push_back(*message);
      if (const auto* lobby = std::get_if<augusta::protocol::LobbyWire>(&*message)) {
        if (mode_ == Mode::kCooperative) {
          Send(augusta::protocol::ReadyWire{.version = lobby->version});
        }
      } else if (const auto* state = std::get_if<augusta::protocol::AuthoritativeStateWire>(&*message)) {
        states.push_back(*state);
        if (!newest_.has_value() || state->tick > newest_->tick) {
          newest_ = *state;
        }
      }
    }
    return states;
  }

  Mode mode_;
  augusta::networking::Client client_;
  bool requested_ = false;
  std::vector<augusta::protocol::MessageWire> received_;
  std::optional<augusta::protocol::AuthoritativeStateWire> newest_;
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
    host.PumpNetwork(std::chrono::steady_clock::now());
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
    host.PumpNetwork(std::chrono::steady_clock::now());
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
              Scenario{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}}),
        session_(TestSessionConfig(host_.ListenEndpoint()), EmptyWorld()) {}

  // Runs both sides' network work until the session reports connected, or
  // the deadline passes.
  bool ConnectSession() {
    session_.Connect();
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      host_.PumpNetwork(std::chrono::steady_clock::now());
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

// Requirements: US-01
TEST_F(SessionTest, ConnectsToAHostDrivenByHand) { EXPECT_TRUE(ConnectSession()); }

// Requirements: US-04
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

// Requirements: US-01
TEST_F(SessionTest, StaysConnectedWhileTheTestAlternatesTicksAndNetworkWork) {
  ASSERT_TRUE(ConnectSession());

  for (int i = 0; i < 10; ++i) {
    host_.Tick(kFixedTick);
    session_.Tick(Command{}, kFixedTick);
    host_.PumpNetwork(std::chrono::steady_clock::now());
    session_.PumpEvents();
    session_.ExchangeMessages();
  }

  EXPECT_EQ(session_.GetConnectionState(), ConnectionState::kConnected);
}

// Whether health has a gauge labelled by any Session.
bool HasSessionGauges(const ConnectionHealth& health) {
  return std::ranges::any_of(health.Collect(), [](const prometheus::MetricFamily& family) {
    return std::ranges::any_of(family.metric, [](const prometheus::ClientMetric& metric) {
      return std::ranges::any_of(
          metric.label, [](const prometheus::ClientMetric::Label& label) { return label.name == "session_id"; });
    });
  });
}

// A host whose matches take every player the protocol allows, and however many
// clients a test starts, all driven by hand.
class JoinTest : public ::testing::Test {
 protected:
  JoinTest()
      : host_(TestHostConfig(WithPlayerCount(augusta::protocol::kMaxPlayers)),
              Scenario{.collision = {},
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
    sessions_.push_back(std::make_unique<Session>(SessionConfig{.server = host_.ListenEndpoint(),
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

// Requirements: US-01
TEST_F(JoinTest, AClientWithTheMatchingVersionIsAdmittedWithASessionId) {
  Session& client = AddClient();

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_TRUE(client.GetSessionId().has_value());
  EXPECT_FALSE(client.GetRefusal().has_value());
  EXPECT_EQ(client.GetPhase(), Phase::kLobby);
}

// Requirements: US-01
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

// Requirements: NFR-07
TEST_F(JoinTest, EachConnectionIsSampledWithTheSessionItCarries) {
  const Session& client = AddClient();
  ASSERT_TRUE(WaitForAnswers());

  const std::vector<ConnectionSample> samples = host_.SampleConnections();

  ASSERT_EQ(samples.size(), 1U);
  ASSERT_TRUE(samples.front().session.has_value());
  ASSERT_TRUE(client.GetSessionId().has_value());
  EXPECT_EQ(static_cast<std::uint32_t>(*samples.front().session), static_cast<std::uint32_t>(*client.GetSessionId()));
}

// Requirements: NFR-07
TEST_F(JoinTest, ASessionThatEndsLeavesTheSampleAndItsGaugesGo) {
  Session& client = AddClient();
  ASSERT_TRUE(WaitForAnswers());
  ConnectionHealth health;
  health.Record(host_.SampleConnections());
  ASSERT_TRUE(HasSessionGauges(health));

  client.Disconnect();
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] {
    const std::vector<ConnectionSample> samples = host_.SampleConnections();
    return std::ranges::none_of(samples, [](const ConnectionSample& sample) { return sample.session.has_value(); });
  }));
  health.Record(host_.SampleConnections());

  EXPECT_FALSE(HasSessionGauges(health));
}

// Requirements: NFR-07
TEST_F(JoinTest, AConnectionThatHasNotJoinedIsSampledWithoutASession) {
  RawClient silent(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  std::vector<ConnectionSample> samples;
  ASSERT_TRUE(silent.ServeUntil(host_, [&] {
    samples = host_.SampleConnections();
    return !samples.empty();
  }));

  ASSERT_EQ(samples.size(), 1U);
  EXPECT_FALSE(samples.front().session.has_value());
}

// Requirements: US-01
TEST_F(JoinTest, AClientWithAnotherEngineVersionIsRefusedForTheVersion) {
  Session& client = AddClient("0.0.0-not-the-servers");

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kVersionMismatch);
  EXPECT_FALSE(client.GetSessionId().has_value());
  EXPECT_EQ(client.GetPhase(), Phase::kNotAdmitted);
}

// Requirements: US-01
TEST_F(JoinTest, ARefusedClientReportsTheRefusalAsItsFailure) {
  Session& client = AddClient("0.0.0-not-the-servers");

  ASSERT_TRUE(WaitForAnswers());

  const auto failure = client.GetFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->kind, FailureKind::kRefused);
  EXPECT_EQ(failure->refusal, JoinRefusal::kVersionMismatch);
}

// Requirements: US-01
TEST_F(JoinTest, AClientWithAnotherClientPackIsRefusedForThePack) {
  Session& client = AddClient(std::string(augusta::EngineVersion()), kCharacter, ClientPack(2));

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kPackMismatch);
  EXPECT_FALSE(client.GetSessionId().has_value());
}

// Requirements: US-02
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

// Requirements: US-01
TEST_F(JoinTest, AWrongVersionIsReportedBeforeAnUnknownCharacter) {
  Session& client = AddClient("0.0.0-not-the-servers", "characters/nobody");

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_EQ(client.GetRefusal(), JoinRefusal::kVersionMismatch);
}

// Requirements: US-01
TEST_F(JoinTest, AnAdmittedClientHasNoFailure) {
  Session& client = AddClient();

  ASSERT_TRUE(WaitForAnswers());

  EXPECT_FALSE(client.GetFailure().has_value());
}

// Requirements: US-02
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

// Requirements: US-02
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
// Requirements: US-02
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
// Requirements: US-04, US-05, NFR-01, NFR-06
TEST_F(JoinTest, EightClientsMoveSprintAndChangeStanceForARoundWithNoMissedTicks) {
  constexpr int kRoundTicks = 600;             // 10 real seconds at kTestTickRate, paced.
  constexpr std::uint32_t kAckTolerance = 20;  // A few round trips' worth still in flight.
  constexpr std::array<Stance, 3> kStanceCycle = {Stance::kStanding, Stance::kCrouching, Stance::kProne};
  constexpr int kStanceCycleTicks = 150;
  constexpr int kSprintBlockTicks = 100;

  StartAFullMatch();

  std::vector<Vec3> first_position(sessions_.size());
  std::vector<Vec3> last_position(sessions_.size());
  std::vector<augusta::command::Sequence> max_acknowledged(sessions_.size(), 0);

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

    host_.PumpNetwork(std::chrono::steady_clock::now());
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
    EXPECT_GE(max_acknowledged[i] + kAckTolerance, static_cast<augusta::command::Sequence>(kRoundTicks))
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
            Scenario{.collision = collision, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
  augusta::prediction::World world = EmptyWorld();
  for (const CollisionMesh& mesh : collision) {
    EXPECT_TRUE(world.AddCollisionMesh(mesh).has_value());
  }
  Session session(TestSessionConfig(host.ListenEndpoint()), std::move(world));
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
      Scenario{.collision = {FloorAt(0.0F)}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});

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
           Scenario{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {hitbox}}}}),
      std::runtime_error);
}

TEST(MapHostTest, AHostRefusesAMapMeshPhysicsRejects) {
  EXPECT_THROW(Host(TestHostConfig(), Scenario{.collision = {CollisionMesh{}},
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
      : host_(TestHostConfig(), Scenario{.collision = std::move(server_map),
                                         .spawn_points = {},
                                         .characters = {{.path = kCharacter, .hitboxes = {}}}}),
        session_(TestSessionConfig(host_.ListenEndpoint()), WorldWithFloorAt(kGroundHeight)) {}

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
    host_.PumpNetwork(std::chrono::steady_clock::now());
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
  augusta::command::Sequence DrainServer(int last_sequence) {
    constexpr int kMaxTicks = 120;
    augusta::command::Sequence acknowledged = session_.GetAuthoritativeState()->acknowledged_sequence;
    for (int i = 0; i < kMaxTicks && acknowledged < static_cast<augusta::command::Sequence>(last_sequence); ++i) {
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

// Requirements: US-04
TEST_F(MovementTest, AForwardCommandMovesTheAuthoritativePlayerAndTheStateReachesTheClient) {
  const float start = Self().position.x;

  for (int i = 0; i < 60; ++i) {
    Step(Walking());
  }

  EXPECT_GT(Self().position.x, start + 2.0F);
}

// Requirements: US-04
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

// Requirements: US-04
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

// Requirements: US-04, NFR-02
TEST_F(MovementTest, ALostDatagramDoesNotLoseAMovementCommand) {
  constexpr int kDeliveredSteps = 2;
  constexpr int kLostCommands = 3;
  for (int i = 0; i < kDeliveredSteps; ++i) {
    Step(Walking());
  }

  augusta::command::Sequence previous = session_.GetAuthoritativeState()->acknowledged_sequence;

  // CommandsWire the server never hears, then one that arrives together with them.
  augusta::networking::SimulateNetworkConditions({.loss_percent = 100.0F});
  for (int i = 0; i < kLostCommands; ++i) {
    session_.Tick(Walking(), kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
  }
  augusta::networking::SimulateNetworkConditions({});
  session_.Tick(Walking(), kFixedTick);
  const augusta::command::Sequence last_sent = kSettleTicks + kDeliveredSteps + kLostCommands + 1;

  // The server consumes one command per tick, so every sequence passes through
  // the acknowledgement in turn; a lost one would make it skip.
  augusta::command::Sequence acknowledged = 0;
  for (int i = 0; i < 12; ++i) {
    ServerTickAndDeliver();
    acknowledged = session_.GetAuthoritativeState()->acknowledged_sequence;
    EXPECT_LE(acknowledged, previous + 1) << "the server skipped a command";
    previous = acknowledged;
  }

  EXPECT_EQ(acknowledged, last_sent);
}

// Requirements: US-04, NFR-02
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

// Requirements: US-04, NFR-02
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
    augusta::command::Sequence acknowledged_sequence = 0;
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
        const augusta::tick::Tick last_tick = session_.GetAuthoritativeState()->tick;
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
      host_.PumpNetwork(std::chrono::steady_clock::now());
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

// Requirements: NFR-02
TEST_F(PacingTest, AClientWhoseClockRunsTwoPercentFastKeepsTheServersQueueOfItsCommandsShort) {
  ExpectPacedOnTarget(RunPaced(1.02, kPacedTicks));
}

// Requirements: NFR-02
TEST_F(PacingTest, AClientWhoseClockRunsTwoPercentSlowKeepsTheServerFromRunningOutOfItsCommands) {
  ExpectPacedOnTarget(RunPaced(0.98, kPacedTicks));
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

// Requirements: NFR-02
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
    Scenario scenario;
    augusta::scripting::Engine policy;
  };

  explicit LoopbackMatch(HostSetup setup) : host_(setup.config, std::move(setup.scenario), std::move(setup.policy)) {}

  // A host setup for the floor with spawn_points, the parameters and the tick rate.
  static HostSetup OnTheFloor(std::vector<Vec3> spawn_points, const Parameters& parameters = kTestParameters,
                              std::uint8_t tick_rate_hz = kTestTickRate) {
    return HostSetup{.config = TestHostConfig(parameters, tick_rate_hz),
                     .scenario = Scenario{.collision = {FloorAt(kFloorY)},
                                          .spawn_points = std::move(spawn_points),
                                          .characters = {{.path = kCharacter, .hitboxes = {}}}},
                     .policy = {}};
  }

  // Connects a new client and runs the network until the server has answered
  // it, admitted or not. The client's own stamina rules are none: any it uses
  // came from the server.
  Session& Connect() {
    sessions_.push_back(
        std::make_unique<Session>(TestSessionConfig(host_.ListenEndpoint()), WorldWithFloorAt(kFloorY)));
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
    host_.PumpNetwork(std::chrono::steady_clock::now());
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
  augusta::simulation::TickResult StepEach(const std::vector<Command>& commands) {
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
    augusta::simulation::TickResult result = host_.Tick(kFixedTick);
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    return result;
  }

  // A StepEach on which every client does command.
  augusta::simulation::TickResult Step(const Command& command = Command{}) {
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
    augusta::simulation::State state = host_.Tick(kFixedTick).state;
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

// Requirements: US-03
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

// Requirements: US-03
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

// Requirements: US-03
TEST_F(SpawnTest, AClientsPredictionStartsAtTheSpawnPointMatchStartGaveIt) {
  Join();
  Session& second = Join();
  ASSERT_TRUE(StartMatch());

  Run(1);

  const augusta::physics::BodyState& body = states_.at(&second).local_body;
  EXPECT_NEAR(body.position.x, SpawnPoints()[1].x, 0.1F);
  EXPECT_NEAR(body.position.z, SpawnPoints()[1].z, 0.1F);
}

// Requirements: US-03
TEST_F(SpawnTest, EveryClientIsToldEveryPlayersCharacterAndSpawnPointAtMatchStart) {
  Session& first = Join();
  Session& second = Join();

  ASSERT_TRUE(StartMatch());

  for (const Session* client : {&first, &second}) {
    const auto start = client->GetMatchStart();
    ASSERT_TRUE(start.has_value());
    ASSERT_EQ(start->players.size(), 2U);
    EXPECT_EQ(start->players[0].session, *first.GetSessionId());
    EXPECT_EQ(start->players[0].character, kCharacter);
    EXPECT_EQ(start->players[0].spawn, SpawnPoints()[0]);
    EXPECT_EQ(start->players[1].session, *second.GetSessionId());
    EXPECT_EQ(start->players[1].character, kCharacter);
    EXPECT_EQ(start->players[1].spawn, SpawnPoints()[1]);
  }
}

// Three players on the floor, whose scenario's rules hand the Map's
// three Spawn points out backwards: the last player in the Match takes the first.
class PolicySpawnTest : public LoopbackMatch {
 protected:
  static std::vector<Vec3> SpawnPoints() {
    return {Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F), Vec3(30.0F, kFloorY, -5.0F)};
  }

  static augusta::scripting::Engine Backwards() {
    auto policy = augusta::scripting::Engine::Load(R"(
      function assign_spawns(match)
        local assignment = {}
        for i, player in ipairs(match.players) do
          assignment[i] = {session = player.session, spawn_point = match.spawn_points - i + 1}
        end
        return assignment
      end
    )");
    EXPECT_TRUE(policy.has_value());
    return policy ? *std::move(policy) : augusta::scripting::Engine{};
  }

  static HostSetup BackwardsSetup() {
    HostSetup setup = OnTheFloor(SpawnPoints(), WithPlayerCount(3));
    setup.policy = Backwards();
    return setup;
  }

  PolicySpawnTest() : LoopbackMatch(BackwardsSetup()) {}
};

// Requirements: US-03
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

// Requirements: US-03
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

// Requirements: US-03
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

// Requirements: US-02
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
    EXPECT_EQ(lobby->roster[0].character, kCharacter);
    EXPECT_EQ(lobby->roster[1].session, *second.GetSessionId());
    EXPECT_EQ(lobby->roster[1].character, kCharacter);
    EXPECT_EQ(client->GetPhase(), Phase::kLobby);
  }
  EXPECT_EQ(first.GetLobby()->version, second.GetLobby()->version);
}

// Requirements: US-02
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

// Requirements: US-02
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

// Requirements: US-02
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
  EXPECT_TRUE(host_.Tick(kFixedTick).state.bodies.empty()) << "started with a client not ReadyWire";

  sessions_[2]->ReportReady(sessions_[2]->GetLobby()->version);
  Settle(host_, All());

  EXPECT_EQ(host_.Tick(kFixedTick).state.bodies.size(), 3U);
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [](const auto& s) { return s->GetPhase() == Phase::kMatch; });
  }));
}

using ReadyTest = MatchOf<2>;

// Requirements: US-02
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
  EXPECT_TRUE(host_.Tick(kFixedTick).state.bodies.empty()) << "started on a ReadyWire for an older Roster";

  first.ReportReady(first.GetLobby()->version);
  Settle(host_, All());

  EXPECT_EQ(host_.Tick(kFixedTick).state.bodies.size(), 2U);
}

// Requirements: US-02
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
  EXPECT_LE(first.GetAuthoritativeState()->acknowledged_sequence, static_cast<augusta::command::Sequence>(kSteps));
  EXPECT_GT(states_.at(&first).local_body.position.x, OwnSpawn(first).x);
}

using MidMatchTest = MatchOf<3>;

// Requirements: NFR-06
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

// Requirements: US-14
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

// A Server view is one moment of what the server has said (ADR-0005): one taken
// during a match still holds that match after it ends, while one taken after
// shows the Lobby.
TEST_F(MatchCycleTest, AServerViewKeepsTheMomentItWasTakenAt) {
  Session& client = Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  const std::shared_ptr<const ServerView> in_match = client.GetServerView();

  host_.EndMatch();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return client.GetPhase() == Phase::kLobby; }));

  EXPECT_EQ(in_match->GetPhase(), Phase::kMatch);
  EXPECT_TRUE(in_match->authoritative.has_value());
  EXPECT_FALSE(in_match->match_end.has_value());
  EXPECT_EQ(in_match->OwnEntity(), client.GetEntityId());
  EXPECT_TRUE(in_match->OwnAlive());
  const std::shared_ptr<const ServerView> in_lobby = client.GetServerView();
  EXPECT_EQ(in_lobby->GetPhase(), Phase::kLobby);
  EXPECT_FALSE(in_lobby->authoritative.has_value());
  EXPECT_TRUE(in_lobby->match_end.has_value());
  EXPECT_FALSE(in_lobby->OwnAlive());
}

// Requirements: US-14
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

// Requirements: US-14
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
    ASSERT_TRUE(host_.Tick(kFixedTick).state.bodies.empty()) << "started " << PauseTicks() - i << " ticks early";
  }

  EXPECT_EQ(host_.Tick(kFixedTick).state.bodies.size(), 2U);
}

// Requirements: US-03, US-14
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
  EXPECT_EQ(start->players[0].character, kCharacter);
  EXPECT_EQ(start->players[0].spawn, SpawnPoints()[0]);
  EXPECT_EQ(start->players[1].session, second_session);
  EXPECT_EQ(start->players[1].spawn, SpawnPoints()[1]);
  // Each client's prediction starts over where the new match put it.
  EXPECT_NEAR(states_.at(&first).local_body.position.x, SpawnPoints()[0].x, 0.1F);
  EXPECT_NEAR(states_.at(&second).local_body.position.x, SpawnPoints()[1].x, 0.1F);
}

// Requirements: US-14
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

// A host on the floor that records every tick it runs (ADR-0048), for a match
// of two.
class RecordingHostTest : public LoopbackMatch {
 protected:
  static std::vector<Vec3> SpawnPoints() { return {Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F)}; }

  static HostSetup FloorSetup() { return OnTheFloor(SpawnPoints(), WithPlayerCount(2)); }

  // Unique to this process: ctest may run the tests of this suite side by side.
  static const std::filesystem::path& RecordingPath() {
    static const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("augusta_session_recording_" + std::to_string(std::random_device{}()) + ".rec");
    return path;
  }

  static HostSetup RecordingSetup() {
    HostSetup setup = FloorSetup();
    setup.config.recording = RecordingPath();
    return setup;
  }

  RecordingHostTest() : LoopbackMatch(RecordingSetup()) {}

  // After every Host of the suite has closed the file.
  static void TearDownTestSuite() { std::filesystem::remove(RecordingPath()); }

  // The recording once its writer has reached the tick the match ended
  // before: the Host, which outlives the test, writes it on a thread of its own.
  static augusta::server::Recording ReadBack() {
    constexpr auto kPatience = std::chrono::seconds(5);
    constexpr auto kRetryAfter = std::chrono::milliseconds(10);
    const auto deadline = std::chrono::steady_clock::now() + kPatience;
    while (true) {
      std::ifstream in(RecordingPath(), std::ios::binary);
      auto recording = augusta::server::ReadRecording(in);
      const bool written =
          recording.has_value() && !recording->ticks.empty() && recording->ticks.back().input.match_ended;
      if (written || std::chrono::steady_clock::now() >= deadline) {
        EXPECT_TRUE(recording.has_value());
        return recording.value_or(augusta::server::Recording{});
      }
      std::this_thread::sleep_for(kRetryAfter);
    }
  }

  // The match the tests record: two clients join, walk forward, fire, and the
  // host ends the match.
  void PlayAMatch() {
    Join();
    Join();
    ASSERT_TRUE(StartMatch());
    Command fire = Forward();
    fire.fire = true;
    Run(kSettleTicks, fire);
    host_.EndMatch();
    ServerTick();
  }
};

TEST_F(RecordingHostTest, EveryTickTheHostRanIsRecordedWithTheCommandsItTookIn) {
  PlayAMatch();
  const augusta::server::Recording recording = ReadBack();

  ASSERT_FALSE(recording.ticks.empty());
  EXPECT_EQ(recording.header.tick_rate_hz, kTestTickRate);
  EXPECT_EQ(recording.header.engine_version, augusta::EngineVersion());
  const auto started = std::ranges::find_if(
      recording.ticks, [](const augusta::server::TickRecord& tick) { return !tick.input.match_start.empty(); });
  ASSERT_NE(started, recording.ticks.end());
  EXPECT_EQ(started->input.match_start.size(), 2U);
  const bool walked_forward = std::ranges::any_of(recording.ticks, [](const augusta::server::TickRecord& tick) {
    return tick.input.commands.size() == 2 && std::ranges::all_of(tick.input.commands, [](const auto& command) {
             return command.command.movement.direction.x == 1.0F && command.command.fire;
           });
  });
  EXPECT_TRUE(walked_forward);
  EXPECT_TRUE(recording.ticks.back().input.match_ended);
}

TEST(RecordingHostConfigTest, AHostRefusesARecordingItCannotWrite) {
  HostConfig config = TestHostConfig();
  // A directory, which no file can be opened as.
  config.recording = std::filesystem::temp_directory_path();
  EXPECT_THROW(Host(config, Scenario{.collision = {}, .spawn_points = {}, .characters = {}, .client_pack = {}}),
               std::runtime_error);
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

// Requirements: US-05
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
// Requirements: US-05, NFR-02
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

// Requirements: US-05
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

// Requirements: US-05
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
// Requirements: US-05, NFR-02
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
    const auto loaded = augusta::server::LoadParameters(
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
        "}",
        kTestTickRate);
    return loaded.value();
  }

  ScriptedParametersTest() : LoopbackMatch(OnTheFloor({}, LoadScript())) {}
};

// Requirements: US-05
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

// Requirements: US-07, US-08
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
  ScriptedServer() : server_(Endpoint{.address = kLoopbackAnyPort}) {}

  // The address it listens on, for its client to connect to.
  [[nodiscard]] Endpoint LocalEndpoint() const { return server_.LocalEndpoint(); }

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
                                                 .character = kCharacter});
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
        .players = {{.spawn = {}, .session = kScriptedSession, .entity = kScriptedEntity, .character = kCharacter}}};
  }

  // An Authoritative State of tick listing entities, each at the origin, with
  // the client's player unhurt.
  static augusta::protocol::AuthoritativeStateWire StateOf(augusta::tick::Tick tick,
                                                           const std::vector<EntityIdWire>& entities) {
    augusta::protocol::AuthoritativeStateWire state{
        .tick = tick, .bodies = {}, .rifle = {}, .health = 100.0F, .acknowledged_sequence = 0};
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

  ScriptedServerTest() : session_(TestSessionConfig(server_.LocalEndpoint()), WorldWithFloorAt(0.0F)) {}

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

// The server's tick never starts over (ADR-0038): past the last one 32 bits
// hold, a newer state is still newer and an older one still stale, and the
// client keeps sending its commands.
TEST_F(ScriptedServerTest, StatesPastThirtyTwoBitsOfTicksAreStillNewestWins) {
  constexpr augusta::tick::Tick kLastOf32Bits = std::numeric_limits<std::uint32_t>::max();
  Settle();

  server_.Send(ScriptedServer::StateOf(kLastOf32Bits, {kScriptedEntity}));
  Settle();
  ASSERT_TRUE(session_.GetAuthoritativeState().has_value());
  EXPECT_EQ(session_.GetAuthoritativeState()->tick, kLastOf32Bits);

  server_.Send(ScriptedServer::StateOf(kLastOf32Bits + 1, {kScriptedEntity}));
  Settle();
  EXPECT_EQ(session_.GetAuthoritativeState()->tick, kLastOf32Bits + 1);

  server_.Send(ScriptedServer::StateOf(kLastOf32Bits, {kScriptedEntity}));
  Settle();
  EXPECT_EQ(session_.GetAuthoritativeState()->tick, kLastOf32Bits + 1);

  const int sent_before = server_.CommandsReceived();
  session_.Tick(Command{}, kFixedTick);
  Settle();
  EXPECT_GT(server_.CommandsReceived(), sent_before);
}

// Requirements: US-14
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

// Requirements: US-14
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
      .players = {{.spawn = {}, .session = SessionIdWire{2}, .entity = EntityIdWire{102}, .character = kCharacter}}});
  Settle();

  EXPECT_EQ(session_.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(session_.GetMatchStart().has_value());
}

// Requirements: US-03
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
      .players = {{.spawn = spawn, .session = kScriptedSession, .entity = kScriptedEntity, .character = kCharacter}}});
  Settle();
  const augusta::prediction::State first = session_.Tick(Command{}, kFixedTick);
  Settle();

  EXPECT_NEAR(first.local_body.position.x, spawn.x, 0.1F);
  EXPECT_NEAR(first.local_body.position.z, spawn.z, 0.1F);
  EXPECT_GT(server_.CommandsReceived(), 0);
}

// Requirements: US-02
TEST_F(ScriptedLobbyTest, ReadyIsSentOnlyWhenToldAndOnlyForTheNewestRoster) {
  server_.Send(
      augusta::protocol::LobbyWire{.version = 1, .roster = {{.session = kScriptedSession, .character = kCharacter}}});
  server_.Send(augusta::protocol::LobbyWire{.version = 2,
                                            .roster = {{.session = kScriptedSession, .character = kCharacter},
                                                       {.session = SessionIdWire{2}, .character = kCharacter}}});
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
  ScriptedServer server;
  Parameters threshold_of_one;
  threshold_of_one.stamina.forced_walk_below = 1.0F;
  for (const Parameters& bad :
       {Parameters{.stamina = {.deplete_per_second = -1.0F}},
        Parameters{.stamina = {.regen_per_second = std::numeric_limits<float>::quiet_NaN()}}, threshold_of_one,
        Parameters{.player_count = 0}, Parameters{.player_count = augusta::protocol::kMaxPlayers + 1}}) {
    // One server for every case: it answers whichever session sent last.
    Session session(TestSessionConfig(server.LocalEndpoint()), EmptyWorld());

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
            Scenario{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
  Session session(TestSessionConfig(host.ListenEndpoint()), EmptyWorld());
  session.Connect();

  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  ExchangeUntil(host, {&session}, [&] { return std::chrono::steady_clock::now() >= until; });

  EXPECT_FALSE(session.GetSessionId().has_value());
  EXPECT_FALSE(session.GetTickRate().has_value());
  EXPECT_FALSE(session.GetParameters().has_value());
}

// What a client is told when its session ends on its own.
TEST(SessionFailureTest, ASessionThatNeverConnectedHasNoFailure) {
  Session session(TestSessionConfig(UnusedLoopbackEndpoint()), EmptyWorld());

  EXPECT_FALSE(session.GetFailure().has_value());
}

// Requirements: US-01
TEST(SessionFailureTest, AServerNobodyIsListeningAtIsUnreachable) {
  // Set before connecting: the timeout only reaches new connections.
  augusta::networking::SimulateNetworkConditions({.timeout_ms = 500});
  Session session(TestSessionConfig(UnusedLoopbackEndpoint()), EmptyWorld());
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

// Requirements: US-01
TEST(SessionFailureTest, AServerThatGoesAwayDuringAMatchIsAConnectionLost) {
  auto host = std::make_unique<Host>(
      TestHostConfig(),
      Scenario{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
  Session session(TestSessionConfig(host->ListenEndpoint()), EmptyWorld());
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
            Scenario{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
  Session session(TestSessionConfig(host.ListenEndpoint()), EmptyWorld());
  session.Connect();
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (session.GetConnectionState() != ConnectionState::kConnected && std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork(std::chrono::steady_clock::now());
    session.PumpEvents();
    std::this_thread::sleep_for(kPollInterval);
  }
  ASSERT_EQ(session.GetConnectionState(), ConnectionState::kConnected);

  session.Disconnect();
  session.PumpEvents();

  EXPECT_FALSE(session.GetFailure().has_value());
}

// Requirements: US-01
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
using FullMatchRobustnessTest = RobustnessOf<augusta::protocol::kMaxPlayers>;

// Requirements: NFR-06
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

// Requirements: NFR-06
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

// US-15 and NFR-05: the catalogue of impossible actions
// (tests/impossible_actions.md), entry by entry, through a real Host and the
// wire alone. An adversarial client sends by hand what no real client sends,
// and each test checks only what the clients are told - their Authoritative
// State, their Shots, the replies to them - never the server's counters.

// A match of an adversary and an honest bystander on the floor, each with a
// rifle of kMagazine rounds that fires every six ticks. Every command the
// adversary sends walks, turns and fires, unless a test makes it impossible,
// so one the server took in shows in what both are told.
class ImpossibleCommandTest : public LoopbackMatch {
 protected:
  static constexpr std::uint8_t kMagazine = 15;
  // The ticks run after the adversary sends: far more than its message takes
  // to arrive and a command takes to be processed.
  static constexpr int kTicks = 20;
  // How far a body resting on the floor may drift between two states: far less
  // than one command's walk.
  static constexpr float kStill = 0.01F;

  // What the adversary has been told of itself, and the bystander of the adversary.
  struct Told {
    augusta::command::Sequence acknowledged = 0;
    std::uint8_t rounds = 0;
    Vec3 position{};
    float yaw = 0.0F;
    std::optional<Vec3> seen_by_bystander;
    // The Shots each has been told of so far.
    std::size_t shots = 0;
    std::size_t bystander_shots = 0;
  };

  static HostSetup Armed() {
    Parameters parameters = WithPlayerCount(2);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = kMagazine;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.ammo.max_range = 1000.0F;
    return OnTheFloor({Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F)}, parameters);
  }

  ImpossibleCommandTest() : LoopbackMatch(Armed()) {}

  void SetUp() override {
    bystander_ = &Join();
    ASSERT_TRUE(adversary_.Join(host_));
    ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_), &adversary_));
    const auto entity = adversary_.Entity();
    ASSERT_TRUE(entity.has_value());
    entity_ = *entity;
    Run(kSettleTicks);
    RunAndTell();
    ASSERT_TRUE(adversary_.NewestState().has_value());
  }

  // A command a real client could send: walking, turned half a radian, firing.
  static protocol::SequencedCommandWire Acting(augusta::command::Sequence sequence) {
    protocol::SequencedCommandWire acting{.sequence = sequence};
    acting.command.direction = Vec3(1.0F, 0.0F, 0.0F);
    acting.command.yaw = 0.5F;
    acting.command.flags = protocol::CommandWire::kFire;
    return acting;
  }

  // A message of as many Commands as the protocol allows, each acting, numbered from 1.
  static protocol::CommandsWire MostCommands() {
    protocol::CommandsWire most;
    for (std::size_t i = 1; i <= protocol::kMaxCommandsPerMessage; ++i) {
      most.commands.push_back(Acting(i));
    }
    return most;
  }

  // Runs the match for kTicks with the bystander standing still, lets what is on
  // its way arrive, and returns what the two have been told.
  Told RunAndTell() {
    Run(kTicks);
    Settle(host_, Pointers(sessions_));
    adversary_.Receive();
    bystander_shots_ += bystander_->TakeShots().size();
    return Now();
  }

  // What the two have been told so far.
  [[nodiscard]] Told Now() const {
    Told told;
    told.shots = adversary_.ReceivedOf<protocol::ShotWire>().size();
    told.bystander_shots = bystander_shots_;
    const auto state = adversary_.NewestState();
    if (!state.has_value()) {
      ADD_FAILURE() << "the adversary has been told no state";
      return told;
    }
    told.acknowledged = state->acknowledged_sequence;
    told.rounds = state->rifle.rounds;
    for (const auto& body : state->bodies) {
      if (body.entity == entity_) {
        told.position = body.body.position;
        told.yaw = body.yaw;
      }
    }
    told.seen_by_bystander = PositionSeenBy(*bystander_, static_cast<EntityId>(std::to_underlying(entity_)));
    return told;
  }

  // Expects the server to have taken in nothing between before and after: no
  // command acknowledged, no round fired, the adversary's body neither moved nor turned.
  static void ExpectNothingTaken(const Told& before, const Told& after) {
    EXPECT_EQ(after.acknowledged, before.acknowledged);
    EXPECT_EQ(after.rounds, before.rounds);
    EXPECT_EQ(after.shots, before.shots);
    EXPECT_EQ(after.bystander_shots, before.bystander_shots);
    EXPECT_NEAR(after.position.x, before.position.x, kStill);
    EXPECT_NEAR(after.position.z, before.position.z, kStill);
    EXPECT_EQ(after.yaw, before.yaw);
    ASSERT_TRUE(after.seen_by_bystander.has_value());
    EXPECT_NEAR(after.seen_by_bystander->x, after.position.x, kStill);
    EXPECT_NEAR(after.seen_by_bystander->z, after.position.z, kStill);
  }

  // Sends one honest command numbered right after the last acknowledged, and
  // expects the server to take it in: whatever it refused since before left it
  // as though it had never been sent.
  void ExpectTheNextHonestCommandTaken(const Told& before) {
    adversary_.Send(protocol::CommandsWire{.commands = {Acting(before.acknowledged + 1)}});
    const Told after = RunAndTell();
    EXPECT_EQ(after.acknowledged, before.acknowledged + 1);
    EXPECT_EQ(after.rounds, before.rounds - 1);
    EXPECT_EQ(after.shots, before.shots + 1);
  }

  // Sends commands in one message and expects the server to take in none of them.
  void ExpectRejected(const std::vector<protocol::SequencedCommandWire>& commands) {
    const Told before = Now();
    adversary_.Send(protocol::CommandsWire{.commands = commands});
    ExpectNothingTaken(before, RunAndTell());
    ExpectTheNextHonestCommandTaken(before);
  }

  RawClient adversary_{host_.ListenEndpoint()};
  EntityIdWire entity_{};
  Session* bystander_ = nullptr;
  std::size_t bystander_shots_ = 0;
};

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, AMovementLongerThanAnyInputDeviceProducesIsRejected) {
  auto too_long = Acting(1);
  too_long.command.direction = Vec3(1.5F, 0.0F, 1.5F);
  auto too_long_upward = Acting(2);
  too_long_upward.command.direction = Vec3(-1.2F, 1.2F, -1.2F);

  ExpectRejected({too_long, too_long_upward});
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, APitchPastStraightUpOrDownIsRejected) {
  auto past_up = Acting(1);
  past_up.command.pitch = 3.0F;
  auto past_down = Acting(2);
  past_down.command.pitch = -1.7F;

  ExpectRejected({past_up, past_down});
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, AYawOutsideOneTurnIsRejected) {
  auto past_half_a_turn = Acting(1);
  past_half_a_turn.command.yaw = 3.9F;
  auto past_half_a_turn_back = Acting(2);
  past_half_a_turn_back.command.yaw = -3.3F;

  ExpectRejected({past_half_a_turn, past_half_a_turn_back});
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, ARejectedCommandLeavesTheGoodOnesOfItsMessageTakenIn) {
  // 1 is good; 2 and 3 are impossible; 4 is good but neither walks nor fires.
  auto impossible_yaw = Acting(2);
  impossible_yaw.command.yaw = 3.9F;
  auto impossible_pitch = Acting(3);
  impossible_pitch.command.pitch = 3.0F;
  protocol::SequencedCommandWire still{.sequence = 4};
  still.command.yaw = Acting(1).command.yaw;
  const Told before = Now();

  adversary_.Send(protocol::CommandsWire{.commands = {Acting(1), impossible_yaw, impossible_pitch, still}});
  const Told after = RunAndTell();

  // Only the first fired, and nothing turned the body past the good commands' view.
  EXPECT_EQ(after.acknowledged, 4U);
  EXPECT_EQ(after.rounds, before.rounds - 1);
  EXPECT_EQ(after.shots, before.shots + 1);
  EXPECT_EQ(after.yaw, Acting(1).command.yaw);
}

// The wire carries a Command's numbers as whole counts of their grids
// (augusta/grid.h), so it has nowhere to carry a NaN or an infinity: a NaN a
// client puts in arrives as 0, an infinity as its grid's bound.
// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, NoNumberOfACommandReachesTheServerAsNaNOrInfinity) {
  constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float kInfinity = std::numeric_limits<float>::infinity();
  const Told before = Now();

  // Arrives as a command that fires and neither walks nor turns, which a real client could send.
  auto not_numbers = Acting(1);
  not_numbers.command.direction = Vec3(kNaN, kNaN, kNaN);
  not_numbers.command.yaw = kNaN;
  not_numbers.command.pitch = kNaN;
  not_numbers.command.seen_fraction = kNaN;
  adversary_.Send(protocol::CommandsWire{.commands = {not_numbers}});
  const Told after_nan = RunAndTell();

  EXPECT_EQ(after_nan.acknowledged, 1U);
  EXPECT_EQ(after_nan.rounds, before.rounds - 1);
  EXPECT_EQ(after_nan.yaw, 0.0F);
  EXPECT_NEAR(after_nan.position.x, before.position.x, kStill);
  EXPECT_NEAR(after_nan.position.z, before.position.z, kStill);
  const auto shots = adversary_.ReceivedOf<protocol::ShotWire>();
  ASSERT_FALSE(shots.empty());
  EXPECT_EQ(shots.back().yaw, 0.0F);
  EXPECT_EQ(shots.back().pitch, 0.0F);

  // Arrive at their grids' bounds: out of range for a yaw, a pitch, or a movement on more than one axis.
  auto infinite_yaw = Acting(2);
  infinite_yaw.command.yaw = kInfinity;
  auto infinite_pitch = Acting(3);
  infinite_pitch.command.pitch = -kInfinity;
  auto infinite_movement = Acting(4);
  infinite_movement.command.direction = Vec3(-kInfinity, kInfinity, kInfinity);
  for (auto* infinite : {&infinite_yaw, &infinite_pitch, &infinite_movement}) {
    infinite->command.seen_fraction = kInfinity;
  }
  ExpectRejected({infinite_yaw, infinite_pitch, infinite_movement});
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, ACommandWhoseSequenceIsNotNewerThanTheLastTakenInRepeatsNothing) {
  adversary_.Send(protocol::CommandsWire{.commands = {Acting(1)}});
  const Told fired = RunAndTell();
  ASSERT_EQ(fired.acknowledged, 1U);
  ASSERT_EQ(fired.rounds, kMagazine - 1);

  // A command that changes nothing but the acknowledgement: it keeps the view and neither walks nor fires.
  protocol::SequencedCommandWire still{.sequence = 3};
  still.command.yaw = Acting(1).command.yaw;
  // A replay of the first, then one that arrives after a newer one.
  adversary_.Send(protocol::CommandsWire{.commands = {Acting(1)}});
  adversary_.Send(protocol::CommandsWire{.commands = {still}});
  adversary_.Send(protocol::CommandsWire{.commands = {Acting(2)}});
  const Told after = RunAndTell();

  EXPECT_EQ(after.acknowledged, 3U);
  EXPECT_EQ(after.rounds, fired.rounds);
  EXPECT_EQ(after.shots, fired.shots);
  EXPECT_NEAR(after.position.x, fired.position.x, kStill);
  EXPECT_NEAR(after.position.z, fired.position.z, kStill);
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, BytesThatAreNoMessageChangeNothing) {
  using protocol::BytesWire;
  BytesWire truncated_state = protocol::Encode(protocol::AuthoritativeStateWire{.bodies = {{}}});
  truncated_state.resize(truncated_state.size() / 2);
  BytesWire commands_with_trailing_bytes = protocol::Encode(protocol::CommandsWire{.commands = {Acting(1)}});
  commands_with_trailing_bytes.push_back(std::byte{7});
  BytesWire truncated_commands = protocol::Encode(protocol::CommandsWire{.commands = {Acting(1)}});
  truncated_commands.pop_back();
  const BytesWire garbage[] = {
      BytesWire{},
      BytesWire{std::byte{0}},
      BytesWire{std::byte{0xFF}, std::byte{1}, std::byte{2}},
      truncated_state,
      commands_with_trailing_bytes,
      truncated_commands,
      BytesWire(64 * 1024, std::byte{0xAB}),
      BytesWire(1000, std::byte{static_cast<unsigned char>(protocol::MessageTypeWire::kCommands)}),
  };
  const Told before = Now();

  for (const BytesWire& payload : garbage) {
    adversary_.SendPayload(payload);
  }

  ExpectNothingTaken(before, RunAndTell());
  EXPECT_EQ(bystander_->GetPhase(), Phase::kMatch);
  EXPECT_EQ(bystander_->GetConnectionState(), ConnectionState::kConnected);
  ExpectTheNextHonestCommandTaken(before);
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, AMessageOnlyTheServerSendsChangesNothingWhenAClientSendsIt) {
  const auto bystander_health = bystander_->GetHealth();
  const EntityIdWire bystander{std::to_underlying(*bystander_->GetEntityId())};
  const SessionIdWire adversary = adversary_.ReceivedOf<protocol::JoinAcceptedWire>().front().session;
  const protocol::MessageWire server_only[] = {
      protocol::JoinAcceptedWire{.session = SessionIdWire{99}, .tick_rate_hz = 1, .character = kCharacter},
      protocol::JoinRefusedWire{.reason = protocol::JoinRefusalWire::kLobbyFull},
      protocol::AuthoritativeStateWire{.tick = 1'000'000,
                                       .bodies = {{.entity = entity_, .body = {.position = Vec3(100.0F, 0.0F, 0.0F)}}},
                                       .health = 1000.0F},
      protocol::LobbyWire{.version = 1, .roster = {}},
      protocol::MatchStartWire{.players = {}},
      protocol::MatchEndWire{.winner = adversary},
      protocol::ShotWire{.tick = 1, .shooter = entity_},
      protocol::HitConfirmationWire{.target = bystander, .damage = 1000.0F},
      protocol::DeathWire{.victim = bystander, .killer = entity_},
  };
  const Told before = Now();

  for (const protocol::MessageWire& message : server_only) {
    adversary_.Send(message);
  }

  ExpectNothingTaken(before, RunAndTell());
  EXPECT_EQ(bystander_->GetPhase(), Phase::kMatch);
  EXPECT_EQ(bystander_->GetHealth(), bystander_health);
  EXPECT_TRUE(bystander_->TakeHitConfirmations().empty());
  EXPECT_TRUE(bystander_->TakeDeaths().empty());
  EXPECT_EQ(bystander_->GetAuthoritativeState()->bodies.size(), 2U);
  EXPECT_EQ(bystander_->GetConnectionState(), ConnectionState::kConnected);
  ExpectTheNextHonestCommandTaken(before);
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, ACommandMessageWithMoreCommandsThanTheProtocolAllowsIsRefusedWhole) {
  // Encode writes no such message, so it is put together by hand: one of
  // kMaxCommandsPerMessage commands, with one more spliced in before its Seen tick.
  constexpr std::size_t kHeader = 2;  // The message type and the count.
  constexpr std::size_t kSeenTick = sizeof(augusta::tick::Tick);
  const protocol::BytesWire encoded = protocol::Encode(MostCommands());
  const protocol::BytesWire extra =
      protocol::Encode(protocol::CommandsWire{.commands = {Acting(protocol::kMaxCommandsPerMessage + 1)}});
  protocol::BytesWire too_many(encoded.begin(), encoded.end() - static_cast<std::ptrdiff_t>(kSeenTick));
  too_many[1] = static_cast<std::byte>(protocol::kMaxCommandsPerMessage + 1);
  too_many.insert(too_many.end(), extra.begin() + static_cast<std::ptrdiff_t>(kHeader), extra.end());
  ASSERT_EQ(protocol::Decode(too_many).error(), protocol::DecodeError::kFieldTooLong);
  const Told before = Now();

  adversary_.SendPayload(too_many);

  ExpectNothingTaken(before, RunAndTell());
  ExpectTheNextHonestCommandTaken(before);
}

// Requirements: US-15, NFR-05
TEST_F(ImpossibleCommandTest, CommandsFromAPeerThatHasNotBeenAdmittedMoveNothing) {
  RawClient intruder(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(intruder.Connect(host_));
  const EntityId bystander = *bystander_->GetEntityId();
  const auto bystander_before = PositionSeenBy(*bystander_, bystander);
  ASSERT_TRUE(bystander_before.has_value());
  const Told before = Now();

  intruder.Send(MostCommands());

  ExpectNothingTaken(before, RunAndTell());
  const auto bystander_after = PositionSeenBy(*bystander_, bystander);
  ASSERT_TRUE(bystander_after.has_value());
  EXPECT_NEAR(bystander_after->x, bystander_before->x, kStill);
  EXPECT_NEAR(bystander_after->z, bystander_before->z, kStill);
  EXPECT_EQ(bystander_->GetAuthoritativeState()->bodies.size(), 2U);
  intruder.Receive();
  EXPECT_EQ(intruder.ReceivedCount(), 0U);
}

// A Lobby of kPlayers on the floor, with three spawn points, whose scenario
// offers two characters, for the catalogue's entries about joining, Ready and
// Commands sent from the Lobby: honest Sessions, and
// adversaries a test scripts message by message.
template <std::uint8_t kPlayers>
class ImpossibleLobbyOf : public LoopbackMatch {
 protected:
  static constexpr const char* kOtherCharacter = "characters/other";
  // How long a test runs the Lobby to show no match starts: one that could
  // start would on the first of them.
  static constexpr int kLobbyTicks = 30;

  static HostSetup TwoCharacters() {
    HostSetup setup = OnTheFloor({Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F), Vec3(30.0F, kFloorY, -5.0F)},
                                 WithPlayerCount(kPlayers));
    setup.scenario.characters.push_back({.path = kOtherCharacter, .hitboxes = {}});
    return setup;
  }

  ImpossibleLobbyOf() : LoopbackMatch(TwoCharacters()) {}

  // Connects adversary, has it send request, and runs the network until the
  // server has answered it.
  bool AskToJoin(RawClient& adversary, const protocol::JoinRequestWire& request) {
    if (!adversary.Connect(host_)) {
      return false;
    }
    adversary.Send(request);
    return adversary.ServeUntil(host_, [&] {
      return !adversary.ReceivedOf<protocol::JoinAcceptedWire>().empty() ||
             !adversary.ReceivedOf<protocol::JoinRefusedWire>().empty();
    });
  }

  // Runs host and adversary until adversary has been told of a Roster holding
  // players players; returns that Roster's version, or nullopt past the deadline.
  std::optional<std::uint32_t> RosterVersionOf(RawClient& adversary, std::size_t players) {
    const auto told = [&] {
      const auto lobbies = adversary.ReceivedOf<protocol::LobbyWire>();
      return !lobbies.empty() && lobbies.back().roster.size() == players;
    };
    if (!adversary.ServeUntil(host_, told)) {
      return std::nullopt;
    }
    return adversary.ReceivedOf<protocol::LobbyWire>().back().version;
  }

  // Runs the Lobby for kLobbyTicks, every Session reporting Ready for each
  // Roster it is sent, and each of adversaries doing its own network work.
  void RunLobby(const std::vector<RawClient*>& adversaries) {
    std::map<const Session*, std::uint32_t> reported;
    for (int i = 0; i < kLobbyTicks; ++i) {
      host_.PumpNetwork(std::chrono::steady_clock::now());
      for (RawClient* adversary : adversaries) {
        adversary->Serve();
      }
      for (const auto& session : sessions_) {
        session->PumpEvents();
        session->ExchangeMessages();
        const auto lobby = session->GetLobby();
        if (lobby.has_value() && session->GetPhase() == Phase::kLobby && reported[session.get()] != lobby->version) {
          session->ReportReady(lobby->version);
          reported[session.get()] = lobby->version;
        }
      }
      host_.Tick(kFixedTick);
      std::this_thread::sleep_for(kNetworkDelay);
    }
  }

  // The Lobby the first Session was told of last, once what is on its way has arrived.
  augusta::harness::Lobby FirstLobby() {
    Settle(host_, Pointers(sessions_));
    return sessions_.front()->GetLobby().value();
  }
};

using ImpossibleReadyTest = ImpossibleLobbyOf<2>;

// Requirements: US-15, NFR-05
TEST_F(ImpossibleReadyTest, AReadyForAnyRosterButTheCurrentOneStartsNoMatch) {
  Session& bystander = Join();
  RawClient adversary(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(AskToJoin(adversary, HonestJoinRequest()));
  const auto current = RosterVersionOf(adversary, 2);
  ASSERT_TRUE(current.has_value());

  for (const std::uint32_t version : {*current - 1, *current + 1, 0U, std::numeric_limits<std::uint32_t>::max()}) {
    adversary.Send(protocol::ReadyWire{.version = version});
  }
  RunLobby({&adversary});

  EXPECT_EQ(bystander.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(adversary.InMatch());
  EXPECT_EQ(FirstLobby().version, *current);

  // The current one starts it at once: the Lobby was full, and the bystander Ready, all along.
  adversary.Send(protocol::ReadyWire{.version = *current});
  EXPECT_TRUE(DriveIntoMatch(host_, Pointers(sessions_), &adversary));
}

using ImpossibleRejoinTest = ImpossibleLobbyOf<3>;

// Requirements: US-15, NFR-05
TEST_F(ImpossibleRejoinTest, ASecondJoinFromAnAdmittedPlayerChangesNeitherThePlayerCountNorItsCharacter) {
  Join();
  RawClient adversary(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(AskToJoin(adversary, HonestJoinRequest()));
  const auto admitted = adversary.ReceivedOf<protocol::JoinAcceptedWire>();
  ASSERT_EQ(admitted.size(), 1U);
  const auto version = RosterVersionOf(adversary, 2);
  ASSERT_TRUE(version.has_value());

  auto as_another_character = HonestJoinRequest();
  as_another_character.character = kOtherCharacter;
  adversary.Send(as_another_character);
  adversary.Send(HonestJoinRequest());
  adversary.Send(protocol::ReadyWire{.version = *version});
  ASSERT_TRUE(
      adversary.ServeUntil(host_, [&] { return adversary.ReceivedOf<protocol::JoinAcceptedWire>().size() == 3; }));
  RunLobby({&adversary});

  // Each reply is the admission it already had.
  for (const auto& reply : adversary.ReceivedOf<protocol::JoinAcceptedWire>()) {
    EXPECT_EQ(reply.session, admitted.front().session);
    EXPECT_EQ(reply.character, admitted.front().character);
  }
  // Everyone is told of the same two players, and no match starts, though both
  // are Ready and a third would fill the Lobby.
  EXPECT_EQ(adversary.ReceivedOf<protocol::LobbyWire>().back().version, *version);
  const augusta::harness::Lobby lobby = FirstLobby();
  EXPECT_EQ(lobby.version, *version);
  ASSERT_EQ(lobby.roster.size(), 2U);
  for (const auto& entry : lobby.roster) {
    if (std::to_underlying(entry.session) == std::to_underlying(admitted.front().session)) {
      EXPECT_EQ(entry.character, admitted.front().character);
    }
  }
  EXPECT_EQ(sessions_.front()->GetPhase(), Phase::kLobby);
  EXPECT_FALSE(adversary.InMatch());

  // A third player fills it, and the match starts with three players, each once.
  Join();
  const auto full = RosterVersionOf(adversary, 3);
  ASSERT_TRUE(full.has_value());
  adversary.Send(protocol::ReadyWire{.version = *full});
  ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_), &adversary));
  const auto start = sessions_.front()->GetMatchStart();
  ASSERT_TRUE(start.has_value());
  std::set<SessionId> players;
  for (const auto& player : start->players) {
    players.insert(player.session);
    if (std::to_underlying(player.session) == std::to_underlying(admitted.front().session)) {
      EXPECT_EQ(player.character, admitted.front().character);
    }
  }
  EXPECT_EQ(start->players.size(), 3U);
  EXPECT_EQ(players.size(), 3U);
}

using ImpossibleJoinTest = ImpossibleLobbyOf<2>;

// Requirements: US-15, NFR-05
TEST_F(ImpossibleJoinTest, AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt) {
  Session& bystander = Join();
  const std::uint32_t version = FirstLobby().version;
  auto unknown_character = HonestJoinRequest();
  unknown_character.character = "characters/nobody";
  auto another_version = HonestJoinRequest();
  another_version.engine_version = "0.0.0-not-the-servers";
  auto another_pack = HonestJoinRequest();
  another_pack.client_pack.back() = std::byte{1};
  const std::pair<protocol::JoinRequestWire, protocol::JoinRefusalWire> requests[] = {
      {unknown_character, protocol::JoinRefusalWire::kUnknownCharacter},
      {another_version, protocol::JoinRefusalWire::kVersionMismatch},
      {another_pack, protocol::JoinRefusalWire::kPackMismatch},
  };
  std::vector<std::unique_ptr<RawClient>> adversaries;
  std::vector<RawClient*> serving;
  for (const auto& request : requests) {
    adversaries.push_back(std::make_unique<RawClient>(host_.ListenEndpoint(), RawClient::Mode::kScripted));
    serving.push_back(adversaries.back().get());
    ASSERT_TRUE(AskToJoin(*adversaries.back(), request.first));
  }

  RunLobby(serving);

  for (std::size_t i = 0; i < adversaries.size(); ++i) {
    const auto refused = adversaries[i]->ReceivedOf<protocol::JoinRefusedWire>();
    ASSERT_EQ(refused.size(), 1U) << "request " << i;
    EXPECT_EQ(refused.front().reason, requests[i].second) << "request " << i;
    EXPECT_TRUE(adversaries[i]->ReceivedOf<protocol::JoinAcceptedWire>().empty()) << "request " << i;
    EXPECT_TRUE(adversaries[i]->ReceivedOf<protocol::LobbyWire>().empty()) << "request " << i;
  }
  const augusta::harness::Lobby lobby = FirstLobby();
  EXPECT_EQ(lobby.version, version);
  EXPECT_EQ(lobby.roster.size(), 1U);
  EXPECT_EQ(bystander.GetPhase(), Phase::kLobby);
}

// How many times log holds needle.
std::size_t CountOccurrences(const std::string& log, const std::string& needle) {
  std::size_t count = 0;
  for (std::size_t at = log.find(needle); at != std::string::npos; at = log.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

// Bytes that are no message: what a misbehaving peer floods the server with.
const augusta::protocol::BytesWire kUndecodable{std::byte{0xFF}, std::byte{1}, std::byte{2}};

// More misbehaviour than the server tolerates, with plenty to spare.
constexpr std::size_t kFlood = 3 * augusta::server::kMisbehaviourThreshold;

// A peer that keeps sending what no honest client sends is disconnected
// (US-15), as an ordinary departure; routine rejections never disconnect it.
using MisbehaviourTest = RobustnessOf<2>;
using LobbyMisbehaviourTest = RobustnessOf<3>;

// Requirements: US-15, NFR-05
TEST_F(MisbehaviourTest, APeerFloodingMalformedMessagesIsDisconnectedAndItsBodyLeavesWhileAnHonestClientPlaysOn) {
  augusta::logging::Init();
  Session& honest = Join();
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));
  ASSERT_TRUE(DriveIntoMatch(host_, All(), &raw));
  Run(kSettleTicks);
  ASSERT_EQ(honest.GetAuthoritativeState()->bodies.size(), 2U);

  testing::internal::CaptureStdout();
  for (std::size_t i = 0; i < kFlood; ++i) {
    raw.SendPayload(kUndecodable);
  }
  Run(kSettleTicks, Forward());
  raw.Serve();
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_DEBUG
  // The heartbeat after the disconnect counts it; a DEBUG line, so only where
  // DEBUG is compiled in.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  host_.RecordTiming(augusta::tick::Timing{});
#endif
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(raw.GetConnectionState(), ConnectionState::kDisconnected);
  EXPECT_EQ(honest.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(honest.GetPhase(), Phase::kMatch);
  EXPECT_EQ(honest.GetAuthoritativeState()->bodies.size(), 1U);
  EXPECT_GT(BodySeenBy(honest, *honest.GetEntityId())->position.x, SpawnPoints()[0].x + 1.0F);
  EXPECT_EQ(CountOccurrences(log, "event=misbehaving_disconnected"), 1U) << log;
  EXPECT_NE(log.find("WARN subsystem=serverruntime event=misbehaving_disconnected peer="), std::string::npos) << log;
  EXPECT_NE(log.find("session=" + std::to_string(std::to_underlying(raw.GetSessionId().value())) +
                     " reason=\"undecodable message\""),
            std::string::npos)
      << log;
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_DEBUG
  EXPECT_NE(log.find(" misbehaving=1"), std::string::npos) << log;
#endif
}

// Requirements: US-15, NFR-05
TEST_F(LobbyMisbehaviourTest, AMisbehavingPlayerDisconnectedFromTheLobbyChangesTheRosterEveryClientIsTold) {
  Session& first = Join();
  Session& second = Join();
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_,
                               [](const auto& s) { return s->GetLobby() && s->GetLobby()->roster.size() == 3; });
  }));
  const auto version = first.GetLobby()->version;

  for (std::size_t i = 0; i < kFlood; ++i) {
    raw.SendPayload(kUndecodable);
  }

  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] {
    return std::ranges::all_of(sessions_, [](const auto& s) { return s->GetLobby()->roster.size() == 2; });
  }));
  for (const Session* client : {&first, &second}) {
    EXPECT_GT(client->GetLobby()->version, version);
    EXPECT_EQ(client->GetLobby()->roster[0].session, *first.GetSessionId());
    EXPECT_EQ(client->GetLobby()->roster[1].session, *second.GetSessionId());
  }
  raw.Serve();
  EXPECT_EQ(raw.GetConnectionState(), ConnectionState::kDisconnected);
}

// Requirements: US-15, NFR-05
TEST_F(MisbehaviourTest, StaleRepeatsAndCommandsInFlightAcrossAMatchEndNeverDisconnect) {
  Session& honest = Join();
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));
  ASSERT_TRUE(DriveIntoMatch(host_, All(), &raw));
  Run(kSettleTicks, Forward());

  const auto commands = [](augusta::command::Sequence sequence) {
    augusta::protocol::SequencedCommandWire sequenced{.sequence = sequence};
    sequenced.command.direction = Vec3(1.0F, 0.0F, 0.0F);
    return augusta::protocol::CommandsWire{.commands = {sequenced}};
  };
  // The same command over and over: every repeat after the first is stale.
  for (std::size_t i = 0; i < kFlood; ++i) {
    raw.Send(commands(1));
  }
  Run(kSettleTicks, Forward());
  const auto version = honest.GetLobby()->version;
  host_.EndMatch();
  // Commands still arriving after the match ended, and a Ready for the Roster before it.
  for (std::size_t i = 0; i < kFlood; ++i) {
    raw.Send(commands(static_cast<augusta::command::Sequence>(2 + i)));
    raw.Send(augusta::protocol::ReadyWire{.version = version});
  }
  Run(kSettleTicks, Forward());
  Settle(host_, All());
  raw.Serve();

  EXPECT_EQ(raw.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(honest.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(honest.GetPhase(), Phase::kLobby);
  EXPECT_EQ(honest.GetLobby()->roster.size(), 2U);
}

// An honest client under NFR-02's 100 ms of latency, and with packet loss, never
// reaches the misbehaviour threshold through a full match.
using HonestClientTest = MovementTest;

// Requirements: NFR-02
TEST_F(HonestClientTest, AtAHundredMillisecondsOfLatencyAndWithPacketLossAClientStaysConnectedThroughAFullMatch) {
  augusta::logging::Init();
  constexpr int kOneWayLatencyMs = 50;
  constexpr float kLossPercent = 20.0F;
  constexpr int kWalkSteps = 120;
  testing::internal::CaptureStdout();
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs, .loss_percent = kLossPercent});

  Run(kWalkSteps, Walking());
  host_.EndMatch();
  // Its commands in flight as the match ends arrive after it.
  Run(kSettleTicks, Walking());
  augusta::networking::SimulateNetworkConditions({});
  const bool back_in_lobby = ExchangeUntil(host_, {&session_}, [&] { return session_.GetPhase() == Phase::kLobby; });
  Settle(host_, {&session_});
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_TRUE(back_in_lobby);
  EXPECT_EQ(session_.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(session_.GetLobby()->roster.size(), 1U);
  EXPECT_EQ(CountOccurrences(log, "event=misbehaving_disconnected"), 0U) << log;
}

// A peer that connects and is not admitted to the Lobby within
// kAdmissionDeadline is disconnected, as one that misbehaves is (US-15). The
// server's clock is handed to it, so the deadline passes without waiting for it.
class AdmissionDeadlineTest : public RobustnessOf<2> {
 protected:
  using Clock = std::chrono::steady_clock;
  static constexpr auto kDeadline = augusta::server::kAdmissionDeadline;
  static constexpr auto kJustShort = std::chrono::milliseconds(1);

  // Runs host_ - its clock stopped at host_time - raw and every session for
  // long enough that a disconnect would have reached them.
  void ServeAt(Clock::time_point host_time, RawClient& raw) {
    const auto until = Clock::now() + std::chrono::milliseconds(300);
    while (Clock::now() < until) {
      host_.PumpNetwork(host_time);
      raw.Serve();
      for (Session* session : All()) {
        session->PumpEvents();
        session->ExchangeMessages();
      }
      std::this_thread::sleep_for(kPollInterval);
    }
  }

  // Runs host_, its clock stopped at host_time, and raw until raw is
  // disconnected or the deadline passes; returns whether it was.
  bool DisconnectedAt(Clock::time_point host_time, RawClient& raw) {
    return raw.ServeUntil(host_, [&] { return raw.GetConnectionState() == ConnectionState::kDisconnected; }, host_time);
  }
};

// Requirements: NFR-05
TEST_F(AdmissionDeadlineTest, APeerThatConnectsAndSendsNothingIsDisconnectedOnceTheDeadlinePassesAndNotBefore) {
  augusta::logging::Init();
  const Clock::time_point before = Clock::now();
  RawClient raw(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(raw.Connect(host_));
  const Clock::time_point connected = Clock::now();

  testing::internal::CaptureStdout();
  ServeAt(before + kDeadline - kJustShort, raw);
  const ConnectionState short_of_the_deadline = raw.GetConnectionState();
  const bool disconnected = DisconnectedAt(connected + kDeadline, raw);
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_DEBUG
  // The heartbeat after the disconnect counts it; a DEBUG line, so only where
  // DEBUG is compiled in.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  host_.RecordTiming(augusta::tick::Timing{});
#endif
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(short_of_the_deadline, ConnectionState::kConnected);
  EXPECT_TRUE(disconnected);
  EXPECT_EQ(CountOccurrences(log, "event=misbehaving_disconnected"), 1U) << log;
  EXPECT_NE(log.find("WARN subsystem=serverruntime event=misbehaving_disconnected peer="), std::string::npos) << log;
  EXPECT_NE(log.find(" reason=\"not admitted in time\""), std::string::npos) << log;
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_DEBUG
  EXPECT_NE(log.find(" misbehaving=1"), std::string::npos) << log;
#endif
}

// Requirements: NFR-05
TEST_F(AdmissionDeadlineTest, APeerThatSendsAnythingButAJoinIsDisconnectedAtTheDeadline) {
  augusta::logging::Init();
  const Clock::time_point before = Clock::now();
  RawClient raw(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(raw.Connect(host_));
  const Clock::time_point connected = Clock::now();

  testing::internal::CaptureStdout();
  raw.Send(protocol::ReadyWire{.version = 1});
  raw.Send(protocol::CommandsWire{.commands = {protocol::SequencedCommandWire{.sequence = 1, .command = {}}}});
  ServeAt(before + kDeadline - kJustShort, raw);
  const ConnectionState short_of_the_deadline = raw.GetConnectionState();
  const bool disconnected = DisconnectedAt(connected + kDeadline, raw);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(short_of_the_deadline, ConnectionState::kConnected);
  EXPECT_TRUE(disconnected);
  EXPECT_TRUE(raw.ReceivedOf<protocol::JoinAcceptedWire>().empty());
  EXPECT_EQ(CountOccurrences(log, "event=misbehaving_disconnected"), 1U) << log;
  EXPECT_NE(log.find(" reason=\"not admitted in time\""), std::string::npos) << log;
}

// Requirements: NFR-05
TEST_F(AdmissionDeadlineTest, AClientThatJoinsWithinTheDeadlineIsAdmittedAndStaysConnected) {
  Session& honest = Join();
  RawClient idle(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(idle.Connect(host_));

  const Clock::time_point past_the_deadline = Clock::now() + (2 * kDeadline);
  ServeAt(past_the_deadline, idle);

  // The idle peer beside it is not spared: the deadline did pass.
  EXPECT_TRUE(DisconnectedAt(past_the_deadline, idle));
  EXPECT_EQ(honest.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(honest.GetPhase(), Phase::kLobby);
  ASSERT_TRUE(honest.GetLobby().has_value());
  EXPECT_EQ(honest.GetLobby()->roster.size(), 1U);
}

// Requirements: NFR-05
TEST_F(AdmissionDeadlineTest, ARefusedClientIsToldWhyBeforeTheDeadlineDisconnectsIt) {
  RawClient raw(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(raw.Connect(host_));
  const Clock::time_point connected = Clock::now();

  raw.Send(protocol::JoinRequestWire{.engine_version = "0.0.0-another", .character = kCharacter});
  ASSERT_TRUE(raw.ServeUntil(host_, [&] { return !raw.ReceivedOf<protocol::JoinRefusedWire>().empty(); }));
  const ConnectionState once_refused = raw.GetConnectionState();

  EXPECT_EQ(once_refused, ConnectionState::kConnected);
  EXPECT_TRUE(DisconnectedAt(connected + kDeadline, raw));
  const auto refused = raw.ReceivedOf<protocol::JoinRefusedWire>();
  ASSERT_EQ(refused.size(), 1U);
  EXPECT_EQ(refused.front().reason, protocol::JoinRefusalWire::kVersionMismatch);
}

// Requirements: NFR-05
TEST_F(AdmissionDeadlineTest, AJoinRefusedAsTheDeadlinePassesIsStillToldBeforeTheConnectionEnds) {
  RawClient raw(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(raw.Connect(host_));
  const Clock::time_point connected = Clock::now();

  raw.Send(protocol::JoinRequestWire{.engine_version = "0.0.0-another", .character = kCharacter});
  // Long enough for the Join to have reached the server, which takes it in on
  // its next round: the one on which the deadline passes.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(DisconnectedAt(connected + kDeadline, raw));
  const auto refused = raw.ReceivedOf<protocol::JoinRefusedWire>();
  ASSERT_EQ(refused.size(), 1U);
  EXPECT_EQ(refused.front().reason, protocol::JoinRefusalWire::kVersionMismatch);
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
    setup.scenario.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
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

// Requirements: US-07
TEST_F(FireTest, HoldingFireFiresAtTheFireRateUntilTheMagazineIsEmpty) {
  Session& client = *sessions_.front();

  // A second of fire: ten rounds.
  Run(60, Firing());
  EXPECT_EQ(ShotsOf(client).size(), 10U);

  // Two more: the five rounds left, and then nothing.
  Run(120, Firing());
  EXPECT_EQ(ShotsOf(client).size(), kMagazine);
}

// Requirements: US-07
TEST_F(FireTest, AOneTickPressOfFireGivesExactlyOneShot) {
  Session& client = *sessions_.front();

  Step(Firing());
  Run(30);

  EXPECT_EQ(ShotsOf(client).size(), 1U);
}

// Requirements: US-07
TEST_F(FireTest, NoShotIsFiredWhileFireIsNotHeld) {
  Session& client = *sessions_.front();

  Run(30);

  EXPECT_TRUE(ShotsOf(client).empty());
}

// Requirements: US-08
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

// Requirements: US-08
TEST_F(FireTest, AReloadPressWithAFullMagazineStartsNothingAndFireContinues) {
  Session& client = *sessions_.front();

  // A second of fire, as without the press: ten rounds.
  Step(Reloading(/*fire=*/true));
  Run(59, Firing());

  EXPECT_EQ(ShotsOf(client).size(), 10U);
}

// A Command's reload is a press (the sampler sets it on one tick), but the
// server does not count on it: sent on every tick, it still starts one reload.
// Requirements: US-08
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

// Requirements: US-07, US-10
TEST_F(FireDuelTest, EveryClientIsToldOfAShotWithItsShooterTickOriginAndDirection) {
  Session& shooter = *sessions_[0];
  Session& bystander = *sessions_[1];
  Settle(host_, Pointers(sessions_));
  const augusta::tick::Tick last_tick = shooter.GetAuthoritativeState()->tick;
  const Vec3 feet = PositionSeenBy(shooter, *shooter.GetEntityId()).value();
  Command command = Firing();
  command.yaw = 0.75F;
  command.pitch = -0.25F;
  const auto shooter_session = static_cast<augusta::server::SessionId>(std::to_underlying(*shooter.GetSessionId()));

  // One tick, by hand: the server takes in the fire command on its next tick.
  shooter.Tick(command, kFixedTick);
  bystander.Tick(Command{}, kFixedTick);
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return host_.QueuedCommands(shooter_session) > 0; }));
  ASSERT_EQ(host_.Tick(kFixedTick).state.shots.size(), 1U);

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
// Requirements: US-07, NFR-02
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
    const augusta::simulation::State state = host_.Tick(kFixedTick).state;
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
  [[nodiscard]] augusta::command::Sequence Acknowledged() const {
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
  augusta::command::Sequence sequence_ = kSettleTicks;
  // The rifle the client predicted after each command, by its sequence, and
  // the sequences the server's answer has been compared at.
  std::map<augusta::command::Sequence, augusta::weapon::State> predicted_;
  std::set<augusta::command::Sequence> compared_;
};

// US-08: the ammo count and the reload, predicted exactly as the server applies them.
// Requirements: US-07, US-08
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
// Requirements: US-07, NFR-02
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
// Requirements: US-07, NFR-02
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
// Requirements: US-09, NFR-02
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
      fired_ += Step(command).state.shots.size();
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

// Requirements: US-09
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

// Requirements: US-09
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

// Requirements: US-09, US-06
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
// Requirements: US-09, US-15
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
    setup.scenario.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    setup.scenario.characters.front().hitboxes = HumanHitboxes();
    if (walled) {
      setup.scenario.collision.push_back(
          CollisionMesh{.points = {Vec3(-20.0F, kFloorY, kWallZ), Vec3(-20.0F, 5.0F, kWallZ), Vec3(20.0F, 5.0F, kWallZ),
                                   Vec3(20.0F, kFloorY, kWallZ)},
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
      const augusta::simulation::TickResult result = StepEach(commands_);
      const augusta::simulation::State& state = result.state;
      hits_.insert(hits_.end(), state.hits.begin(), state.hits.end());
      map_impacts_.insert(map_impacts_.end(), state.map_impacts.begin(), state.map_impacts.end());
      shots_fired_.insert(shots_fired_.end(), state.shots.begin(), state.shots.end());
      deaths_.insert(deaths_.end(), state.deaths.begin(), state.deaths.end());
      for (const augusta::simulation::PolicyAction& action : result.actions) {
        match_ends_.emplace_back(state.tick, std::get<augusta::simulation::MatchEnd>(action));
      }
    }
  }

  // Turns command's view to look along aim: from the eye to the point aimed at.
  static void AimAt(Command& command, const Vec3& aim) {
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / Length(aim));
  }

  // The shooter taps fire once, aimed from its eye at the point offset from
  // target's feet as the newest update it has shows them, which is the Seen
  // time its Command reports, and the match runs until its rifle is ready again.
  void ShootAt(const Session& target, const Vec3& offset) {
    ShootThrough(PositionSeenBy(Standing(0), *target.GetEntityId()).value() + offset);
  }

  // As ShootAt, aimed at point, whatever is there.
  void ShootThrough(const Vec3& point) {
    Session& shooter = Standing(0);
    const Vec3 eye = PositionSeenBy(shooter, *shooter.GetEntityId()).value() + Vec3(0.0F, kEyeHeight, 0.0F);
    Command& command = CommandOf(shooter);
    AimAt(command, point - eye);
    command.seen_tick = shooter.GetAuthoritativeState().value().tick;
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
  std::vector<std::pair<augusta::tick::Tick, augusta::simulation::MatchEnd>> match_ends_;
  std::map<const Session*, std::vector<HitConfirmation>> confirmations_;
  std::map<const Session*, std::vector<Death>> deaths_received_;
};

using HitLineTest = HitMatchOf<3>;

// Requirements: US-11
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
// Requirements: US-11
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
// Requirements: US-11
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
// Requirements: US-11
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

// Requirements: US-10, US-11
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
// Requirements: US-11
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
      const augusta::simulation::State state = host_.Tick(kFixedTick).state;
      hits_.insert(hits_.end(), state.hits.begin(), state.hits.end());
    }
    std::this_thread::sleep_for(kNetworkDelay);
    Exchange();
    if (const auto feet = PositionSeenBy(shooter, *target.GetEntityId()); feet.has_value()) {
      seen_[shooter.GetAuthoritativeState()->tick] = *feet;
    }
  }

  // Where each update the shooter was sent put the target's feet, by its tick.
  std::map<augusta::tick::Tick, Vec3> seen_;
};

// US-11, ADR-0044: a shot that hits on the shooter's screen hits on the server.
// Requirements: US-11, NFR-02
TEST_F(LagCompensatedHitTest, AClientFiringAtAStrafingTargetUnderItsCrosshairAtItsSeenTimeGetsAHitConfirmation) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  CommandOf(target).movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  augusta::networking::SimulateNetworkConditions({.latency_ms = kOneWayLatencyMs});
  for (int i = 0; i < 60; ++i) {
    PlayAhead(shooter, target);
  }

  // The Seen time: the newest update kept that is the Interpolation delay or more
  // behind the newest of all, and halfway to the next one if that is kept too.
  ASSERT_FALSE(seen_.empty());
  const augusta::tick::Tick newest = seen_.rbegin()->first;
  ASSERT_GT(newest, kInterpolationTicks);
  auto shown = seen_.upper_bound(newest - kInterpolationTicks);
  ASSERT_NE(shown, seen_.begin());
  --shown;
  const auto next = seen_.find(shown->first + 1);
  const float fraction = next == seen_.end() ? 0.0F : 0.5F;
  const Vec3 feet = next == seen_.end() ? shown->second : augusta::math::Lerp(shown->second, next->second, fraction);
  // The target has since walked clear of where the Seen time shows its torso, 0.4 m wide.
  ASSERT_GT(seen_.rbegin()->second.x - feet.x, 0.25F);

  const Vec3 eye = PositionSeenBy(shooter, *shooter.GetEntityId()).value() + Vec3(0.0F, kEyeHeight, 0.0F);
  Command& command = CommandOf(shooter);
  AimAt(command, feet + Vec3(0.0F, kTorsoHeight, 0.0F) - eye);
  command.seen_tick = shown->first;
  command.seen_fraction = fraction;
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
    setup.scenario.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    setup.scenario.characters.front().hitboxes = HumanHitboxes();
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
// each reports the Seen time of the update it was last sent, so the hits are lag compensated. The
// server keeps up with every client's commands, tells every client of every
// Shot, and every shooter of every one of its hits.
// Requirements: US-07, US-08, US-11, NFR-01, NFR-06
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
    augusta::command::Sequence acknowledged = 0;
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

    const augusta::simulation::State state = host_.Tick(kFixedTick).state;
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
        command.seen_tick = shown->tick;
      }

      const auto predicted = sessions_[i]->Tick(command, kFixedTick);
      client.rifle = predicted.rifle;
      if (tick == 0) {
        client.first_x = predicted.local_body.position.x;
      }
      client.farthest = std::max(client.farthest, std::abs(predicted.local_body.position.x - client.first_x));
    }

    host_.PumpNetwork(std::chrono::steady_clock::now());
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
    EXPECT_GE(client.acknowledged + kAckTolerance, static_cast<augusta::command::Sequence>(kMatchTicks))
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
      .tick = 7, .origin = Vec3(1.0F, 1.5F, -2.0F), .shooter = shooter, .yaw = 0.5F, .pitch = -0.125F};
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
  for (augusta::tick::Tick tick = 1; tick <= 3; ++tick) {
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
  const auto sent = static_cast<augusta::tick::Tick>(augusta::harness::kMaxPendingShots + 10);
  for (augusta::tick::Tick tick = 1; tick <= sent; ++tick) {
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

// Requirements: US-13
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

// Requirements: US-13
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
// Requirements: US-13, NFR-02
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
// Requirements: US-13, US-15
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
// Requirements: US-13
TEST_F(DeathTest, ABulletAimedThroughWhereTheDeadPlayerStoodHitsWhatIsBehindIt) {
  Session& victim = Standing(1);
  Session& behind = Standing(2);
  const Vec3 stood = PositionSeenBy(Standing(0), *victim.GetEntityId()).value();
  Kill(victim);

  ShootThrough(stood + Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits_.size(), 3U);
  EXPECT_EQ(std::to_underlying(hits_.back().target), std::to_underlying(*behind.GetEntityId()));
}

// Requirements: US-13
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
// Requirements: US-03, US-14
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

// The tick that ends a Match sends its own Shots, Hit confirmations and Deaths
// just before Match end, so they often arrive together: what the Match ended
// on is still handed out once it has.
TEST_F(ScriptedServerTest, TheShotsHitsAndDeathsThatArriveJustBeforeMatchEndAreStillHandedOutAfterIt) {
  server_.Send(ShotBy(kScriptedEntity));
  server_.Send(HitOn(kScriptedEntity));
  server_.Send(DeathOf(kScriptedEntity, kScriptedEntity));
  server_.Send(augusta::protocol::MatchEndWire{});
  Settle();
  ASSERT_EQ(session_.GetPhase(), Phase::kLobby);

  EXPECT_EQ(session_.TakeShots().size(), 1U);
  EXPECT_EQ(session_.TakeHitConfirmations().size(), 1U);
  EXPECT_EQ(session_.TakeDeaths().size(), 1U);
}

TEST_F(ScriptedServerTest, ADeathNobodyAskedForIsNotHandedOutInTheNextMatch) {
  Settle();

  server_.Send(DeathOf(kScriptedEntity, kScriptedEntity));
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ScriptedServer::StartOfAlone());
  Settle();
  ASSERT_EQ(session_.GetPhase(), Phase::kMatch);

  EXPECT_TRUE(session_.TakeDeaths().empty());
}

// Told of its own death, a client predicts no more, even before an update says so.
// Requirements: US-13
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

// The example scenario's Game policy, read out of its golden server pack by the
// server's own loader: its rules hand out the Spawn points and end the Match on
// the last player standing (US-14).
augusta::scripting::Engine ExamplePolicy() {
  const std::filesystem::path packs{AUGUSTA_EXAMPLE_PACKS};
  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(packs / "test.pub");
  EXPECT_TRUE(public_key.has_value());
  auto pack = augusta::assets::Pack::Load(packs / "server.pack", public_key.value());
  EXPECT_TRUE(pack.has_value()) << augusta::assets::DescribeLoadError(pack.error());
  auto policy = augusta::server::LoadPolicy(pack.value());
  EXPECT_TRUE(policy.has_value()) << augusta::server::DescribePolicyLoadError(policy.error());
  return policy ? *std::move(policy) : augusta::scripting::Engine{};
}

// A match of kPlayers in the line, under the example scenario's Game policy:
// the last player standing wins.
template <std::uint8_t kPlayers>
class LastStandingMatchOf : public HitMatchOf<kPlayers> {
 protected:
  LastStandingMatchOf() : HitMatchOf<kPlayers>(false, ExamplePolicy()) {}

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
    command.seen_tick = shooter.GetAuthoritativeState().value().tick;
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

// Requirements: US-14
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
// Requirements: US-14, NFR-02
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
// Requirements: US-14
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
// Requirements: US-14
TEST_F(LastStandingDuelTest, TheNextMatchStartsOnItsOwnOnceEveryoneIsReadyAgainNoSoonerThanThePauseAfterTheLast) {
  constexpr auto kDeadline = std::chrono::seconds(15);
  Kill(Standing(1));
  ASSERT_EQ(match_ends_.size(), 1U);
  const augusta::tick::Tick ended = match_ends_[0].first;

  // As a client does, each reports Ready for every Roster it is sent; nothing else happens.
  std::map<const Session*, std::uint32_t> reported;
  augusta::tick::Tick started = 0;
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
    const augusta::simulation::State state = host_.Tick(kFixedTick).state;
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

// Requirements: US-14, NFR-07
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

// Those who leave are out of the match (US-14): the one left standing wins.
using LastStandingTrioTest = LastStandingMatchOf<3>;

// Requirements: US-14
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

// A full Match of eight under the example scenario's Game policy, everyone
// standing in a line along X, 4 m apart, at the Spawn point the rules hand
// it: its rank down the line, 0 at the origin. Everyone looks along -X,
// at the player in front of it, and plays the character of HumanHitboxes with a
// rifle of 600 rounds a minute whose magazine of 15 takes half a second to
// reload, and whose every round kicks the aim up by 1/256 rad. Five torso hits kill.
class EightPlayerMatchTest : public LoopbackMatch {
 protected:
  static constexpr std::size_t kPlayers = augusta::protocol::kMaxPlayers;
  static constexpr float kEyeHeight = 1.6F;
  static constexpr float kTorsoHeight = 1.2F;
  static constexpr float kSpacing = 4.0F;

  static HostSetup InALine() {
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
    parameters.starting_health = 100.0F;
    std::vector<Vec3> spawn_points;
    for (std::size_t rank = 0; rank < kPlayers; ++rank) {
      spawn_points.emplace_back(kSpacing * static_cast<float>(rank), kFloorY, 0.0F);
    }
    HostSetup setup = OnTheFloor(std::move(spawn_points), parameters);
    setup.policy = ExamplePolicy();
    setup.scenario.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    setup.scenario.characters.front().hitboxes = HumanHitboxes();
    return setup;
  }

  EightPlayerMatchTest() : LoopbackMatch(InALine()) {}

  void SetUp() override {
    for (std::size_t i = 0; i < kPlayers; ++i) {
      Join();
    }
    ASSERT_TRUE(StartMatch());
  }

  // Every client by its rank down the line, each spawned at a Spawn point of its
  // own; empty if two share one or one is off the line.
  std::vector<Session*> ByRank() {
    std::vector<Session*> by_rank(kPlayers, nullptr);
    for (const auto& session : sessions_) {
      const Vec3 spawn = OwnSpawn(*session);
      const auto rank = static_cast<std::size_t>(std::lround(spawn.x / kSpacing));
      if (rank >= kPlayers || by_rank[rank] != nullptr ||
          spawn != Vec3(kSpacing * static_cast<float>(rank), kFloorY, 0.0F)) {
        return {};
      }
      by_rank[rank] = session.get();
    }
    return by_rank;
  }
};

// NFR-01 and NFR-06 for a whole Match (issue #252), paced to the real 60 Hz for
// the reason the M3 soak test gives: eight players strafe and sprint, all the
// same way so the line keeps its shape, and hold fire from Match start,
// reloading whenever their magazine is empty, with the policy hooks running.
// Each fires over everyone's head until its turn comes, then at the player in
// front, so the players die one by one from the back of the line, each killed
// by the next, and the front one is the last standing. The server keeps up with
// every client's commands, the dead's included, to the end, which the example
// rules decide with that player as winner, and every client is told
// of every Death and of the winner.
// Requirements: US-14, NFR-01, NFR-06
TEST_F(EightPlayerMatchTest, EightPlayersFightAMatchToItsEndWithAWinnerAndNoMissedTicks) {
  constexpr int kTickLimit = 900;  // The Match ends some 540 ticks in.
  constexpr int kFirstTurnTick = 60;
  constexpr int kTurnTicks = 75;
  constexpr std::uint32_t kAckTolerance = 20;  // A few round trips' worth still in flight.
  constexpr int kSprintBlockTicks = 100;
  // A block strafing one way, a block still, a block the other way, a block still.
  constexpr int kStrafeBlockTicks = 30;
  constexpr std::array<float, 4> kStrafeCycle = {1.0F, 0.0F, -1.0F, 0.0F};
  // Down the line, from the eye of one player at the torso of the next.
  constexpr float kAlongTheLine = std::numbers::pi_v<float> / 2.0F;
  const float at_the_one_in_front = std::atan2(kTorsoHeight - kEyeHeight, kSpacing);
  constexpr float kOverTheirHeads = 0.3F;

  const std::vector<Session*> by_rank = ByRank();
  ASSERT_EQ(by_rank.size(), kPlayers) << "the players did not spawn at a Spawn point each";
  std::vector<std::uint32_t> entities;
  for (const Session* client : by_rank) {
    entities.push_back(std::to_underlying(*client->GetEntityId()));
  }

  struct Client {
    // Its rifle as it last predicted it: what it decides to reload by.
    augusta::weapon::State rifle{};
    std::uint32_t sent = 0;
    augusta::command::Sequence acknowledged = 0;
    std::vector<std::uint32_t> victims{};
  };
  std::vector<Client> clients(kPlayers);
  const auto collect = [&] {
    for (std::size_t rank = 0; rank < kPlayers; ++rank) {
      for (const Death& death : by_rank[rank]->TakeDeaths()) {
        clients[rank].victims.push_back(std::to_underlying(death.victim));
      }
    }
  };
  std::vector<augusta::simulation::Death> deaths;
  std::optional<augusta::simulation::MatchEnd> match_end;
  auto slowest_tick = std::chrono::steady_clock::duration::zero();

  const auto tick_duration = std::chrono::duration<float>(kFixedTick);
  for (int tick = 0; tick < kTickLimit && !match_end.has_value(); ++tick) {
    const auto tick_start = std::chrono::steady_clock::now();

    const augusta::simulation::TickResult result = host_.Tick(kFixedTick);
    slowest_tick = std::max(slowest_tick, std::chrono::steady_clock::now() - tick_start);
    deaths.insert(deaths.end(), result.state.deaths.begin(), result.state.deaths.end());
    for (const augusta::simulation::PolicyAction& action : result.actions) {
      match_end = std::get<augusta::simulation::MatchEnd>(action);
    }
    for (std::size_t rank = 0; rank < kPlayers; ++rank) {
      Session& session = *by_rank[rank];
      Client& client = clients[rank];
      const bool its_turn = rank > 0 && tick >= kFirstTurnTick + ((static_cast<int>(rank) - 1) * kTurnTicks);
      Command command{};
      command.movement.direction = Vec3(kStrafeCycle.at((tick / kStrafeBlockTicks) % kStrafeCycle.size()), 0.0F, 0.0F);
      command.movement.sprint = (tick / kSprintBlockTicks) % 2 == 0;
      command.yaw = kAlongTheLine;
      command.pitch = its_turn ? at_the_one_in_front : kOverTheirHeads;
      command.fire = true;
      command.reload = tick > 0 && client.rifle.rounds == 0 && client.rifle.reload_remaining <= 0.0F;
      if (const auto shown = session.GetAuthoritativeState()) {
        command.seen_tick = shown->tick;
      }

      if (session.GetPhase() == Phase::kMatch) {
        ++client.sent;
      }
      client.rifle = session.Tick(command, kFixedTick).rifle;
    }

    host_.PumpNetwork(std::chrono::steady_clock::now());
    for (std::size_t rank = 0; rank < kPlayers; ++rank) {
      Session& session = *by_rank[rank];
      session.PumpEvents();
      session.ExchangeMessages();

      ASSERT_FALSE(session.GetFailure().has_value()) << "rank " << rank << " failed at tick " << tick;
      EXPECT_EQ(session.GetConnectionState(), ConnectionState::kConnected)
          << "rank " << rank << " dropped at tick " << tick;

      if (const auto authoritative = session.GetAuthoritativeState()) {
        clients[rank].acknowledged = std::max(clients[rank].acknowledged, authoritative->acknowledged_sequence);
      }
    }
    collect();

    std::this_thread::sleep_until(tick_start +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
  }
  RecordProperty("slowest_tick_us",
                 static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(slowest_tick).count()));

  ASSERT_TRUE(match_end.has_value()) << "the Match had not ended after " << kTickLimit << " ticks";
  ASSERT_TRUE(match_end->winner.has_value());
  EXPECT_EQ(std::to_underlying(*match_end->winner), std::to_underlying(*by_rank.back()->GetSessionId()));
  ASSERT_EQ(deaths.size(), kPlayers - 1);
  std::vector<std::uint32_t> victims;
  for (std::size_t rank = 0; rank + 1 < kPlayers; ++rank) {
    EXPECT_EQ(std::to_underlying(deaths[rank].victim), entities[rank]) << "death " << rank;
    EXPECT_EQ(std::to_underlying(deaths[rank].killer), entities[rank + 1]) << "death " << rank;
    victims.push_back(entities[rank]);
  }

  // Death and Match end are reliable: the last of them are on their way.
  ASSERT_TRUE(ExchangeUntil(host_, by_rank, [&] {
    collect();
    return std::ranges::all_of(by_rank, [](const Session* session) {
      return session->GetPhase() == Phase::kLobby && session->GetMatchEnd().has_value();
    });
  }));
  for (std::size_t rank = 0; rank < kPlayers; ++rank) {
    const Client& client = clients[rank];
    EXPECT_GE(client.acknowledged + kAckTolerance, client.sent)
        << "rank " << rank << " fell behind: server acknowledged only " << client.acknowledged << " of " << client.sent
        << " commands";
    EXPECT_EQ(client.victims, victims) << "rank " << rank << " was not told of every Death";
    EXPECT_EQ(by_rank[rank]->GetMatchEnd()->winner, by_rank.back()->GetSessionId()) << "rank " << rank;
  }
}

// A Match of one, for development (ADR-0043). In v1 only rounds kill and none
// hits its own shooter, so a lone player cannot die over the network: that its
// death is a draw is checked through SimulationWorld
// (example_rules_test.cpp); here, that a lone player plays on, and that a
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
  SoloLastStandingTest() : SoloMatchTest(ExamplePolicy()) {}
};

// Requirements: US-14
TEST_F(SoloLastStandingTest, ALonePlayerPlaysOnUnderTheExampleObjectives) {
  Session& client = Join();
  ASSERT_TRUE(StartMatch());

  Run(2 * kTestTickRate);

  EXPECT_EQ(client.GetPhase(), Phase::kMatch);
  EXPECT_FALSE(client.GetMatchEnd().has_value());
}

class SoloDrawTest : public SoloMatchTest {
 protected:
  SoloDrawTest() : SoloMatchTest(Rules("function on_tick() return {draw = true} end")) {}

  static augusta::scripting::Engine Rules(const char* rules) {
    auto policy = augusta::scripting::Engine::Load(rules);
    EXPECT_TRUE(policy.has_value());
    return policy ? *std::move(policy) : augusta::scripting::Engine{};
  }
};

// Requirements: US-14
TEST_F(SoloDrawTest, ASoloMatchEndedAsADrawIsToldToItsPlayerAsADrawAndItIsBackInTheLobby) {
  Session& client = Join();
  ASSERT_TRUE(StartMatch() || client.GetMatchEnd().has_value());

  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return client.GetMatchEnd().has_value(); }));

  EXPECT_EQ(client.GetPhase(), Phase::kLobby);
  EXPECT_FALSE(client.GetMatchEnd()->winner.has_value());
}

// The catalogue's corrected entries (impossible_actions.md): an adversary, a
// client speaking the protocol by hand, plays a Match beside honest Sessions.
// Each tick the server runs once every command sent for it has arrived, and
// everyone is told of it before the next. A test compares what the adversary's
// impossible action gets with what an honest client's honest equivalent gets,
// as clients are told it.
class AdversaryMatch : public LoopbackMatch {
 protected:
  using Tick = augusta::tick::Tick;
  static constexpr std::uint8_t kFire = protocol::CommandWire::kFire;
  static constexpr std::uint8_t kReload = protocol::CommandWire::kReload;
  static constexpr std::uint8_t kSprint = protocol::CommandWire::kSprint;

  explicit AdversaryMatch(HostSetup setup) : LoopbackMatch(std::move(setup)) {}

  // Admits an honest Session, then the adversary, starts the Match and lets it settle.
  void StartWithAnHonestPlayer() {
    Join();
    ASSERT_TRUE(adversary_.Join(host_));
    ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_), &adversary_));
    const auto entity = adversary_.Entity();
    ASSERT_TRUE(entity.has_value());
    adversary_entity_ = *entity;
    honest_commands_.assign(sessions_.size(), Command{});
    for (int i = 0; i < kSettleTicks; ++i) {
      Play();
    }
    ASSERT_TRUE(adversary_.NewestState().has_value());
  }

  [[nodiscard]] EntityId Adversary() const { return static_cast<EntityId>(std::to_underlying(adversary_entity_)); }

  // The newest update the adversary has been told of.
  [[nodiscard]] protocol::AuthoritativeStateWire AdversaryTold() const { return adversary_.NewestState().value(); }

  // The tick the server runs next: the adversary has been told of every one before it.
  [[nodiscard]] Tick NextTick() const { return AdversaryTold().tick + 1; }

  // A command with flags set, walking along direction.
  static protocol::CommandWire Intent(std::uint8_t flags, const Vec3& direction = {}) {
    return protocol::CommandWire{.direction = direction, .flags = flags};
  }

  // The adversary's message for one tick: commands, numbered on from the last
  // it sent, sampled against seen_tick, or else the newest update it has been told of.
  protocol::CommandsWire Numbered(const std::vector<protocol::CommandWire>& commands,
                                  std::optional<Tick> seen_tick = std::nullopt) {
    protocol::CommandsWire message{.commands = {}, .seen_tick = seen_tick.value_or(AdversaryTold().tick)};
    for (const protocol::CommandWire& command : commands) {
      message.commands.push_back({.sequence = ++sent_, .command = command});
    }
    return message;
  }

  // One tick: the adversary sends message, if it holds a command, and every
  // honest Session its command of honest_commands_; the server ticks once every
  // command sent has arrived, or kStepPatience has passed, and everyone is told
  // of the tick. Returns the server's.
  augusta::simulation::TickResult Play(const protocol::CommandsWire& message = {}) {
    std::vector<augusta::server::SessionId> sending;
    if (!message.commands.empty()) {
      adversary_.Send(message);
      sending.push_back(static_cast<augusta::server::SessionId>(std::to_underlying(*adversary_.GetSessionId())));
    }
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      Session& session = *sessions_[i];
      if (session.GetPhase() == Phase::kMatch && session.IsAlive()) {
        sending.push_back(static_cast<augusta::server::SessionId>(std::to_underlying(*session.GetSessionId())));
      }
      states_[&session] = session.Tick(honest_commands_.at(i), kFixedTick);
    }
    const auto give_up = std::chrono::steady_clock::now() + kStepPatience;
    ExchangeUntil(host_, Pointers(sessions_), [&] {
      adversary_.Serve();
      return std::chrono::steady_clock::now() >= give_up ||
             std::ranges::all_of(sending,
                                 [&](augusta::server::SessionId session) { return host_.QueuedCommands(session) > 0; });
    });
    augusta::simulation::TickResult result = host_.Tick(kFixedTick);
    Deliver(result.state.tick);
    const std::vector<Shot> taken = sessions_.front()->TakeShots();
    told_shots_.insert(told_shots_.end(), taken.begin(), taken.end());
    return result;
  }

  // Plays ticks ticks, the adversary sending commands on each.
  void PlayFor(int ticks, const std::vector<protocol::CommandWire>& commands) {
    for (int i = 0; i < ticks; ++i) {
      Play(Numbered(commands));
    }
  }

  // The ticks of the Shots the first honest Session has been told shooter
  // fired, from tick from on.
  [[nodiscard]] std::vector<Tick> ShotTicks(EntityId shooter, Tick from = 0) const {
    std::vector<Tick> ticks;
    for (const Shot& shot : told_shots_) {
      if (shot.shooter == shooter && shot.tick >= from) {
        ticks.push_back(shot.tick);
      }
    }
    return ticks;
  }

  RawClient adversary_{host_.ListenEndpoint()};
  EntityIdWire adversary_entity_{};
  // What each honest Session sends on every tick, in the order of sessions_.
  std::vector<Command> honest_commands_;
  // The sequence of the last Command the adversary sent.
  augusta::command::Sequence sent_ = 0;

 private:
  // Runs the network until the adversary and every honest Session have been
  // told of tick, or kStepPatience has passed.
  void Deliver(Tick tick) {
    const auto give_up = std::chrono::steady_clock::now() + kStepPatience;
    const auto told = [&] {
      const auto newest = adversary_.NewestState();
      return newest.has_value() && newest->tick >= tick && std::ranges::all_of(sessions_, [&](const auto& session) {
               const auto state = session->GetAuthoritativeState();
               return state.has_value() && state->tick >= tick;
             });
    };
    ExchangeUntil(host_, Pointers(sessions_), [&] {
      adversary_.Serve();
      return told() || std::chrono::steady_clock::now() >= give_up;
    });
  }

  // Every Shot the first honest Session has been told of.
  std::vector<Shot> told_shots_;
};

// The adversary and one honest player, apart on the floor, each with
// FireMatchOf's rifle (a round every six ticks, a magazine of 15 that takes 30
// ticks to reload) and on StaminaTest's stamina rules (a bar that empties in a
// second of sprinting and refills in four of rest, and a walk forced at or
// below a fifth of it). The honest player sees the adversary's body and Shots.
class CorrectedActionTest : public AdversaryMatch {
 protected:
  static constexpr std::uint8_t kMagazine = 15;
  static constexpr int kTicksPerRound = 6;
  static constexpr int kReloadTicks = 30;
  static constexpr float kWalkSpeed = 3.0F;
  // How far apart two bodies that moved alike may end up.
  static constexpr float kAlike = 0.01F;

  static HostSetup ArmedAndTiring() {
    Parameters parameters = WithPlayerCount(2);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = kMagazine;
    parameters.rifle.reload_seconds = 0.5F;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.ammo.max_range = 1000.0F;
    parameters.stamina = {.deplete_per_second = 1.0F, .regen_per_second = 0.25F, .forced_walk_below = 0.2F};
    return OnTheFloor({Vec3(10.0F, kFloorY, 0.0F), Vec3(20.0F, kFloorY, 5.0F)}, parameters);
  }

  CorrectedActionTest() : AdversaryMatch(ArmedAndTiring()) {}

  void SetUp() override { StartWithAnHonestPlayer(); }

  Session& Honest() { return *sessions_.front(); }
  Command& HonestCommand() { return honest_commands_.front(); }
  EntityId HonestEntity() { return *Honest().GetEntityId(); }

  // Where the honest player is told entity is.
  Vec3 Seen(EntityId entity) { return PositionSeenBy(Honest(), entity).value(); }

  // Expects the adversary to have moved from adversary_from as far as the
  // honest player from honest_from, as the honest player is told.
  void ExpectMovedAlike(const Vec3& adversary_from, const Vec3& honest_from) {
    const Vec3 adversary_moved = Seen(Adversary()) - adversary_from;
    const Vec3 honest_moved = Seen(HonestEntity()) - honest_from;
    EXPECT_NEAR(adversary_moved.x, honest_moved.x, kAlike);
    EXPECT_NEAR(adversary_moved.z, honest_moved.z, kAlike);
  }

  static float HorizontalSpeed(const augusta::physics::BodyState& body) {
    return std::hypot(body.velocity.x, body.velocity.z);
  }

  static Command Moving(bool sprint) {
    Command command{};
    command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    command.movement.sprint = sprint;
    return command;
  }

  static Command Firing() {
    Command command{};
    command.fire = true;
    return command;
  }

  // Both players hold fire until their magazines are empty and the last round's interval is over.
  void EmptyBothMagazines() {
    HonestCommand() = Firing();
    PlayFor((kMagazine + 1) * kTicksPerRound, {Intent(kFire)});
    ASSERT_EQ(ShotTicks(Adversary()).size(), kMagazine);
    ASSERT_EQ(ShotTicks(HonestEntity()).size(), kMagazine);
  }

  // From empty magazines, the honest player presses reload with fire held and
  // then holds fire, while the adversary sends the same press and then flags on
  // every tick. Expects the adversary's first round on the tick of the honest
  // player's, once the reload's 30 ticks are over, and no more rounds than it.
  void ExpectFirstRoundAfterAReloadWhileSending(std::uint8_t flags) {
    EmptyBothMagazines();
    const Tick pressed = NextTick();
    Command press = Firing();
    press.reload = true;
    HonestCommand() = press;
    Play(Numbered({Intent(kFire | kReload)}));
    HonestCommand() = Firing();
    PlayFor(kReloadTicks + kTicksPerRound, {Intent(flags)});

    const std::vector<Tick> honest = ShotTicks(HonestEntity(), pressed);
    ASSERT_FALSE(honest.empty());
    EXPECT_EQ(honest.front(), pressed + kReloadTicks);
    const std::vector<Tick> adversary = ShotTicks(Adversary(), pressed);
    ASSERT_FALSE(adversary.empty());
    EXPECT_EQ(adversary.front(), honest.front());
    EXPECT_LE(adversary.size(), honest.size());
  }
};

// Teleport, structural: a Command has no field for a position, so all a client
// can do is ask to move. This one also claims, in a message only the server
// sends, to be far away, and asks for the longest movement the boundary takes.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, AClientThatClaimsAPositionEndsWhereItsSprintTakesItAsAnHonestOneDoes) {
  const Vec3 claimed(500.0F, kFloorY, 500.0F);
  const Vec3 adversary_from = Seen(Adversary());
  const Vec3 honest_from = Seen(HonestEntity());
  HonestCommand() = Moving(/*sprint=*/true);

  for (int i = 0; i < 30; ++i) {
    if (i % 10 == 0) {
      adversary_.Send(protocol::AuthoritativeStateWire{
          .tick = NextTick(), .bodies = {{.entity = adversary_entity_, .body = {.position = claimed}}}});
    }
    Play(Numbered({Intent(kSprint, Vec3(1.99F, 0.0F, 0.0F))}));
  }

  EXPECT_GT(Seen(HonestEntity()).x - honest_from.x, 1.0F);
  ExpectMovedAlike(adversary_from, honest_from);
  EXPECT_GT(Length(Seen(Adversary()) - claimed), 400.0F);
}

// Speed hack: the server takes one Command a tick, and past the queue's cap
// drops the oldest.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, AClientSendingMoreCommandsThanTicksMovesNoFasterThanAnHonestOne) {
  const Vec3 adversary_from = Seen(Adversary());
  const Vec3 honest_from = Seen(HonestEntity());
  HonestCommand() = Moving(/*sprint=*/false);

  for (int i = 0; i < 30; ++i) {
    Play(Numbered(std::vector<protocol::CommandWire>(8, Intent(0, Vec3(1.0F, 0.0F, 0.0F)))));
    const auto body = BodySeenBy(Honest(), Adversary());
    ASSERT_TRUE(body.has_value());
    EXPECT_LE(HorizontalSpeed(*body), kWalkSpeed + 0.1F) << "tick " << i;
  }

  EXPECT_GT(Seen(HonestEntity()).x - honest_from.x, 1.0F);
  ExpectMovedAlike(adversary_from, honest_from);
}

// Fire rate: held fire fires at the rifle's rate whatever the client does with
// the trigger. This one sends two Commands a tick, letting go and pressing again.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, PressingFireAnewTwiceATickFiresNoFasterThanTheRiflesRate) {
  const Tick from = NextTick();
  HonestCommand() = Firing();

  PlayFor(60, {Intent(0), Intent(kFire)});

  EXPECT_EQ(ShotTicks(HonestEntity(), from).size(), 10U);
  const std::vector<Tick> fired = ShotTicks(Adversary(), from);
  ASSERT_FALSE(fired.empty());
  EXPECT_LE(fired.size(), 10U);
  for (std::size_t i = 1; i < fired.size(); ++i) {
    EXPECT_GE(fired[i] - fired[i - 1], static_cast<Tick>(kTicksPerRound)) << "round " << i;
  }
}

// Infinite ammo: the magazine is the server's.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, HoldingFireWithAnEmptyMagazineFiresNothingMore) {
  EmptyBothMagazines();

  PlayFor(60, {Intent(kFire)});

  EXPECT_EQ(ShotTicks(Adversary()).size(), kMagazine);
  EXPECT_EQ(ShotTicks(HonestEntity()).size(), kMagazine);
  EXPECT_EQ(AdversaryTold().rifle.rounds, 0U);
}

// Reload skip: no round fires on a tick of a reload.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, FireDuringAReloadFiresNothingUntilItCompletes) {
  ExpectFirstRoundAfterAReloadWhileSending(kFire);
}

// Reload spam: a reload is never started over, and never done sooner. A press
// once a round has left the magazine starts another, so it only costs rounds.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, ReloadOnEveryTickRefillsNoSoonerThanTheReloadTime) {
  ExpectFirstRoundAfterAReloadWhileSending(kFire | kReload);
}

// Stamina: the server keeps the stamina, so sprint is only ever asked for.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, SprintingWithNoStaminaIsHeldToAWalkUntilItRecoversAboveTheThreshold) {
  const Vec3 adversary_from = Seen(Adversary());
  const Vec3 honest_from = Seen(HonestEntity());
  HonestCommand() = Moving(/*sprint=*/true);
  bool was_exhausted = false;
  bool recovered = false;
  int walked = 0;
  int sprinted_after_recovering = 0;

  // A second of sprinting runs the bar out, and 0.2 of it at 0.25 a second
  // takes 48 ticks to come back. Every tick that starts exhausted is walked;
  // once recovered, the sprint is honored again until the bar runs out anew.
  for (int i = 0; i < 150; ++i) {
    Play(Numbered({Intent(kSprint, Vec3(1.0F, 0.0F, 0.0F))}));
    const auto body = BodySeenBy(Honest(), Adversary());
    ASSERT_TRUE(body.has_value());
    if (was_exhausted) {
      ++walked;
      EXPECT_NEAR(HorizontalSpeed(*body), kWalkSpeed, 0.2F) << "tick " << i;
    } else if (recovered && HorizontalSpeed(*body) > kWalkSpeed + 1.0F) {
      ++sprinted_after_recovering;
    }
    recovered = recovered || (was_exhausted && !body->exhausted);
    was_exhausted = body->exhausted;
  }

  EXPECT_GT(walked, 40);
  EXPECT_GT(sprinted_after_recovering, 0);
  ExpectMovedAlike(adversary_from, honest_from);
}

// Impersonation, structural: no client message carries a Session ID, so the
// server knows whose Command it is by the connection alone. The adversary
// numbers its Commands on from the honest player's, walking and firing, while
// the honest player stands still.
// Requirements: US-15, NFR-05
TEST_F(CorrectedActionTest, CommandsNumberedAsAnotherPlayersMoveAndFireOnlyTheSendersOwnBody) {
  const auto honest_before = Honest().GetAuthoritativeState();
  ASSERT_TRUE(honest_before.has_value());
  const Vec3 adversary_from = Seen(Adversary());
  const Vec3 honest_from = Seen(HonestEntity());
  sent_ = std::max(sent_, honest_before->acknowledged_sequence);

  PlayFor(30, {Intent(kFire, Vec3(1.0F, 0.0F, 0.0F))});

  EXPECT_NEAR(Seen(HonestEntity()).x, honest_from.x, kAlike);
  EXPECT_NEAR(Seen(HonestEntity()).z, honest_from.z, kAlike);
  EXPECT_EQ(Honest().GetAuthoritativeState()->rifle.rounds, kMagazine);
  EXPECT_TRUE(ShotTicks(HonestEntity()).empty());
  EXPECT_GT(Seen(Adversary()).x - adversary_from.x, 1.0F);
  // A round every kTicksPerRound of the 30 ticks.
  EXPECT_EQ(ShotTicks(Adversary()).size(), 30U / kTicksPerRound);
}

// The adversary and an honest player 60 m apart down -Z on the floor, playing
// the character of HumanHitboxes. A round takes 50 of the 100 of health at the
// head, 20 at the torso and 10 at a limb, and reaches the other player on the
// tick it is fired. The honest player is the target, and sprints (on stamina
// that never drains) across the adversary's view, 0.08 m a tick: that the
// middle of its right arm, 0.1 m wide, is where a round is aimed hits only if
// the round is judged against the very moment it was aimed at, which shows
// where Lag compensation judged it.
class CorrectedAimTest : public AdversaryMatch {
 protected:
  static constexpr float kEyeHeight = 1.6F;
  static constexpr float kHeadHeight = 1.65F;
  static constexpr float kLimbDamage = 10.0F;
  static constexpr float kStartingHealth = 100.0F;
  // The middle of the right arm, beside the torso, from the feet of a body facing yaw 0.
  static inline const Vec3 kArm{0.35F, 1.2F, 0.0F};
  // How long the target sprints before the first round: longer than the Shooter's delay's cap.
  static constexpr int kStrafeTicks = 20;
  // The ticks after a tap of fire until the rifle is ready again.
  static constexpr int kRoundTicks = 6;

  static HostSetup DownTheLine() {
    Parameters parameters = WithPlayerCount(2);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = 30;
    parameters.rifle.muzzle_velocity = 8000.0F;
    parameters.ammo.max_range = 200.0F;
    parameters.ammo.damage = {.head = 50.0F, .torso = 20.0F, .limb = kLimbDamage};
    parameters.starting_health = kStartingHealth;
    HostSetup setup = OnTheFloor({Vec3(0.0F, kFloorY, 0.0F), Vec3(0.0F, kFloorY, -60.0F)}, parameters);
    setup.scenario.characters.front().eye = Vec3(0.0F, kEyeHeight, 0.0F);
    setup.scenario.characters.front().hitboxes = HumanHitboxes();
    return setup;
  }

  CorrectedAimTest() : AdversaryMatch(DownTheLine()) {}

  void SetUp() override { StartWithAnHonestPlayer(); }

  Session& Target() { return *sessions_.front(); }
  Command& TargetCommand() { return honest_commands_.front(); }
  EntityIdWire TargetWire() { return EntityIdWire{std::to_underlying(*Target().GetEntityId())}; }

  // The Shooter's delay's cap in ticks: 250 ms at 60 Hz.
  static Tick Cap() { return augusta::simulation::HitboxHistoryTicks(kTestTickRate); }

  // Where the update of tick the adversary was told of put entity's feet.
  Vec3 SeenOn(Tick tick, EntityIdWire entity) const {
    for (const auto& state : adversary_.ReceivedOf<protocol::AuthoritativeStateWire>()) {
      if (state.tick != tick) {
        continue;
      }
      for (const auto& body : state.bodies) {
        if (body.entity == entity) {
          return body.body.position;
        }
      }
    }
    ADD_FAILURE() << "the adversary was told of no body " << std::to_underlying(entity) << " on tick " << tick;
    return {};
  }

  // A view's yaw and pitch.
  struct View {
    float yaw = 0.0F;
    float pitch = 0.0F;
  };

  // The view from eye to point.
  static View ViewFrom(const Vec3& eye, const Vec3& point) {
    const Vec3 aim = point - eye;
    return View{.yaw = std::atan2(-aim.x, -aim.z), .pitch = std::asin(aim.y / Length(aim))};
  }

  // The target sprints along +X for kStrafeTicks.
  void Strafe() {
    TargetCommand().movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    TargetCommand().movement.sprint = true;
    for (int i = 0; i < kStrafeTicks; ++i) {
      Play();
    }
  }

  // On the next tick the adversary taps fire, aimed from its eye at point and
  // reporting the Seen time of seen_tick and fraction; the match then runs until
  // its rifle is ready again.
  void FireAt(const Vec3& point, Tick seen_tick, float fraction) {
    const Tick told = AdversaryTold().tick;
    protocol::CommandWire command = Intent(kFire);
    const View view = ViewFrom(SeenOn(told, adversary_entity_) + Vec3(0.0F, kEyeHeight, 0.0F), point);
    command.yaw = view.yaw;
    command.pitch = view.pitch;
    command.seen_fraction = fraction;
    Play(Numbered({command}, seen_tick));
    // Fired on the tick it was aimed for.
    EXPECT_EQ(AdversaryTold().tick, told + 1);
    for (int i = 0; i < kRoundTicks; ++i) {
      Play();
    }
  }

  // Expects each of rounds rounds of the adversary's to have hit the target's
  // arm, as both are told: a Hit confirmation of each to the adversary, and the
  // target's health down by each.
  void ExpectArmHits(std::size_t rounds) {
    ASSERT_TRUE(adversary_.ServeUntil(
        host_, [&] { return adversary_.ReceivedOf<protocol::HitConfirmationWire>().size() >= rounds; }));
    const auto confirmations = adversary_.ReceivedOf<protocol::HitConfirmationWire>();
    ASSERT_EQ(confirmations.size(), rounds);
    for (const auto& confirmation : confirmations) {
      EXPECT_EQ(confirmation.target, TargetWire());
      EXPECT_EQ(confirmation.part, protocol::BodyPartWire::kLimb);
      EXPECT_EQ(confirmation.damage, kLimbDamage);
    }
    EXPECT_EQ(Target().GetHealth(), kStartingHealth - (static_cast<float>(rounds) * kLimbDamage));
  }
};

// Seen time too old: one older than the Shooter's delay's cap is judged at the
// cap, as one reporting a Seen time exactly that old is (ADR-0044).
// Requirements: US-15, NFR-05
TEST_F(CorrectedAimTest, ASeenTimeOlderThanTheShootersDelaysCapIsJudgedAtTheCap) {
  Strafe();

  const Tick honest = NextTick();
  FireAt(SeenOn(honest - Cap(), TargetWire()) + kArm, honest - Cap(), 0.0F);
  const Tick impossible = NextTick();
  FireAt(SeenOn(impossible - Cap(), TargetWire()) + kArm, 0, 0.0F);

  ExpectArmHits(2);
}

// Seen time in the future: one newer than any update sent is judged at the newest
// sent, the last tick's, as one reporting that update is.
// Requirements: US-15, NFR-05
TEST_F(CorrectedAimTest, ASeenTimeNewerThanAnyUpdateSentIsJudgedAtTheNewestSent) {
  Strafe();

  const Tick honest = NextTick();
  FireAt(SeenOn(honest - 1, TargetWire()) + kArm, honest - 1, 0.0F);
  const Tick impossible = NextTick();
  FireAt(SeenOn(impossible - 1, TargetWire()) + kArm, impossible + 1'000, 0.5F);

  ExpectArmHits(2);
}

// Seen time fraction: the wire carries a fraction from 0 to 255/256 (augusta/grid.h),
// so one past 1 arrives as 255/256 and one below 0 as 0, and Lag compensation
// holds what arrives within 0 to 1 besides.
// Requirements: US-15, NFR-05
TEST_F(CorrectedAimTest, ASeenTimeFractionOutsideZeroToOneIsHeldWithinIt) {
  constexpr float kNearlyOne = 255.0F / 256.0F;
  // A round reporting the Seen time of 5 ticks before it, well within the Hitbox
  // history, at sent, aimed where the target was at judged of the way to the next update.
  const auto fire_with = [&](float judged, float sent) {
    const Tick seen = NextTick() - 5;
    FireAt(augusta::math::Lerp(SeenOn(seen, TargetWire()), SeenOn(seen + 1, TargetWire()), judged) + kArm, seen, sent);
  };
  Strafe();

  fire_with(kNearlyOne, kNearlyOne);
  fire_with(kNearlyOne, 7.0F);
  fire_with(0.0F, 0.0F);
  fire_with(0.0F, -3.0F);

  ExpectArmHits(4);
}

// Spectator: a dead player's body has left the simulation, so its Commands have
// nothing to move, turn or fire, which is what a dead honest client, sending
// nothing, gets too.
// Requirements: US-15, NFR-05
TEST_F(CorrectedAimTest, ADeadPlayersCommandsMoveTurnAndFireNothing) {
  // The target shoots the adversary twice in the head, from the update it was told last.
  for (int round = 0; round < 2; ++round) {
    const Vec3 eye = PositionSeenBy(Target(), *Target().GetEntityId()).value() + Vec3(0.0F, kEyeHeight, 0.0F);
    const Vec3 head = PositionSeenBy(Target(), Adversary()).value() + Vec3(0.0F, kHeadHeight, 0.0F);
    const View view = ViewFrom(eye, head);
    TargetCommand().yaw = view.yaw;
    TargetCommand().pitch = view.pitch;
    TargetCommand().seen_tick = Target().GetAuthoritativeState()->tick;
    TargetCommand().fire = true;
    Play();
    TargetCommand().fire = false;
    for (int i = 0; i < kRoundTicks; ++i) {
      Play();
    }
  }
  ASSERT_EQ(AdversaryTold().health, 0.0F);
  const std::vector<Death> deaths = Target().TakeDeaths();
  ASSERT_EQ(deaths.size(), 1U);
  ASSERT_EQ(deaths.front().victim, Adversary());
  const Vec3 target = PositionSeenBy(Target(), *Target().GetEntityId()).value();

  // It walks at the target, turned to face it, sprinting and firing.
  protocol::CommandWire charge = Intent(kFire | kSprint, Vec3(0.0F, 0.0F, 1.0F));
  charge.yaw = std::numbers::pi_v<float>;
  PlayFor(20, {charge});

  EXPECT_TRUE(ShotTicks(Adversary()).empty());
  EXPECT_TRUE(adversary_.ReceivedOf<protocol::HitConfirmationWire>().empty());
  EXPECT_FALSE(BodySeenBy(Target(), Adversary()).has_value());
  EXPECT_EQ(Target().GetHealth(), kStartingHealth);
  EXPECT_TRUE(Target().IsAlive());
  EXPECT_NEAR(PositionSeenBy(Target(), *Target().GetEntityId())->z, target.z, 0.01F);
  EXPECT_TRUE(Target().TakeDeaths().empty());
}

// Reported outcomes, structural: no client message carries a hit, a damage, a
// health or a kill. The adversary fires wide twice, the second time claiming,
// in the messages only the server sends, that its round hit and killed the
// target and won it the Match: it gets what the first, honest, round got.
// Requirements: US-15, NFR-05
TEST_F(CorrectedAimTest, AClientThatClaimsAHitAKillAndAWinHurtsNoOne) {
  const auto wide = [&] { return SeenOn(AdversaryTold().tick, TargetWire()) + Vec3(5.0F, kHeadHeight, 0.0F); };
  FireAt(wide(), AdversaryTold().tick, 0.0F);

  adversary_.Send(
      protocol::HitConfirmationWire{.target = TargetWire(), .damage = 1000.0F, .part = protocol::BodyPartWire::kHead});
  adversary_.Send(protocol::DeathWire{.victim = TargetWire(), .killer = adversary_entity_});
  adversary_.Send(protocol::AuthoritativeStateWire{.tick = NextTick(), .bodies = {}, .health = 0.0F});
  adversary_.Send(protocol::MatchEndWire{.winner = *adversary_.GetSessionId()});
  FireAt(wide(), AdversaryTold().tick, 0.0F);

  EXPECT_EQ(ShotTicks(Adversary()).size(), 2U);
  EXPECT_TRUE(adversary_.ReceivedOf<protocol::HitConfirmationWire>().empty());
  EXPECT_TRUE(adversary_.ReceivedOf<protocol::DeathWire>().empty());
  EXPECT_TRUE(adversary_.ReceivedOf<protocol::MatchEndWire>().empty());
  EXPECT_EQ(Target().GetHealth(), kStartingHealth);
  EXPECT_TRUE(Target().IsAlive());
  EXPECT_TRUE(Target().TakeDeaths().empty());
  EXPECT_EQ(Target().GetPhase(), Phase::kMatch);
}

// Lobby: Commands from a player in the Lobby are dropped at the server, so its
// Match starts as an honest player's does, which sends none there: at its
// spawn point, facing yaw 0, with a full magazine and none of them taken in.
using CorrectedLobbyTest = ImpossibleLobbyOf<2>;

// Requirements: US-15, NFR-05
TEST_F(CorrectedLobbyTest, CommandsFromAPlayerInTheLobbyAffectNothing) {
  Session& honest = Join();
  RawClient adversary(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(AskToJoin(adversary, HonestJoinRequest()));
  const auto current = RosterVersionOf(adversary, 2);
  ASSERT_TRUE(current.has_value());
  // Walking, turned, firing and reloading, numbered from 1, as the first Commands of a Match would be.
  protocol::CommandsWire walking;
  for (augusta::command::Sequence sequence = 1; sequence <= protocol::kMaxCommandsPerMessage; ++sequence) {
    walking.commands.push_back({.sequence = sequence,
                                .command = {.direction = Vec3(1.0F, 0.0F, 0.0F),
                                            .yaw = 1.0F,
                                            .flags = protocol::CommandWire::kFire | protocol::CommandWire::kReload}});
  }

  for (int i = 0; i < 3; ++i) {
    adversary.Send(walking);
  }
  RunLobby({&adversary});
  ASSERT_EQ(honest.GetPhase(), Phase::kLobby);
  adversary.Send(protocol::ReadyWire{.version = *current});
  ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_), &adversary));
  Run(kSettleTicks);
  ASSERT_TRUE(adversary.ServeUntil(host_, [&] { return adversary.NewestState().has_value(); }));
  Settle(host_, Pointers(sessions_));

  const auto entity = adversary.Entity();
  ASSERT_TRUE(entity.has_value());
  Vec3 spawn{};
  for (const auto& player : adversary.ReceivedOf<protocol::MatchStartWire>().back().players) {
    if (player.entity == *entity) {
      spawn = player.spawn;
    }
  }
  const protocol::AuthoritativeStateWire told = adversary.NewestState().value();
  const auto honest_told = honest.GetAuthoritativeState();
  ASSERT_TRUE(honest_told.has_value());
  EXPECT_EQ(told.acknowledged_sequence, 0U);
  EXPECT_EQ(told.rifle.rounds, honest_told->rifle.rounds);
  EXPECT_EQ(told.health, honest_told->health);
  for (const auto& body : told.bodies) {
    if (body.entity == *entity) {
      EXPECT_NEAR(body.body.position.x, spawn.x, 0.01F);
      EXPECT_NEAR(body.body.position.z, spawn.z, 0.01F);
      EXPECT_EQ(body.yaw, 0.0F);
    }
  }
  const auto seen = PositionSeenBy(honest, static_cast<EntityId>(std::to_underlying(*entity)));
  ASSERT_TRUE(seen.has_value());
  EXPECT_NEAR(seen->x, spawn.x, 0.01F);
  EXPECT_NEAR(seen->z, spawn.z, 0.01F);
  EXPECT_TRUE(honest.TakeShots().empty());

  // Now that it plays, its Commands are taken in from 1.
  protocol::SequencedCommandWire first{.sequence = 1};
  first.command.flags = protocol::CommandWire::kFire;
  adversary.Send(protocol::CommandsWire{.commands = {first}});
  Run(kSettleTicks);
  ASSERT_TRUE(adversary.ServeUntil(host_, [&] { return adversary.NewestState()->acknowledged_sequence == 1U; }));
  EXPECT_EQ(adversary.NewestState()->rifle.rounds, honest_told->rifle.rounds - 1);
}

// What the Host counts for the metrics endpoint (ADR-0049), read through the
// Host alone, never over HTTP: each event where it happens, once, for both the
// series and the heartbeat line.
using augusta::server::HostMetrics;
using augusta::server::Leaving;
using augusta::server::MessageType;

using HostCountsTest = MatchOf<2>;

// Every command the Host discarded, for whatever reason.
std::uint64_t Discarded(const HostMetrics& metrics) {
  return metrics.commands_rejected.Total() + metrics.commands_overflowed.Value() +
         metrics.commands_outside_match.Value() + metrics.commands_before_joining.Value();
}

// Requirements: NFR-07
TEST_F(HostCountsTest, EveryTickIsCountedWithHowItKeptToTheSchedule) {
  host_.RecordTiming({.duration = std::chrono::milliseconds(3), .late = true});
  host_.RecordTiming({.duration = std::chrono::milliseconds(30), .overrun = true, .resynchronised = true});

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.ticks.Value(), 2U);
  EXPECT_EQ(metrics.ticks_late.Value(), 1U);
  EXPECT_EQ(metrics.tick_overruns.Value(), 1U);
  EXPECT_EQ(metrics.tick_resyncs.Value(), 1U);
  const auto durations = metrics.tick_duration.Read();
  EXPECT_EQ(durations.cumulative_counts.back(), 2U);
  EXPECT_NEAR(durations.sum, 0.033, 1e-9);
  EXPECT_EQ(metrics.tick_rate_hz, kTestTickRate);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, JoinsAreCountedByResultAndTheSessionsAndTheLobbyByWhoIsIn) {
  Join();
  Join();
  const Session& third = Connect();
  ASSERT_EQ(third.GetRefusal(), JoinRefusal::kLobbyFull);

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.joins_admitted.Value(), 2U);
  EXPECT_EQ(metrics.joins_refused[augusta::server::JoinRefusal::kLobbyFull].Value(), 1U);
  EXPECT_EQ(metrics.joins_refused.Total(), 1U);
  EXPECT_EQ(metrics.sessions.Value(), 2.0);
  EXPECT_EQ(metrics.lobby_players.Value(), 2.0);
  EXPECT_EQ(metrics.match_in_progress.Value(), 0.0);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, AMatchIsCountedFromItsStartToItsEnd) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);
  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.matches_started.Value(), 1U);
  EXPECT_EQ(metrics.match_in_progress.Value(), 1.0);
  EXPECT_EQ(metrics.match_players_alive.Value(), 2.0);
  EXPECT_EQ(metrics.lobby_players.Value(), 0.0);
  EXPECT_EQ(metrics.sessions.Value(), 2.0);

  host_.EndMatch();

  EXPECT_EQ(metrics.matches_ended_drawn.Value(), 1U);
  EXPECT_EQ(metrics.matches_ended_with_winner.Value() + metrics.matches_ended_abandoned.Value(), 0U);
  EXPECT_EQ(metrics.match_in_progress.Value(), 0.0);
  EXPECT_EQ(metrics.lobby_players.Value(), 2.0);
  const auto durations = metrics.match_duration.Read();
  EXPECT_EQ(durations.cumulative_counts.back(), 1U);
  EXPECT_GE(durations.sum, static_cast<double>(kSettleTicks) / kTestTickRate);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, AMatchWhoseLastPlayerLeavesIsAbandonedAndEachDepartureIsCountedFromTheMatch) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks);

  for (const auto& session : sessions_) {
    session->Disconnect();
  }
  const HostMetrics& metrics = host_.Metrics();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return metrics.sessions.Value() == 0.0; }));

  EXPECT_EQ(metrics.disconnects_from_match[Leaving::kLeft].Value(), 2U);
  EXPECT_EQ(metrics.disconnects_from_lobby.Total(), 0U);
  EXPECT_EQ(metrics.matches_ended_abandoned.Value(), 1U);
  EXPECT_EQ(metrics.match_in_progress.Value(), 0.0);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, APlayerWhoLeavesTheLobbyIsCountedFromTheLobby) {
  Join().Disconnect();
  const HostMetrics& metrics = host_.Metrics();
  ASSERT_TRUE(ExchangeUntil(host_, All(), [&] { return metrics.sessions.Value() == 0.0; }));

  EXPECT_EQ(metrics.disconnects_from_lobby[Leaving::kLeft].Value(), 1U);
  EXPECT_EQ(metrics.disconnects_from_match.Total(), 0U);
  EXPECT_EQ(metrics.lobby_players.Value(), 0.0);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, EveryMessageIsCountedByTypeAndEveryUpdateByItsSize) {
  Join();
  Join();
  ASSERT_TRUE(StartMatch());
  Run(kSettleTicks, Forward());

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.messages_received[MessageType::kJoinRequest].Value(), 2U);
  EXPECT_GE(metrics.messages_received[MessageType::kReady].Value(), 2U);
  EXPECT_GT(metrics.messages_received[MessageType::kCommands].Value(), 0U);
  EXPECT_GT(metrics.commands_received.Value(), 0U);
  EXPECT_EQ(metrics.messages_sent[MessageType::kJoinAccepted].Value(), 2U);
  EXPECT_EQ(metrics.messages_sent[MessageType::kMatchStart].Value(), 2U);
  const std::uint64_t updates = metrics.messages_sent[MessageType::kAuthoritativeState].Value();
  EXPECT_GE(updates, 2U * kSettleTicks);
  const auto sizes = metrics.authoritative_state_update_bytes.Read();
  EXPECT_EQ(sizes.cumulative_counts.back(), updates);
  EXPECT_GT(static_cast<double>(metrics.sent_bytes.Value()), sizes.sum);
  EXPECT_GT(metrics.received_bytes.Value(), 0U);
  EXPECT_EQ(augusta::server::Totals(metrics).messages, metrics.messages_received.Total());
}

// Requirements: NFR-07
TEST_F(HostCountsTest, AFloodOfMalformedMessagesIsCountedAsMisbehaviourAndItsDisconnectOnce) {
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));

  for (std::size_t i = 0; i < kFlood; ++i) {
    raw.SendPayload(kUndecodable);
  }
  ASSERT_TRUE(raw.ServeUntil(host_, [&] { return raw.GetConnectionState() == ConnectionState::kDisconnected; }));

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.misbehaviour[augusta::server::PeerRejection::kUndecodable].Value(),
            augusta::server::kMisbehaviourThreshold);
  EXPECT_EQ(metrics.misbehaviour.Total(), augusta::server::kMisbehaviourThreshold);
  EXPECT_EQ(metrics.disconnects_from_lobby[Leaving::kMisbehaving].Value(), 1U);
  EXPECT_GE(metrics.received_bytes.Value(), augusta::server::kMisbehaviourThreshold * kUndecodable.size());
  const augusta::server::Activity totals = augusta::server::Totals(metrics);
  EXPECT_EQ(totals.dropped, augusta::server::kMisbehaviourThreshold);
  EXPECT_EQ(totals.misbehaving, 1U);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, CommandsOutsideAMatchAreDiscardedAsRoutineNotAsMisbehaviour) {
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));

  raw.Send(protocol::CommandsWire{.commands = {{.sequence = 1}, {.sequence = 2}}});
  const HostMetrics& metrics = host_.Metrics();
  ASSERT_TRUE(raw.ServeUntil(host_, [&] { return metrics.commands_outside_match.Value() == 2U; }));

  EXPECT_EQ(metrics.commands_received.Value(), 2U);
  EXPECT_EQ(metrics.misbehaviour.Total(), 0U);
  EXPECT_EQ(augusta::server::Totals(metrics).stale, 2U);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, CommandsFromAPeerThatHasNotJoinedAreMisbehaviour) {
  RawClient raw(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(raw.Connect(host_));

  raw.Send(protocol::CommandsWire{.commands = {{.sequence = 1}}});
  const HostMetrics& metrics = host_.Metrics();
  ASSERT_TRUE(raw.ServeUntil(host_, [&] { return metrics.misbehaviour.Total() == 1U; }));

  EXPECT_EQ(metrics.misbehaviour[augusta::server::PeerRejection::kCommandsBeforeJoining].Value(), 1U);
  EXPECT_EQ(metrics.joins_admitted.Value(), 0U);
  EXPECT_EQ(metrics.commands_received.Value(), 1U);
  EXPECT_EQ(metrics.commands_before_joining.Value(), 1U);
  EXPECT_EQ(metrics.commands_outside_match.Value(), 0U);
}

// Requirements: NFR-07
TEST_F(HostCountsTest, APeerDisconnectedForNotBeingAdmittedInTimeIsCountedBeforeAdmission) {
  RawClient raw(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(raw.Connect(host_));

  const auto past_the_deadline = std::chrono::steady_clock::now() + augusta::server::kAdmissionDeadline;
  ASSERT_TRUE(raw.ServeUntil(
      host_, [&] { return raw.GetConnectionState() == ConnectionState::kDisconnected; }, past_the_deadline));

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.disconnects_before_admission[Leaving::kMisbehaving].Value(), 1U);
  EXPECT_EQ(metrics.disconnects_from_lobby.Total() + metrics.disconnects_from_match.Total(), 0U);
  EXPECT_EQ(augusta::server::Totals(metrics).misbehaving, 1U);
}

using SoloHostCountsTest = MatchOf<1>;

// Requirements: NFR-07
TEST_F(SoloHostCountsTest, CommandsTheQueueTurnsAwayAreDiscardedByWhy) {
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));
  ASSERT_TRUE(DriveIntoMatch(host_, {}, &raw));
  protocol::SequencedCommandWire out_of_range{.sequence = 2};
  out_of_range.command.pitch = 3.0F;

  raw.Send(protocol::CommandsWire{.commands = {{.sequence = 1}, {.sequence = 1}, out_of_range}});
  const HostMetrics& metrics = host_.Metrics();
  ASSERT_TRUE(raw.ServeUntil(host_, [&] { return metrics.commands_rejected.Total() == 2U; }));

  EXPECT_EQ(metrics.commands_received.Value(), 3U);
  EXPECT_EQ(metrics.commands_rejected[augusta::server::Rejection::kStale].Value(), 1U);
  EXPECT_EQ(metrics.commands_rejected[augusta::server::Rejection::kOutOfRange].Value(), 1U);
  EXPECT_EQ(metrics.misbehaviour[augusta::server::PeerRejection::kOutOfRangeCommand].Value(), 1U);
  EXPECT_EQ(metrics.misbehaviour.Total(), 1U);
  EXPECT_EQ(metrics.commands_received.Value() - Discarded(metrics), 1U);
}

// Commands still in a message when its sender is disconnected for misbehaving
// are never taken in, and never counted received: every command counted
// received is either taken in or discarded.
// Requirements: NFR-07
TEST_F(SoloHostCountsTest, CommandsLeftInTheMessageOfAPeerDisconnectedMidwayAreNeitherReceivedNorDiscarded) {
  RawClient raw(host_.ListenEndpoint());
  ASSERT_TRUE(raw.Join(host_));
  ASSERT_TRUE(DriveIntoMatch(host_, {}, &raw));
  augusta::command::Sequence sequence = 0;
  const auto out_of_range = [&](std::size_t count) {
    protocol::CommandsWire message;
    for (std::size_t i = 0; i < count; ++i) {
      protocol::SequencedCommandWire command{.sequence = ++sequence};
      command.command.pitch = 3.0F;
      message.commands.push_back(command);
    }
    return message;
  };

  // 7 and 7, then the 16th misbehaviour is the second command of 8.
  raw.Send(out_of_range(7));
  raw.Send(out_of_range(7));
  raw.Send(out_of_range(protocol::kMaxCommandsPerMessage));
  ASSERT_TRUE(raw.ServeUntil(host_, [&] { return raw.GetConnectionState() == ConnectionState::kDisconnected; }));

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.misbehaviour.Total(), augusta::server::kMisbehaviourThreshold);
  EXPECT_EQ(metrics.commands_received.Value(), augusta::server::kMisbehaviourThreshold);
  EXPECT_EQ(Discarded(metrics), metrics.commands_received.Value());
}

// A command that reports no Seen time is judged at the Shooter's delay's cap.
using FireCountsTest = FireMatchOf<1>;

// Requirements: NFR-07
TEST_F(FireCountsTest, EveryRoundIsCountedWithItsShootersDelayAndARoundAtTheCapAsCapped) {
  Step(Firing());
  Run(kTicksPerRound);

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.shots.Value(), 1U);
  const auto delays = metrics.shooters_delay.Read();
  EXPECT_EQ(delays.cumulative_counts.back(), 1U);
  EXPECT_FLOAT_EQ(static_cast<float>(delays.sum),
                  std::chrono::duration<float>(augusta::simulation::kMaxShootersDelay).count());
  EXPECT_EQ(metrics.shooters_delay_capped.Value(), 1U);
}

// 800 m/s against a range of 1000 m: a round flies 75 ticks.
// Requirements: NFR-07
TEST_F(FireCountsTest, ARoundIsCountedInFlightUntilItsFlightEnds) {
  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.bullets_in_flight.Value(), 0.0);

  Step(Firing());
  Run(kTicksPerRound);
  EXPECT_EQ(metrics.bullets_in_flight.Value(), 1.0);

  Run(75);
  EXPECT_EQ(metrics.bullets_in_flight.Value(), 0.0);
}

using WinnerCountsTest = LastStandingMatchOf<2>;

// Requirements: NFR-07
TEST_F(WinnerCountsTest, EveryHitIsCountedByBodyPartAndAMatchWithAWinnerAsWon) {
  ShootAt(Standing(1), Vec3(0.0F, kTorsoHeight, 0.0F));
  Kill(Standing(1));
  ASSERT_EQ(match_ends_.size(), 1U);

  const HostMetrics& metrics = host_.Metrics();
  EXPECT_EQ(metrics.shots.Value(), shots_fired_.size());
  EXPECT_EQ(metrics.hit_confirmations[BodyPart::kTorso].Value(), 1U);
  EXPECT_EQ(metrics.hit_confirmations[BodyPart::kHead].Value(), 2U);
  EXPECT_EQ(metrics.hit_confirmations.Total(), hits_.size());
  EXPECT_EQ(metrics.shooters_delay.Read().cumulative_counts.back(), shots_fired_.size());
  EXPECT_EQ(metrics.shooters_delay_capped.Value(), 0U);
  EXPECT_EQ(metrics.matches_ended_with_winner.Value(), 1U);
}

}  // namespace
