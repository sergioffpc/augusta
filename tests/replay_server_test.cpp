#include "replay_server.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"
#include "augusta/scripting.h"
#include "augusta/version.h"
#include "capture.h"
#include "content.h"
#include "frames.h"
#include "host.h"
#include "host_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "replay_catalog.h"
#include "wire.h"

// A replay server (ADR-0051) over loopback, driven by hand: what a Replay
// viewer, a Replay list request and a Join request get from it, and the whole
// path of a Match a live Host captured, re-run and checked against its
// capture (NFR-09). ReplayServerTest's viewers speak the protocol by hand;
// ReplayViewerTest's are the client's own, harness::Session and ReplayListQuery.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::ReplayServer;
using augusta::server::ReplayServerConfig;
using augusta::server::Scenario;
namespace protocol = augusta::protocol;

constexpr std::uint8_t kTickRate = 60;
constexpr float kFixedTick = 1.0F / kTickRate;
constexpr auto kPollInterval = std::chrono::milliseconds(2);
constexpr auto kPollDeadline = std::chrono::seconds(10);
constexpr const char* kLoopbackAnyPort = "127.0.0.1:0";
constexpr const char* kCharacter = "soldier";
constexpr float kEyeHeight = 1.6F;
constexpr float kSpacing = 10.0F;

// Last man standing, as the example scenario's rules decide it.
constexpr const char* kLastManStanding = R"(
  function on_tick(match)
    local alive = {}
    for _, player in ipairs(match.players) do
      if player.alive then alive[#alive + 1] = player end
    end
    if #alive > 1 then return nil end
    if #alive == 1 then return {winner = alive[1].session} end
    return {draw = true}
  end
)";

class NetworkEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kNetworkEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkEnvironment);

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

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// Two players 10 m apart down -Z on a floor, the first looking at the second
// with a view of yaw 0. A round to the head takes half of a player's health.
Scenario TwoInALine() {
  return Scenario{
      .collision = {Floor()},
      .spawn_points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, -kSpacing)},
      .characters =
          {{.path = kCharacter,
            .hitboxes = {BoxHitbox(augusta::assets::BodyPart::kHead, Vec3(-0.1F, 1.5F, -0.1F), Vec3(0.1F, 1.8F, 0.1F)),
                         BoxHitbox(augusta::assets::BodyPart::kTorso, Vec3(-0.2F, 0.9F, -0.1F), Vec3(0.2F, 1.5F, 0.1F)),
                         BoxHitbox(augusta::assets::BodyPart::kLimb, Vec3(-0.2F, 0.0F, -0.1F), Vec3(0.2F, 0.9F, 0.1F))},
            .eye = Vec3(0.0F, kEyeHeight, 0.0F)}},
      .client_pack = {}};
}

Parameters Rules() {
  Parameters parameters;
  parameters.player_count = 2;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.magazine_capacity = 30;
  parameters.rifle.muzzle_velocity = 800.0F;
  parameters.ammo.max_range = 200.0F;
  parameters.ammo.damage = {.head = 50.0F, .torso = 20.0F, .limb = 10.0F};
  parameters.starting_health = 100.0F;
  return parameters;
}

augusta::scripting::Engine Policy() {
  auto policy = augusta::scripting::Engine::Load(kLastManStanding);
  EXPECT_TRUE(policy.has_value());
  return policy ? *std::move(policy) : augusta::scripting::Engine{};
}

// A directory of the test's own, gone with it.
class TempDirectory {
 public:
  TempDirectory() {
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            (std::string("augusta_replay_server_") + test.name() + "_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() { std::filesystem::remove_all(path_); }
  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;
  TempDirectory(TempDirectory&&) = delete;
  TempDirectory& operator=(TempDirectory&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A client speaking the protocol by hand: it sends what a test asks and keeps
// every message it receives.
class RawClient {
 public:
  explicit RawClient(const Endpoint& server) { client_.Connect(server); }
  ~RawClient() { client_.Disconnect(); }
  RawClient(const RawClient&) = delete;
  RawClient& operator=(const RawClient&) = delete;
  RawClient(RawClient&&) = delete;
  RawClient& operator=(RawClient&&) = delete;

  void Serve() {
    client_.PumpEvents();
    if (!pending_.empty() && client_.GetState() == ConnectionState::kConnected) {
      for (const protocol::MessageWire& message : pending_) {
        ASSERT_TRUE(
            client_.Send(protocol::Encode(message).value(), augusta::networking::Reliability::kReliable).has_value());
      }
      pending_.clear();
    }
    for (const auto& payload : client_.ReceiveMessages().value()) {
      if (auto message = protocol::Decode(payload); message.has_value()) {
        received_.push_back(*std::move(message));
      }
    }
  }

  // Sends message once connected.
  void Send(const protocol::MessageWire& message) { pending_.push_back(message); }

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

  [[nodiscard]] const std::vector<protocol::MessageWire>& Received() const { return received_; }
  [[nodiscard]] ConnectionState State() const { return client_.GetState(); }
  [[nodiscard]] bool Refused() const { return !ReceivedOf<protocol::JoinRefusedWire>().empty(); }
  [[nodiscard]] std::optional<protocol::JoinRefusalWire> Refusal() const {
    const auto refused = ReceivedOf<protocol::JoinRefusedWire>();
    return refused.empty() ? std::nullopt : std::optional(refused.front().reason);
  }

 private:
  augusta::networking::Client client_;
  std::vector<protocol::MessageWire> pending_;
  std::vector<protocol::MessageWire> received_;
};

protocol::ReplayRequestWire RequestFor(const std::string& capture) {
  return protocol::ReplayRequestWire{
      .engine_version = std::string(augusta::EngineVersion()), .client_pack = {}, .capture = capture};
}

// Plays a Match of TwoInALine on a live Host capturing into directory: the
// first player fires at the second's head until the second dies and Game
// policy ends the Match. Returns the name of its capture.
std::string CaptureAKill(const std::filesystem::path& directory) {
  HostConfig config{.tick_rate_hz = kTickRate,
                    .parameters = Rules(),
                    .listen = Endpoint{.address = kLoopbackAnyPort},
                    .recording = {},
                    .recording_mode = {},
                    .server_pack = {},
                    .capture_directory = directory,
                    .faults = nullptr};
  Host host(config, TwoInALine(), Policy());
  std::vector<std::unique_ptr<augusta::harness::Session>> sessions;
  for (int i = 0; i < 2; ++i) {
    augusta::prediction::World world;
    EXPECT_TRUE(world.AddCollisionMesh(Floor()).has_value());
    sessions.push_back(std::make_unique<augusta::harness::Session>(
        augusta::harness::SessionConfig{.server = host.ListenEndpoint(), .character = kCharacter}, std::move(world)));
    sessions.back()->Connect();
  }
  const auto pump = [&] {
    host.PumpNetwork(std::chrono::steady_clock::now());
    for (const auto& session : sessions) {
      session->PumpEvents();
      session->ExchangeMessages();
    }
  };
  // Into the Match, each reporting Ready for every Roster it is sent.
  std::vector<std::uint32_t> reported(sessions.size(), 0);
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (std::chrono::steady_clock::now() < deadline && !std::ranges::all_of(sessions, [](const auto& s) {
           return s->GetPhase() == augusta::harness::Phase::kMatch;
         })) {
    pump();
    for (std::size_t i = 0; i < sessions.size(); ++i) {
      const auto lobby = sessions[i]->GetLobby();
      if (lobby.has_value() && reported[i] != lobby->version) {
        sessions[i]->ReportReady(lobby->version);
        reported[i] = lobby->version;
      }
    }
    host.Tick(kFixedTick);
    std::this_thread::sleep_for(kPollInterval);
  }
  // Whoever joined first spawned first, at the origin: it aims at the other's head.
  const auto spawned_first = [](const augusta::harness::Session& session) {
    for (const auto& player : session.GetMatchStart().value().players) {
      if (player.session == session.GetSessionId()) {
        return std::abs(player.spawn.z) < 0.5F;
      }
    }
    return false;
  };
  if (!spawned_first(*sessions[0])) {
    std::swap(sessions[0], sessions[1]);
  }
  const auto shooter = static_cast<augusta::server::SessionId>(std::to_underlying(*sessions[0]->GetSessionId()));
  Command fire;
  const Vec3 aim = Vec3(0.0F, 1.65F - kEyeHeight, -kSpacing);
  fire.yaw = 0.0F;
  fire.pitch = std::asin(aim.y / augusta::math::Length(aim));
  fire.fire = true;
  fire.ads = true;
  bool ended = false;
  for (int tick = 0; tick < 240 && !ended; ++tick) {
    sessions[0]->Tick(fire, kFixedTick);
    sessions[1]->Tick(Command{}, kFixedTick);
    const auto patience = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < patience && host.QueuedCommands(shooter) == 0) {
      pump();
      std::this_thread::sleep_for(kPollInterval);
    }
    ended = !host.Tick(kFixedTick).actions.empty();
    pump();
  }
  EXPECT_TRUE(ended);
  // The tick after takes the ended Match out, and its capture ends.
  host.Tick(kFixedTick);
  const std::vector<std::filesystem::path> files(std::filesystem::directory_iterator(directory), {});
  EXPECT_EQ(files.size(), 1U);
  return files.empty() ? std::string() : files.front().filename().string();
}

// The capture name names in directory, read once its writer has written it whole.
augusta::server::Capture ReadWhole(const std::filesystem::path& file) {
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (true) {
    std::ifstream in(file, std::ios::binary);
    auto capture = augusta::server::ReadCapture(in);
    if ((capture.has_value() && !capture->records.empty() &&
         std::holds_alternative<augusta::server::CapturedMatchEnd>(capture->records.back().event)) ||
        std::chrono::steady_clock::now() >= deadline) {
      EXPECT_TRUE(capture.has_value());
      return capture.value_or(augusta::server::Capture{});
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

// Writes capture to file, as a Capturer would have.
void Rewrite(const std::filesystem::path& file, const augusta::server::Capture& capture) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(protocol::kCaptureMagic.data()), protocol::kCaptureMagic.size());
  augusta::server::WriteFrame(out, augusta::server::kCaptureFrames,
                              augusta::server::EncodeToCapture(augusta::server::ToWire(capture.header)).value());
  for (const augusta::server::CaptureRecord& record : capture.records) {
    augusta::server::WriteFrame(out, augusta::server::kCaptureFrames,
                                augusta::server::EncodeToCapture(augusta::server::ToWire(record)).value());
  }
}

class ReplayServerTest : public ::testing::Test {
 protected:
  void SetUp() override { augusta::logging::Init(); }

  // A replay server of directory_, on the scenario a capture of CaptureAKill was made on.
  std::unique_ptr<ReplayServer> Serve(std::size_t max_viewers = 4) {
    return std::make_unique<ReplayServer>(ReplayServerConfig{.tick_rate_hz = kTickRate,
                                                             .parameters = Rules(),
                                                             .listen = Endpoint{.address = kLoopbackAnyPort},
                                                             .server_pack = {},
                                                             .captures = directory_.Path(),
                                                             .max_viewers = max_viewers,
                                                             .faults = nullptr},
                                          TwoInALine(), Policy);
  }

  // Runs server and clients, ticking server each round if tick, until until
  // holds or the deadline passes; returns whether it held.
  template <typename Condition>
  static bool RunUntil(ReplayServer& server, const std::vector<RawClient*>& clients, Condition until,
                       bool tick = false) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      server.PumpNetwork(std::chrono::steady_clock::now());
      for (RawClient* client : clients) {
        client->Serve();
      }
      if (until()) {
        return true;
      }
      if (tick) {
        server.Tick();
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  // Watches capture to its end on server; returns what the viewer received.
  static std::vector<protocol::MessageWire> Watch(ReplayServer& server, const std::string& capture) {
    RawClient viewer(server.ListenEndpoint());
    viewer.Send(RequestFor(capture));
    EXPECT_TRUE(RunUntil(
        server, {&viewer}, [&] { return viewer.State() == ConnectionState::kDisconnected || viewer.Refused(); }, true));
    return viewer.Received();
  }

  TempDirectory directory_;
};

template <typename T>
std::vector<T> Of(const std::vector<protocol::MessageWire>& messages) {
  std::vector<T> found;
  for (const auto& message : messages) {
    if (const auto* typed = std::get_if<T>(&message)) {
      found.push_back(*typed);
    }
  }
  return found;
}

// Requirements: US-21
TEST_F(ReplayServerTest, AJoinRequestIsRefusedAsAReplayServerWouldRefuseAnyRequestToPlay) {
  const std::unique_ptr<ReplayServer> server = Serve();
  RawClient player(server->ListenEndpoint());
  player.Send(protocol::JoinRequestWire{
      .engine_version = std::string(augusta::EngineVersion()), .client_pack = {}, .character = kCharacter});

  ASSERT_TRUE(RunUntil(*server, {&player}, [&] { return player.Refused(); }));

  EXPECT_EQ(player.Refusal(), protocol::JoinRefusalWire::kReplayServer);
  EXPECT_TRUE(player.ReceivedOf<protocol::JoinAcceptedWire>().empty());
  EXPECT_EQ(server->Viewers(), 0U);
}

// Requirements: US-21
TEST_F(ReplayServerTest, AReplayListRequestIsAnsweredWithEveryCaptureItReplaysThenClosed) {
  const std::string name = CaptureAKill(directory_.Path());
  const std::unique_ptr<ReplayServer> server = Serve();
  RawClient asker(server->ListenEndpoint());
  asker.Send(protocol::ReplayListRequestWire{});

  ASSERT_TRUE(RunUntil(*server, {&asker}, [&] {
    return !asker.ReceivedOf<protocol::ReplayListWire>().empty() && asker.State() == ConnectionState::kDisconnected;
  }));

  const std::vector<protocol::ReplayListWire> lists = asker.ReceivedOf<protocol::ReplayListWire>();
  ASSERT_EQ(lists.size(), 1U);
  ASSERT_EQ(lists[0].replays.size(), 1U);
  EXPECT_EQ(lists[0].replays[0].name, name);
  EXPECT_EQ(lists[0].replays[0].characters, (std::vector<std::string>{kCharacter, kCharacter}));
  EXPECT_EQ(lists[0].replays[0].tick_rate_hz, kTickRate);
  EXPECT_GT(lists[0].replays[0].ticks, 0U);
}

// A name is matched against the listing, never opened as a path (ADR-0051).
// Requirements: US-21
TEST_F(ReplayServerTest, AReplayRequestNamingAnythingNotInTheListingIsRefused) {
  const std::string name = CaptureAKill(directory_.Path());
  std::filesystem::create_directories(directory_.Path() / "inner");
  std::filesystem::copy_file(directory_.Path() / name, directory_.Path() / "inner" / name);
  const std::unique_ptr<ReplayServer> server = Serve();

  for (const std::string& unknown : {std::string("missing.capture"), "../" + name, "inner/" + name, "./" + name,
                                     "/tmp/" + name, std::string("/etc/passwd"), std::string("..")}) {
    RawClient viewer(server->ListenEndpoint());
    viewer.Send(RequestFor(unknown));
    ASSERT_TRUE(RunUntil(*server, {&viewer}, [&] { return viewer.Refused(); })) << unknown;
    EXPECT_EQ(viewer.Refusal(), protocol::JoinRefusalWire::kUnknownCapture) << unknown;
    EXPECT_TRUE(viewer.ReceivedOf<protocol::MatchStartWire>().empty()) << unknown;
  }
  EXPECT_EQ(server->Viewers(), 0U);
}

TEST_F(ReplayServerTest, AReplayRequestFromAnotherVersionOrPackIsRefusedForItBeforeItsCapture) {
  const std::unique_ptr<ReplayServer> server = Serve();
  RawClient old_version(server->ListenEndpoint());
  RawClient other_pack(server->ListenEndpoint());
  old_version.Send(protocol::ReplayRequestWire{.engine_version = "0.0.0", .client_pack = {}, .capture = "x"});
  protocol::ReplayRequestWire request = RequestFor("x");
  request.client_pack.fill(std::byte{9});
  other_pack.Send(request);

  ASSERT_TRUE(
      RunUntil(*server, {&old_version, &other_pack}, [&] { return old_version.Refused() && other_pack.Refused(); }));

  EXPECT_EQ(old_version.Refusal(), protocol::JoinRefusalWire::kVersionMismatch);
  EXPECT_EQ(other_pack.Refusal(), protocol::JoinRefusalWire::kPackMismatch);
}

// Requirements: US-21
TEST_F(ReplayServerTest, AViewerIsSentAMatchsStartStatesViewsAndEndThenClosed) {
  const std::string name = CaptureAKill(directory_.Path());
  const augusta::server::Capture capture = ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve();

  const std::vector<protocol::MessageWire> received = Watch(*server, name);

  ASSERT_FALSE(received.empty());
  const auto* accepted = std::get_if<protocol::JoinAcceptedWire>(&received.front());
  ASSERT_NE(accepted, nullptr);
  EXPECT_EQ(accepted->session, protocol::SessionIdWire{0});
  EXPECT_EQ(accepted->tick_rate_hz, kTickRate);
  EXPECT_EQ(accepted->parameters.player_count, 2);
  const std::vector<protocol::MatchStartWire> starts = Of<protocol::MatchStartWire>(received);
  ASSERT_EQ(starts.size(), 1U);
  ASSERT_EQ(starts[0].players.size(), 2U);
  EXPECT_EQ(starts[0].first_tick, 1U);
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& join = std::get<augusta::server::CapturedJoin>(capture.records[i].event);
    EXPECT_EQ(starts[0].players[i].session, augusta::server::ToWire(join.session));
    EXPECT_EQ(starts[0].players[i].entity, static_cast<protocol::EntityIdWire>(i + 1));
    EXPECT_EQ(starts[0].players[i].spawn, join.spawn);
  }
  EXPECT_FALSE(Of<protocol::AuthoritativeStateWire>(received).empty());
  for (const protocol::AuthoritativeStateWire& state : Of<protocol::AuthoritativeStateWire>(received)) {
    EXPECT_EQ(state.health, 0.0F);
    EXPECT_EQ(state.acknowledged_sequence, 0U);
  }
  // The shooter's pitch and ADS, which no Authoritative State carries.
  const std::vector<protocol::ReplayViewWire> views = Of<protocol::ReplayViewWire>(received);
  ASSERT_FALSE(views.empty());
  const auto shooter =
      std::ranges::find(views.back().players, protocol::EntityIdWire{1}, &protocol::PlayerViewWire::entity);
  ASSERT_NE(shooter, views.back().players.end());
  EXPECT_GT(shooter->pitch, 0.0F);
  EXPECT_EQ(shooter->flags, protocol::PlayerViewWire::kAds);
  EXPECT_FALSE(Of<protocol::ShotWire>(received).empty());
  EXPECT_TRUE(Of<protocol::HitConfirmationWire>(received).empty());
  const std::vector<protocol::DeathWire> deaths = Of<protocol::DeathWire>(received);
  ASSERT_EQ(deaths.size(), 1U);
  EXPECT_EQ(deaths[0].victim, protocol::EntityIdWire{2});
  EXPECT_EQ(deaths[0].killer, protocol::EntityIdWire{1});
  ASSERT_TRUE(std::holds_alternative<protocol::MatchEndWire>(received.back()));
  EXPECT_EQ(std::get<protocol::MatchEndWire>(received.back()).winner, starts[0].players[0].session);
  EXPECT_EQ(server->Viewers(), 0U);
}

// On the build that made it, a capture's Replay resolves every Death and the
// Match end as captured, and logs no difference.
// Requirements: US-21, NFR-09
TEST_F(ReplayServerTest, OnTheCapturingBuildAReplayLogsNoDifference) {
  const std::string name = CaptureAKill(directory_.Path());
  ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve();

  testing::internal::CaptureStdout();
  const std::vector<protocol::MessageWire> received = Watch(*server, name);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_EQ(Of<protocol::DeathWire>(received).size(), 1U);
  EXPECT_NE(log.find("event=replay_ended"), std::string::npos) << log;
  EXPECT_EQ(log.find("event=replay_diverged"), std::string::npos) << log;
}

// A doctored capture's Replay logs the first Death or Match end that differs.
// Requirements: US-21, NFR-09
TEST_F(ReplayServerTest, ADoctoredCapturesReplayLogsTheFirstDifference) {
  const std::string name = CaptureAKill(directory_.Path());
  augusta::server::Capture capture = ReadWhole(directory_.Path() / name);
  std::uint32_t death_offset = 0;
  for (augusta::server::CaptureRecord& record : capture.records) {
    if (auto* death = std::get_if<augusta::server::CapturedDeath>(&record.event)) {
      death->killer = death->victim;
      death_offset = record.offset;
    }
  }
  ASSERT_GT(death_offset, 0U);
  Rewrite(directory_.Path() / name, capture);
  const std::unique_ptr<ReplayServer> server = Serve();

  testing::internal::CaptureStdout();
  Watch(*server, name);
  const std::string log = testing::internal::GetCapturedStdout();

  const std::string expected = "event=replay_diverged peer=";
  ASSERT_NE(log.find(expected), std::string::npos) << log;
  EXPECT_NE(log.find("marker=death offset=" + std::to_string(death_offset) + " captured=[2<-2] resolved=[2<-1]"),
            std::string::npos)
      << log;
  // Only the first difference is logged.
  EXPECT_EQ(log.find(expected), log.rfind(expected)) << log;
}

// Each viewer gets a Replay of its own, from the first tick, and one past
// replay.max_viewers is refused as a full Lobby is.
// Requirements: US-21
TEST_F(ReplayServerTest, TwoViewersEachWatchFromTheStartAndOnePastTheMostIsRefused) {
  const std::string name = CaptureAKill(directory_.Path());
  ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve(2);
  RawClient first(server->ListenEndpoint());
  first.Send(RequestFor(name));
  ASSERT_TRUE(RunUntil(*server, {&first}, [&] { return !first.ReceivedOf<protocol::MatchStartWire>().empty(); }));
  // The first is some ticks in when the second asks.
  for (int i = 0; i < 3; ++i) {
    server->Tick();
  }
  RawClient second(server->ListenEndpoint());
  second.Send(RequestFor(name));
  ASSERT_TRUE(
      RunUntil(*server, {&first, &second}, [&] { return !second.ReceivedOf<protocol::MatchStartWire>().empty(); }));
  RawClient third(server->ListenEndpoint());
  third.Send(RequestFor(name));
  ASSERT_TRUE(RunUntil(*server, {&first, &second, &third}, [&] { return third.Refused(); }));
  EXPECT_EQ(third.Refusal(), protocol::JoinRefusalWire::kLobbyFull);
  EXPECT_EQ(server->Viewers(), 2U);

  ASSERT_TRUE(RunUntil(
      *server, {&first, &second},
      [&] {
        return first.State() == ConnectionState::kDisconnected && second.State() == ConnectionState::kDisconnected;
      },
      true));

  // Each saw the Match from its first tick to its end.
  for (const RawClient* viewer : {&first, &second}) {
    const auto views = viewer->ReceivedOf<protocol::ReplayViewWire>();
    ASSERT_FALSE(views.empty());
    const auto first_tick = std::ranges::min(views, {}, &protocol::ReplayViewWire::tick).tick;
    EXPECT_LE(first_tick, 2U);
    EXPECT_EQ(viewer->ReceivedOf<protocol::DeathWire>().size(), 1U);
    EXPECT_EQ(viewer->ReceivedOf<protocol::MatchEndWire>().size(), 1U);
  }
  EXPECT_EQ(server->Viewers(), 0U);
}

// The client's side of a Replay (ADR-0051): a harness::Session that watches.
class ReplayViewerTest : public ReplayServerTest {
 protected:
  std::unique_ptr<augusta::harness::Session> Viewer(const ReplayServer& server, const std::string& capture) {
    auto viewer = std::make_unique<augusta::harness::Session>(
        augusta::harness::SessionConfig{.server = server.ListenEndpoint(), .character = {}, .replay = capture},
        augusta::prediction::World());
    viewer->Connect();
    return viewer;
  }

  // Runs server, ticking it if tick, and viewer until until holds or the deadline passes.
  template <typename Condition>
  static bool RunUntil(ReplayServer& server, augusta::harness::Session& viewer, Condition until, bool tick = false) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      server.PumpNetwork(std::chrono::steady_clock::now());
      viewer.PumpEvents();
      viewer.ExchangeMessages();
      if (until()) {
        return true;
      }
      if (tick) {
        server.Tick();
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }
};

// A viewer is a Spectator from the first tick: in the Match, with no body of
// its own, shown every player's pitch and ADS, until the Match end, after
// which its connection's end is no failure.
// Requirements: US-21
TEST_F(ReplayViewerTest, AViewerWatchesACaptureFromItsFirstTickToItsEndWithEveryPlayersView) {
  const std::string name = CaptureAKill(directory_.Path());
  ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve();
  const std::unique_ptr<augusta::harness::Session> viewer = Viewer(*server, name);

  // Until the shooter has aimed: before its first Command, it idles.
  ASSERT_TRUE(RunUntil(
      *server, *viewer,
      [&] {
        const auto& view = viewer->GetServerView()->replay_view;
        return view.has_value() && !view->players.empty() && view->players[0].ads;
      },
      true));
  const std::shared_ptr<const augusta::harness::ServerView> watching = viewer->GetServerView();
  EXPECT_EQ(watching->GetPhase(), augusta::harness::Phase::kMatch);
  EXPECT_FALSE(watching->OwnEntity().has_value());
  EXPECT_EQ(watching->match_start->players.size(), 2U);
  EXPECT_EQ(watching->match_start->first_tick, 1U);
  ASSERT_EQ(watching->replay_view->players.size(), 2U);
  EXPECT_EQ(watching->replay_view->players[0].entity, augusta::harness::EntityId{1});
  EXPECT_GT(watching->replay_view->players[0].pitch, 0.0F);
  EXPECT_TRUE(watching->replay_view->players[0].ads);
  EXPECT_FALSE(watching->replay_view->players[1].ads);
  // It predicts nothing and sends nothing, however it is ticked.
  EXPECT_EQ(viewer->Tick(Command{}, kFixedTick).local_body.position, Vec3{});

  std::vector<augusta::harness::Death> deaths;
  ASSERT_TRUE(RunUntil(
      *server, *viewer,
      [&] {
        const auto taken = viewer->TakeDeaths();
        deaths.insert(deaths.end(), taken.begin(), taken.end());
        return viewer->ReplayEnded();
      },
      true));

  ASSERT_EQ(deaths.size(), 1U);
  EXPECT_EQ(deaths[0].victim, augusta::harness::EntityId{2});
  ASSERT_TRUE(viewer->GetMatchEnd().has_value());
  EXPECT_EQ(viewer->GetMatchEnd()->winner, watching->match_start->players[0].session);
  EXPECT_FALSE(viewer->GetFailure().has_value());
}

// Requirements: US-21
TEST_F(ReplayViewerTest, AViewerOfACaptureTheServerDoesNotReplayIsRefusedForIt) {
  const std::unique_ptr<ReplayServer> server = Serve();
  const std::unique_ptr<augusta::harness::Session> viewer = Viewer(*server, "../elsewhere.capture");

  ASSERT_TRUE(RunUntil(*server, *viewer, [&] { return viewer->GetFailure().has_value(); }));

  EXPECT_EQ(viewer->GetFailure()->kind, augusta::harness::FailureKind::kRefused);
  EXPECT_EQ(viewer->GetFailure()->refusal, augusta::harness::JoinRefusal::kUnknownCapture);
  EXPECT_FALSE(viewer->ReplayEnded());
}

// A player's client on a replay server is told what it is.
// Requirements: US-21
TEST_F(ReplayViewerTest, APlayersClientIsRefusedAsAReplayServerRefusesAnyRequestToPlay) {
  const std::unique_ptr<ReplayServer> server = Serve();
  augusta::harness::Session player(
      augusta::harness::SessionConfig{.server = server->ListenEndpoint(), .character = kCharacter},
      augusta::prediction::World());
  player.Connect();

  ASSERT_TRUE(RunUntil(*server, player, [&] { return player.GetFailure().has_value(); }));

  EXPECT_EQ(player.GetFailure()->refusal, augusta::harness::JoinRefusal::kReplayServer);
}

// augustac --replays: the server's list, oldest first.
// Requirements: US-21
TEST_F(ReplayViewerTest, AReplayListQueryGetsTheServersList) {
  const std::string name = CaptureAKill(directory_.Path());
  ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve();
  augusta::harness::ReplayListQuery query(server->ListenEndpoint());

  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (!query.List().has_value() && !query.GetFailure().has_value() && std::chrono::steady_clock::now() < deadline) {
    server->PumpNetwork(std::chrono::steady_clock::now());
    query.Pump();
    std::this_thread::sleep_for(kPollInterval);
  }

  ASSERT_TRUE(query.List().has_value());
  ASSERT_EQ(query.List()->size(), 1U);
  EXPECT_EQ(query.List()->front().name, name);
  EXPECT_EQ(query.List()->front().characters, (std::vector<std::string>{kCharacter, kCharacter}));
  EXPECT_FALSE(query.GetFailure().has_value());
}

TEST(ReplayListQueryTest, AServerThatIsNotThereIsUnreachable) {
  Endpoint gone;
  {
    const augusta::networking::Server server(Endpoint{.address = kLoopbackAnyPort});
    gone = server.LocalEndpoint();
  }
  augusta::networking::SimulateNetworkConditions({.timeout_ms = 500});
  augusta::harness::ReplayListQuery query(gone);

  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (!query.GetFailure().has_value() && std::chrono::steady_clock::now() < deadline) {
    query.Pump();
    std::this_thread::sleep_for(kPollInterval);
  }
  augusta::networking::SimulateNetworkConditions({});

  ASSERT_TRUE(query.GetFailure().has_value());
  EXPECT_EQ(query.GetFailure()->kind, augusta::harness::FailureKind::kServerUnreachable);
}

TEST(DescribeReplayListTest, ItNamesEachCaptureWithItsStartLengthAndCharacters) {
  const std::vector<augusta::harness::ReplayListing> listings = {
      {.name = "20261009T101500123Z-0001.capture",
       .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds{1'791'540'900'123}),
       .ticks = 7500,
       .tick_rate_hz = 60,
       .characters = {"soldier", "sniper"}},
      {.name = "b.capture",
       .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds{1'791'540'960'000}),
       .ticks = 30,
       .tick_rate_hz = 60,
       .characters = {"soldier"}}};

  EXPECT_EQ(augusta::harness::DescribeReplayList(listings),
            "20261009T101500123Z-0001.capture  started 2026-10-09 10:15:00 UTC  lasted 2:05  soldier, sniper\n"
            "b.capture  started 2026-10-09 10:16:00 UTC  lasted 0:00  soldier");
  EXPECT_EQ(augusta::harness::DescribeReplayList({}), "the server replays no capture");
}

// A viewer has asked for its Replay: a list request or a Join request from
// it later is dropped and judged, never answered, and its Replay goes on to
// its end.
// Requirements: US-21
TEST_F(ReplayServerTest, AViewersLaterRequestsAreDroppedAndItsReplayGoesOn) {
  const std::string name = CaptureAKill(directory_.Path());
  ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve();
  RawClient viewer(server->ListenEndpoint());
  viewer.Send(RequestFor(name));
  ASSERT_TRUE(RunUntil(*server, {&viewer}, [&] { return !viewer.ReceivedOf<protocol::MatchStartWire>().empty(); }));
  server->Tick();

  viewer.Send(protocol::ReplayListRequestWire{});
  viewer.Send(protocol::JoinRequestWire{
      .engine_version = std::string(augusta::EngineVersion()), .client_pack = {}, .character = kCharacter});
  viewer.Send(RequestFor(name));
  ASSERT_TRUE(RunUntil(*server, {&viewer}, [&] {
    return server->Metrics().messages_received[augusta::server::MessageType::kReplayRequest].Value() == 2;
  }));
  EXPECT_EQ(server->Viewers(), 1U);

  ASSERT_TRUE(RunUntil(*server, {&viewer}, [&] { return viewer.State() == ConnectionState::kDisconnected; }, true));
  EXPECT_TRUE(viewer.ReceivedOf<protocol::ReplayListWire>().empty());
  EXPECT_TRUE(viewer.ReceivedOf<protocol::JoinRefusedWire>().empty());
  EXPECT_EQ(viewer.ReceivedOf<protocol::MatchEndWire>().size(), 1U);
  EXPECT_EQ(server->Metrics().misbehaviour[augusta::server::PeerRejection::kNotAClientMessage].Value(), 3U);
}

// Each Replay's tick is timed into the server's metrics, so what every
// viewer costs the Simulation thread's tick budget shows (ADR-0051).
// Requirements: US-21
TEST_F(ReplayServerTest, EachReplaysTickIsTimedIntoTheMetrics) {
  const std::string name = CaptureAKill(directory_.Path());
  ReadWhole(directory_.Path() / name);
  const std::unique_ptr<ReplayServer> server = Serve();
  RawClient first(server->ListenEndpoint());
  RawClient second(server->ListenEndpoint());
  first.Send(RequestFor(name));
  second.Send(RequestFor(name));
  ASSERT_TRUE(RunUntil(*server, {&first, &second}, [&] { return server->Viewers() == 2; }));
  EXPECT_EQ(server->Metrics().replays.Value(), 2.0);

  constexpr int kTicks = 3;
  for (int i = 0; i < kTicks; ++i) {
    server->Tick();
  }

  const auto timed = server->Metrics().replay_tick_duration.Read();
  EXPECT_EQ(timed.cumulative_counts.back(), 2U * kTicks);
  EXPECT_GT(timed.sum, 0.0);
}

// augustac --replays or --replay against a live server is told it is none,
// rather than waiting for an answer that never comes.
// Requirements: US-21
TEST(LiveServerReplayTest, ALiveServerRefusesReplayRequestsAsNoReplayServer) {
  Host host(HostConfig{.tick_rate_hz = kTickRate,
                       .parameters = Rules(),
                       .listen = Endpoint{.address = kLoopbackAnyPort},
                       .recording = {},
                       .recording_mode = {},
                       .server_pack = {},
                       .capture_directory = {},
                       .faults = nullptr},
            TwoInALine());
  augusta::harness::ReplayListQuery query(host.ListenEndpoint());
  RawClient viewer(host.ListenEndpoint());
  viewer.Send(RequestFor("a.capture"));

  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while ((!query.GetFailure().has_value() || !viewer.Refused()) && std::chrono::steady_clock::now() < deadline) {
    host.PumpNetwork(std::chrono::steady_clock::now());
    query.Pump();
    viewer.Serve();
    std::this_thread::sleep_for(kPollInterval);
  }

  ASSERT_TRUE(query.GetFailure().has_value());
  EXPECT_EQ(query.GetFailure()->kind, augusta::harness::FailureKind::kRefused);
  EXPECT_EQ(query.GetFailure()->refusal, augusta::harness::JoinRefusal::kNotAReplayServer);
  EXPECT_EQ(viewer.Refusal(), protocol::JoinRefusalWire::kNotAReplayServer);
}

TEST(ReplayServerConfigTest, AReplayServerRefusesACapturesDirectoryThatIsNone) {
  EXPECT_THROW(ReplayServer(ReplayServerConfig{.tick_rate_hz = kTickRate,
                                               .parameters = Rules(),
                                               .listen = Endpoint{.address = kLoopbackAnyPort},
                                               .server_pack = {},
                                               .captures = std::filesystem::temp_directory_path() /
                                                           "augusta_replay_server_no_such_directory",
                                               .max_viewers = 1,
                                               .faults = nullptr},
                            TwoInALine(), Policy),
               std::runtime_error);
}

// A list of more captures than a Replay list holds names the newest.
TEST(ListedOfTest, ItNamesTheNewestCapturesAReplayRequestCanName) {
  std::vector<augusta::server::ReplayListing> listings;
  for (std::size_t i = 0; i < protocol::kMaxReplayListings + 2; ++i) {
    listings.push_back(augusta::server::ReplayListing{
        .name = std::to_string(1000 + i), .started = {}, .ticks = 1, .tick_rate_hz = kTickRate, .characters = {}});
  }
  listings.push_back(augusta::server::ReplayListing{.name = std::string(protocol::kMaxCaptureNameLength + 1, 'z'),
                                                    .started = {},
                                                    .ticks = 1,
                                                    .tick_rate_hz = kTickRate,
                                                    .characters = {}});

  const std::vector<augusta::server::ReplayListing> listed = augusta::server::ListedOf(listings);

  ASSERT_EQ(listed.size(), protocol::kMaxReplayListings);
  EXPECT_EQ(listed.front().name, "1002");
  EXPECT_EQ(listed.back().name, std::to_string(1000 + protocol::kMaxReplayListings + 1));
  // Whatever it names, the list carries.
  EXPECT_TRUE(protocol::Encode(augusta::server::ToWire(listed)).has_value());
}

}  // namespace
