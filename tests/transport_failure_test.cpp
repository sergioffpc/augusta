#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/primitives.h"
#include "augusta/runner.h"
#include "augusta/supervisor.h"
#include "content.h"
#include "host.h"
#include "host_metrics.h"
#include "misbehaviour.h"
#include "runtime.h"
#include "tick_messages.h"

// Local transport failures at the runtime seams (ADR-0033): a send or receive
// the local transport refuses becomes the runtime's typed failure, which its
// worker escalates to the supervisor, while what a peer does - malformed
// input, leaving - stays that peer's. A runtime that has failed, on its
// transport or on a broken invariant, sends nothing more. The transport is
// made to fail by controlled fault injection (failure::Faults), never by
// breaking the real one; everything else is a real Host and Session over
// loopback.
namespace {

using augusta::command::Command;
using augusta::failure::Code;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Failure;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::harness::Runner;
using augusta::harness::RunnerHooks;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::networking::Payload;
using augusta::networking::PeerId;
using augusta::networking::Reliability;
using augusta::networking::SendOutcome;
using augusta::physics::CollisionMesh;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::PeerRejection;
using augusta::server::Scenario;
using augusta::server::ServerRuntime;

constexpr std::uint8_t kTickRate = 60;
constexpr float kFixedTick = 1.0F / kTickRate;
constexpr auto kPollInterval = std::chrono::milliseconds(5);
constexpr auto kDeadline = std::chrono::seconds(5);
// Long enough for a message sent over loopback to have arrived, had it been sent.
constexpr auto kSettle = std::chrono::milliseconds(300);
constexpr const char* kCharacter = "soldier";

class TransportFailureEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kTransportFailureEnvironment =
    ::testing::AddGlobalTestEnvironment(new TransportFailureEnvironment);

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  constexpr float kHeight = -0.5F;
  return CollisionMesh{.points = {Vec3(-kExtent, kHeight, -kExtent), Vec3(-kExtent, kHeight, kExtent),
                                  Vec3(kExtent, kHeight, kExtent), Vec3(kExtent, kHeight, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

HostConfig HostConfigWith(Faults* faults) {
  return HostConfig{.tick_rate_hz = kTickRate,
                    .parameters = {},
                    .listen = Endpoint{.address = "127.0.0.1:0"},
                    .recording = {},
                    .recording_mode = {},
                    .server_pack = {},
                    .capture_directory = {},
                    .capture_mode = {},
                    .faults = faults};
}

Scenario TestScenario() {
  return Scenario{.collision = {Floor()}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}};
}

augusta::prediction::World WorldWithFloor() {
  augusta::prediction::World world;
  EXPECT_TRUE(world.AddCollisionMesh(Floor()).has_value());
  return world;
}

// The value of the failure's context under key, or empty if it has none.
std::string ContextOf(const Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
      return field.value;
    }
  }
  return {};
}

// Calls poll until done() holds or the deadline passes; returns whether it held.
template <typename Poll, typename Done>
bool PollUntil(Poll poll, Done done) {
  const auto deadline = std::chrono::steady_clock::now() + kDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    poll();
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return false;
}

// A Host and a Session that connects to it, each with its own faults, driven
// by hand.
class TransportFailureTest : public ::testing::Test {
 protected:
  TransportFailureTest()
      : host_(HostConfigWith(&host_faults_), TestScenario()),
        session_(SessionConfig{.server = host_.ListenEndpoint(), .character = kCharacter, .faults = &session_faults_},
                 WorldWithFloor()) {}

  // One round of both sides' network work.
  void Exchange() {
    host_.PumpNetwork(std::chrono::steady_clock::now());
    session_.PumpEvents();
    session_.ExchangeMessages();
  }

  bool ExchangeUntil(const auto& done) {
    return PollUntil([this] { Exchange(); }, done);
  }

  // Rounds of both sides' network work for kSettle.
  void Settle() {
    const auto end = std::chrono::steady_clock::now() + kSettle;
    while (std::chrono::steady_clock::now() < end) {
      Exchange();
      std::this_thread::sleep_for(kPollInterval);
    }
  }

  // Declared before what asks them, so they outlive it.
  Faults host_faults_;
  Faults session_faults_;
  Host host_;
  Session session_;
};

TEST_F(TransportFailureTest, AClientJoinsWithNoFailureAndEverySentMessageIsCounted) {
  session_.Connect();

  ASSERT_TRUE(ExchangeUntil([&] { return session_.GetSessionId().has_value(); }));

  EXPECT_FALSE(host_.TakeTransportFailure().has_value());
  EXPECT_FALSE(session_.TakeTransportFailure().has_value());
  // Join accepted and the Roster, at least.
  EXPECT_GE(host_.Metrics().messages_sent.Total(), 2U);
  EXPECT_GT(host_.Metrics().sent_bytes.Value(), 0U);
}

// A reply the local transport refused never reaches the client and is never
// counted as sent: the metrics count the transport's accepted work, not the
// Host's attempts.
TEST_F(TransportFailureTest, AServerSendTheLocalTransportRefusesIsTheRuntimesFailureAndIsNotCounted) {
  host_faults_.Arm(Site::kTransportSend, "socket closed", Faults::kEveryTime);
  session_.Connect();

  ASSERT_TRUE(ExchangeUntil([&] { return host_.Metrics().messages_received.Total() > 0; }));
  Settle();

  const std::optional<Failure> failure = host_.TakeTransportFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kTransportSendFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(failure->detail, "socket closed");
  EXPECT_EQ(host_.Metrics().messages_sent.Total(), 0U);
  EXPECT_EQ(host_.Metrics().sent_bytes.Value(), 0U);
  EXPECT_FALSE(session_.GetSessionId().has_value()) << "a refused send arrived";
}

// A runtime that has failed sends nothing more, valid messages included, until
// its worker stops it: the Roster that follows a refused Join accepted is not
// sent either.
TEST_F(TransportFailureTest, AfterAServerSendFailsTheHostSendsNothingMore) {
  host_faults_.Arm(Site::kTransportSend, "socket closed");
  session_.Connect();

  ASSERT_TRUE(ExchangeUntil([&] { return host_.Metrics().messages_received.Total() > 0; }));
  Settle();

  const std::optional<Failure> failure = host_.TakeTransportFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kTransportSendFailed);
  EXPECT_EQ(host_.Metrics().messages_sent.Total(), 0U) << "the Roster was sent after the runtime failed";
  EXPECT_FALSE(session_.GetLobby().has_value());
}

TEST_F(TransportFailureTest, AServerReceiveTheLocalTransportFailsIsTheRuntimesFailure) {
  host_faults_.Arm(Site::kTransportReceive, "poll group gone");

  host_.PumpNetwork(std::chrono::steady_clock::now());

  const std::optional<Failure> failure = host_.TakeTransportFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kTransportReceiveFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
}

// Exactly one worker escalates it, so the supervisor reports it once.
TEST_F(TransportFailureTest, ATransportFailureIsTakenOnlyOnce) {
  host_faults_.Arm(Site::kTransportReceive, "poll group gone", 2);
  host_.PumpNetwork(std::chrono::steady_clock::now());
  host_.PumpNetwork(std::chrono::steady_clock::now());

  EXPECT_TRUE(host_.TakeTransportFailure().has_value());
  EXPECT_FALSE(host_.TakeTransportFailure().has_value());
}

// The other side of the same seam: a peer's malformed input is the peer's,
// dropped and judged, and the server's transport has not failed.
TEST_F(TransportFailureTest, MalformedPeerInputIsThePeersOutcomeNotATransportFailure) {
  augusta::networking::Client intruder;
  intruder.Connect(host_.ListenEndpoint());
  ASSERT_TRUE(PollUntil(
      [&] {
        host_.PumpNetwork(std::chrono::steady_clock::now());
        intruder.PumpEvents();
      },
      [&] { return intruder.GetState() == augusta::networking::ConnectionState::kConnected; }));

  const Payload garbage{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
  ASSERT_EQ(intruder.Send(garbage, Reliability::kReliable), SendOutcome::kAccepted);
  ASSERT_TRUE(PollUntil([&] { host_.PumpNetwork(std::chrono::steady_clock::now()); },
                        [&] { return host_.Metrics().misbehaviour[PeerRejection::kUndecodable].Value() > 0; }));

  EXPECT_FALSE(host_.TakeTransportFailure().has_value());
  // And an honest client is still admitted.
  session_.Connect();
  EXPECT_TRUE(ExchangeUntil([&] { return session_.GetSessionId().has_value(); }));
  EXPECT_FALSE(host_.TakeTransportFailure().has_value());
}

TEST_F(TransportFailureTest, APeerLeavingIsItsSessionsOutcomeNotATransportFailure) {
  session_.Connect();
  ASSERT_TRUE(ExchangeUntil([&] { return session_.GetSessionId().has_value(); }));

  session_.Disconnect();
  ASSERT_TRUE(PollUntil([&] { host_.PumpNetwork(std::chrono::steady_clock::now()); },
                        [&] { return host_.Metrics().sessions.Value() == 0.0; }));
  static_cast<void>(host_.Tick(kFixedTick));

  EXPECT_FALSE(host_.TakeTransportFailure().has_value());
  EXPECT_FALSE(session_.TakeTransportFailure().has_value());
}

TEST_F(TransportFailureTest, AClientSendTheLocalTransportRefusesIsTheClientsRuntimeFailure) {
  session_faults_.Arm(Site::kTransportSend, "socket closed", Faults::kEveryTime);
  session_.Connect();

  std::optional<Failure> failure;
  ASSERT_TRUE(ExchangeUntil([&] {
    failure = session_.TakeTransportFailure();
    return failure.has_value();
  }));
  Settle();

  EXPECT_EQ(failure->code, Code::kTransportSendFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(host_.Metrics().messages_received.Total(), 0U) << "a refused join request arrived";
  // Not the server's doing: the Session itself has not failed.
  EXPECT_FALSE(session_.GetFailure().has_value());
}

// As the Host: once its send has failed, the Session sends nothing more, not
// even a Ready the transport would now take.
TEST_F(TransportFailureTest, AfterAClientSendFailsTheSessionSendsNothingMore) {
  session_.Connect();
  ASSERT_TRUE(ExchangeUntil([&] { return session_.GetLobby().has_value(); }));
  const std::uint32_t roster = session_.GetLobby()->version;
  const std::uint64_t received = host_.Metrics().messages_received.Total();

  session_faults_.Arm(Site::kTransportSend, "socket closed");
  session_.ReportReady(roster);
  ASSERT_TRUE(session_.TakeTransportFailure().has_value());
  session_.ReportReady(roster);
  Settle();

  EXPECT_EQ(host_.Metrics().messages_received.Total(), received) << "a Ready was sent after the runtime failed";
}

TEST_F(TransportFailureTest, AClientReceiveTheLocalTransportFailsIsTheClientsRuntimeFailure) {
  session_.Connect();
  ASSERT_TRUE(ExchangeUntil([&] { return session_.GetConnectionState() == ConnectionState::kConnected; }));
  session_faults_.Arm(Site::kTransportReceive, "connection handle gone");

  session_.ExchangeMessages();

  const std::optional<Failure> failure = session_.TakeTransportFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kTransportReceiveFailed);
  EXPECT_EQ(failure->detail, "connection handle gone");
}

// Through the Runner, as a client runs live: the failure becomes the Runner's
// first cause, and both threads stop. Whichever of its threads takes it first
// escalates it, so the thread named is either; the failure is the same.
TEST_F(TransportFailureTest, AClientTransportFailureStopsTheRunnerWithATypedFailure) {
  session_faults_.Arm(Site::kTransportReceive, "connection handle gone", Faults::kEveryTime);
  const Runner runner(session_,
                      RunnerHooks{.next_command = [] { return Command{}; }, .on_tick = {}, .on_network_round = {}});

  ASSERT_TRUE(PollUntil([&] { host_.PumpNetwork(std::chrono::steady_clock::now()); },
                        [&] { return runner.Failure().has_value(); }));

  const Failure failure = *runner.Failure();
  EXPECT_EQ(failure.code, Code::kTransportReceiveFailed);
  EXPECT_EQ(failure.detail, "connection handle gone");
  const std::string thread = ContextOf(failure, augusta::supervisor::kThreadContextKey);
  EXPECT_TRUE(thread == "network" || thread == "prediction") << thread;
}

// Through ServerRuntime, as augustad runs: both threads stop on the failure,
// and Run returns it as the runtime's first cause for the application boundary
// to report. Whichever thread takes it first escalates it.
TEST(ServerRuntimeTransportFailureTest, AServerTransportFailureEndsRunWithATypedFailure) {
  Faults faults;
  faults.Arm(Site::kTransportReceive, "poll group gone", Faults::kEveryTime);
  // Port 0: the metrics endpoint takes a free port, and is not what is tested.
  ServerRuntime runtime(HostConfigWith(&faults), 0, TestScenario());

  const std::optional<Failure> failure = runtime.Run();

  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kTransportReceiveFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(failure->detail, "poll group gone");
  const std::string thread = ContextOf(*failure, augusta::supervisor::kThreadContextKey);
  EXPECT_TRUE(thread == "network" || thread == "simulation") << thread;
}

// A server whose Parameters hold more Recoil kicks than the protocol carries
// cannot encode a Join accepted: a broken invariant (ADR-0033), sent to no one,
// after which the Host sends nothing more - not even the Roster the join
// changed.
TEST(HostInvariantFailureTest, AJoinAcceptedTheProtocolCannotCarryIsAnInvariantFailureAndNothingIsSent) {
  HostConfig config = HostConfigWith(nullptr);
  config.parameters.rifle.recoil_pattern.resize(augusta::primitives::kMaxRecoilKicks + 1);
  Host host(config, TestScenario());
  Session session(SessionConfig{.server = host.ListenEndpoint(), .character = kCharacter}, WorldWithFloor());
  const auto exchange = [&] {
    host.PumpNetwork(std::chrono::steady_clock::now());
    session.PumpEvents();
    session.ExchangeMessages();
  };
  session.Connect();

  ASSERT_TRUE(PollUntil(exchange, [&] { return host.Metrics().messages_received.Total() > 0; }));
  const auto settled = std::chrono::steady_clock::now() + kSettle;
  while (std::chrono::steady_clock::now() < settled) {
    exchange();
    std::this_thread::sleep_for(kPollInterval);
  }

  const std::optional<Failure> failure = host.TakeInvariantFailure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kInvariantViolated);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(host.Metrics().messages_sent.Total(), 0U);
  EXPECT_FALSE(session.GetSessionId().has_value());
  EXPECT_FALSE(session.GetLobby().has_value());
  EXPECT_FALSE(host.TakeTransportFailure().has_value());
}

// A message the peer's connection dropped was never sent, so the metrics do not
// count it, as they do not count one the local transport refused.
TEST(SendCountedTest, AMessageThePeersConnectionDroppedIsNotCounted) {
  augusta::networking::Server server(Endpoint{.address = "127.0.0.1:0"});
  augusta::server::HostMetrics metrics(kTickRate);

  const auto sent = augusta::server::SendCounted(server, metrics, PeerId{42}, Payload{std::byte{1}, std::byte{2}},
                                                 Reliability::kReliable);

  EXPECT_EQ(sent, SendOutcome::kDropped);
  EXPECT_EQ(metrics.messages_sent.Total(), 0U);
  EXPECT_EQ(metrics.sent_bytes.Value(), 0U);
}

}  // namespace
