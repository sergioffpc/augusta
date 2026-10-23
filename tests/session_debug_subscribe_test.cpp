#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/content.h"
#include "augusta/harness.h"
#include "augusta/host.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/misbehaviour.h"
#include "augusta/protocol.h"
#include "session_test.h"

// Debug subscribe end to end (ADR-0038, ADR-0074), on a real server host and
// real client sessions over loopback: a client asks for streams of the
// server's state, and the server keeps what each asked for and never holds it
// against them. Built only into a Development build's Session tests, whose
// session_test.cpp starts the Link.
namespace {

using augusta::harness::DebugSubscribe;
using augusta::harness::Phase;
using augusta::harness::Session;
using augusta::host::Host;
using augusta::host::HostConfig;
using augusta::host::MessageType;
using augusta::link::ConnectionState;
using augusta::math::Vec3;
using augusta::protocol::DebugSubscribeWire;
using augusta::scenario::Scenario;
using augusta::session_test::DriveIntoMatch;
using augusta::session_test::ExchangeUntil;
using augusta::session_test::FloorAt;
using augusta::session_test::HonestJoinRequest;
using augusta::session_test::kCharacter;
using augusta::session_test::Pointers;
using augusta::session_test::RawClient;
using augusta::session_test::TestHostConfig;
using augusta::session_test::TestSessionConfig;
using augusta::session_test::WithPlayerCount;
using augusta::session_test::WorldWithFloorAt;

// More misbehaviour than the server tolerates, with plenty to spare.
constexpr std::size_t kFlood = 3 * augusta::host::kMisbehaviourThreshold;

// The bit of a Debug subscribe's flags no stream uses yet.
constexpr std::uint8_t kUnusedBit = 1U << 7U;

// How many times needle occurs in haystack.
std::size_t CountOccurrences(std::string_view haystack, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = haystack.find(needle); at != std::string_view::npos; at = haystack.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// A server for a Match of two on a floor, with Shot judgements on or off.
class DebugSubscribeTest : public ::testing::Test {
 protected:
  static constexpr float kFloorY = 0.0F;

  explicit DebugSubscribeTest(bool shot_judgements = false) : host_(Config(shot_judgements), FloorScenario()) {}

  static HostConfig Config(bool shot_judgements) {
    HostConfig config = TestHostConfig(WithPlayerCount(2));
    config.shot_judgements = shot_judgements;
    return config;
  }

  static Scenario FloorScenario() {
    return Scenario{.collision = {FloorAt(kFloorY)},
                    .spawn_points = {Vec3(0.0F, kFloorY, 0.0F), Vec3(10.0F, kFloorY, 0.0F)},
                    .characters = {{.path = kCharacter, .hitboxes = {}}}};
  }

  // Connects a new client and runs the network until the server has admitted it.
  Session& Join() {
    sessions_.push_back(
        std::make_unique<Session>(TestSessionConfig(host_.ListenEndpoint()), WorldWithFloorAt(kFloorY)));
    Session& client = *sessions_.back();
    client.Connect();
    EXPECT_TRUE(
        ExchangeUntil(host_, Pointers(sessions_), [&] { return client.GetServerView()->accepted.has_value(); }));
    return client;
  }

  // Has client send count Debug subscribes of flags, then runs the network
  // until the server has received every Debug subscribe sent so far.
  void Subscribe(Session& client, std::uint8_t flags, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
      client.SubscribeDebug(flags);
    }
    sent_ += count;
    EXPECT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return Received() == sent_; }));
  }

  [[nodiscard]] std::size_t Received() const {
    return host_.Metrics().messages_received[MessageType::kDebugSubscribe].Value();
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
  std::size_t sent_ = 0;
};

class ShotJudgementsOnTest : public DebugSubscribeTest {
 protected:
  ShotJudgementsOnTest() : DebugSubscribeTest(true) {}
};

// However many a client sends and whenever it sends them, Debug subscribes are
// never refused and never count toward a misbehaving disconnect, so the order
// in which a developer opens an overlay and joins does not matter.
TEST_F(DebugSubscribeTest, DebugSubscribesBeforeJoiningNeverDisconnectNorKeepAPeerFromJoining) {
  RawClient early(host_.ListenEndpoint(), RawClient::Mode::kScripted);
  ASSERT_TRUE(early.Connect(host_));
  for (std::size_t i = 0; i < kFlood; ++i) {
    early.Send(DebugSubscribeWire{.flags = 0xFF});
  }
  ASSERT_TRUE(early.ServeUntil(host_, [&] { return Received() == kFlood; }));
  early.Send(HonestJoinRequest());

  ASSERT_TRUE(early.Join(host_));
  EXPECT_EQ(early.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(host_.Metrics().misbehaviour.Total(), 0U);
}

TEST_F(DebugSubscribeTest, DebugSubscribesInTheLobbyInAMatchAndBetweenMatchesNeverDisconnect) {
  Session& client = Join();
  Join();
  Subscribe(client, DebugSubscribe::kShotJudgements | kUnusedBit, kFlood);
  EXPECT_EQ(client.GetServerView()->GetPhase(), Phase::kLobby);
  ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_)));
  Subscribe(client, DebugSubscribe::kShotJudgements, kFlood);
  host_.EndMatch();
  ASSERT_TRUE(
      ExchangeUntil(host_, Pointers(sessions_), [&] { return client.GetServerView()->GetPhase() == Phase::kLobby; }));
  Subscribe(client, 0, kFlood);

  EXPECT_EQ(client.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(client.GetServerView()->GetPhase(), Phase::kLobby);
  EXPECT_EQ(host_.Metrics().misbehaviour.Total(), 0U);
}

// A bit the server does not serve is ignored with one INFO per peer and bit,
// the first time that peer sets it: Shot judgements' while its key is off, and
// a bit no stream uses.
TEST_F(DebugSubscribeTest, EachBitTheServerDoesNotServeIsLoggedOncePerPeer) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  Session& first = Join();
  Session& second = Join();

  testing::internal::CaptureStdout();
  Subscribe(first, DebugSubscribe::kShotJudgements, 3);
  Subscribe(first, 0, 1);
  Subscribe(first, DebugSubscribe::kShotJudgements | kUnusedBit, 3);
  Subscribe(second, DebugSubscribe::kShotJudgements | kUnusedBit, 3);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(CountOccurrences(log, "event=debug_stream_not_served"), 4U) << log;
  EXPECT_EQ(CountOccurrences(log, "INFO subsystem=serverruntime event=debug_stream_not_served peer="), 4U) << log;
  EXPECT_EQ(CountOccurrences(log, " stream=shot_judgements"), 2U) << log;
  EXPECT_EQ(CountOccurrences(log, " stream=bit7"), 2U) << log;
  EXPECT_EQ(host_.Metrics().misbehaviour.Total(), 0U);
}

// With `debug.shot_judgements` on, bit 0 is served: only a bit no stream uses
// is logged.
TEST_F(ShotJudgementsOnTest, AServedBitIsNotLoggedAndAnUnusedOneIs) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  Session& client = Join();

  testing::internal::CaptureStdout();
  Subscribe(client, DebugSubscribe::kShotJudgements | kUnusedBit, 2);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(CountOccurrences(log, "event=debug_stream_not_served"), 1U) << log;
  EXPECT_EQ(CountOccurrences(log, " stream=bit7"), 1U) << log;
}

// A peer's flags last from its connection to its disconnect: one that comes
// back is a new peer, told once more.
TEST_F(DebugSubscribeTest, APeerThatReconnectsIsLoggedAgain) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);

  testing::internal::CaptureStdout();
  Session& first = Join();
  Subscribe(first, DebugSubscribe::kShotJudgements, 1);
  first.Disconnect();
  ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] { return host_.Metrics().sessions.Value() == 0.0; }));
  Session& again = Join();
  Subscribe(again, DebugSubscribe::kShotJudgements, 1);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(CountOccurrences(log, " stream=shot_judgements"), 2U) << log;
}

TEST(DebugSubscribeStartupTest, AServerWithShotJudgementsOnSaysSoAtStartup) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  HostConfig on = TestHostConfig();
  on.shot_judgements = true;

  testing::internal::CaptureStdout();
  {
    const Host with(on, Scenario{.collision = {}, .spawn_points = {}, .characters = {}, .client_pack = {}});
  }
  {
    const Host without(TestHostConfig(),
                       Scenario{.collision = {}, .spawn_points = {}, .characters = {}, .client_pack = {}});
  }
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(CountOccurrences(log, "INFO subsystem=serverruntime event=shot_judgements_enabled"), 1U) << log;
}

}  // namespace
