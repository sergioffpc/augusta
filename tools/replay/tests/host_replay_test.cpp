#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "content.h"
#include "host.h"
#include "recording.h"
#include "replay.h"

// What a real server::Host recorded, with real clients connected over
// loopback, replays to the same outcome (ADR-0048): the recording holds every
// input the Host handed its World. The Host and its clients are driven by hand,
// as the runtime's session tests drive them (tests/session_test.cpp, whose
// helpers this keeps its own copy of).
namespace {

using augusta::command::Command;
using augusta::harness::Phase;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::math::Vec3;
using augusta::networking::Endpoint;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::server::Host;
using augusta::server::HostConfig;
using augusta::server::Scenario;

constexpr std::uint8_t kTickRate = 60;
constexpr float kFixedTick = 1.0F / kTickRate;
constexpr const char* kCharacter = "soldier";
constexpr int kMatchTicks = 30;
constexpr auto kNetworkWait = std::chrono::milliseconds(2);
constexpr auto kDeadline = std::chrono::seconds(15);

// Init and Shutdown once for the whole process.
class NetworkingEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkingEnvironment);

// A large horizontal slab at height 0, its triangles facing up.
CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// The floor and two spawn points on it, for a Match of two.
Scenario TwoPlayerFloor() {
  return Scenario{.collision = {Floor()},
                  .spawn_points = {Vec3(10.0F, 0.0F, 0.0F), Vec3(20.0F, 0.0F, 5.0F)},
                  .characters = {{.path = kCharacter, .hitboxes = {}}}};
}

Parameters TwoPlayers() {
  Parameters parameters;
  parameters.player_count = 2;
  return parameters;
}

// Unique to this process: ctest may run tests side by side.
std::filesystem::path RecordingPath() {
  return std::filesystem::temp_directory_path() /
         ("augusta_host_replay_" + std::to_string(std::random_device{}()) + ".rec");
}

// A Host on the floor, recording to path, and two clients of it, driven by hand.
class RecordedHostMatch {
 public:
  explicit RecordedHostMatch(const std::filesystem::path& path)
      : host_(HostConfig{.tick_rate_hz = kTickRate,
                         .parameters = TwoPlayers(),
                         .listen = Endpoint{.address = "127.0.0.1:0"},
                         .recording = path,
                         // A replay needs every tick: one lost fails the run.
                         .recording_mode = augusta::server::RecordingMode::kStrict,
                         .server_pack = {},
                         .capture_directory = {},
                         .capture_mode = {},
                         .faults = nullptr},
              TwoPlayerFloor(), {}) {
    for (int i = 0; i < 2; ++i) {
      augusta::prediction::World world;
      EXPECT_TRUE(world.AddCollisionMesh(Floor()).has_value());
      sessions_.push_back(std::make_unique<Session>(
          SessionConfig{.server = host_.ListenEndpoint(), .character = kCharacter}, std::move(world)));
      sessions_.back()->Connect();
    }
  }

  // Has every client report it has loaded each Roster it is sent, and ticks the
  // Host, until both are in a Match; returns whether they are.
  bool StartMatch() {
    std::map<const Session*, std::uint32_t> reported;
    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      Exchange();
      bool all_in_match = true;
      for (const auto& session : sessions_) {
        const auto lobby = session->GetLobby();
        if (lobby.has_value() && session->GetPhase() == Phase::kLobby && reported[session.get()] != lobby->version) {
          session->ReportReady(lobby->version);
          reported[session.get()] = lobby->version;
        }
        all_in_match = all_in_match && session->GetPhase() == Phase::kMatch;
      }
      if (all_in_match) {
        return true;
      }
      host_.Tick(kFixedTick);
      std::this_thread::sleep_for(kNetworkWait);
    }
    return false;
  }

  // Ticks of both clients walking forward and firing, then the Host ending
  // the Match and one more tick. Whichever commands reach the Host in time, the
  // recording holds what it ran on.
  void Play() {
    Command fire;
    fire.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
    fire.fire = true;
    for (int i = 0; i < kMatchTicks; ++i) {
      for (const auto& session : sessions_) {
        session->Tick(fire, kFixedTick);
      }
      Exchange();
      std::this_thread::sleep_for(kNetworkWait);
      Exchange();
      host_.Tick(kFixedTick);
    }
    host_.EndMatch();
    host_.Tick(kFixedTick);
  }

 private:
  void Exchange() {
    host_.PumpNetwork(std::chrono::steady_clock::now());
    for (const auto& session : sessions_) {
      session->PumpEvents();
      session->ExchangeMessages();
    }
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
};

// Requirements: US-21, NFR-09
TEST(HostReplayTest, WhatTheHostRecordedReplaysToTheSameOutcome) {
  const std::filesystem::path path = RecordingPath();
  {
    RecordedHostMatch match(path);
    ASSERT_TRUE(match.StartMatch());
    match.Play();
  }
  std::ifstream in(path, std::ios::binary);
  const auto recording = augusta::server::ReadRecording(in);
  in.close();
  std::filesystem::remove(path);
  ASSERT_TRUE(recording.has_value()) << augusta::server::DescribeRecordingError(recording.error());
  ASSERT_FALSE(recording->ticks.empty());

  const auto replayed = augusta::replay::Replay(
      *recording, augusta::server::Content{.scenario = TwoPlayerFloor(), .parameters = TwoPlayers(), .policy = {}},
      augusta::replay::kSameBuild);

  ASSERT_TRUE(replayed.has_value()) << "diverged on tick " << replayed.error().tick << ": "
                                    << augusta::replay::DescribeDivergenceKind(replayed.error().kind);
  EXPECT_EQ(*replayed, recording->ticks.size());
}

}  // namespace
