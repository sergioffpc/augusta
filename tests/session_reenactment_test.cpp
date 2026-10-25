#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "augusta/animation.h"
#include "augusta/capture_records.h"
#include "augusta/command.h"
#include "augusta/content.h"
#include "augusta/grid.h"
#include "augusta/harness.h"
#include "augusta/host.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/pose.h"
#include "augusta/reenactment.h"
#include "augusta/tick.h"
#include "clips_fixture.h"
#include "session_test.h"

// The reenactments debug tool end to end (ADR-0050), on a real server host and
// real client sessions over loopback. Built only into a Development build's
// Session tests (ADR-0074), whose session_test.cpp starts the Link.
namespace {

using augusta::command::Command;
using augusta::harness::Phase;
using augusta::harness::Progress;
using augusta::harness::ReadScript;
using augusta::harness::Reenactment;
using augusta::harness::Script;
using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::host::Host;
using augusta::host::HostConfig;
using augusta::link::ConnectionState;
using augusta::math::Vec3;
using augusta::scenario::Scenario;
using augusta::session_test::AdmittedSession;
using augusta::session_test::DriveIntoMatch;
using augusta::session_test::ExchangeUntil;
using augusta::session_test::FloorAt;
using augusta::session_test::kCharacter;
using augusta::session_test::kFixedTick;
using augusta::session_test::Pointers;
using augusta::session_test::Settle;
using augusta::session_test::TestHostConfig;
using augusta::session_test::TestSessionConfig;
using augusta::session_test::WithPlayerCount;
using augusta::session_test::WorldWithFloorAt;

// What a client was told of its own body on one tick: where its view looked,
// whether it aimed down sights, and the pose its Character's Clips give the
// body from that state, each Joint relative to its parent.
struct Seen {
  float pitch = 0.0F;
  bool ads = false;
  std::vector<augusta::animation::JointPose> pose;
};

// ADR-0050 end to end: a playtest of two players captured on one server, then
// both reenacted at once, joining in the other order, against another server
// with reenactments on and other Spawn points, whose own capture of the run is
// compared with the playtest's. Both play the Holder (clips_fixture.h), whose
// Aim offset and ADS grip pose it by its view.
class ReenactmentHostTest : public ::testing::Test {
 protected:
  static constexpr float kFloorY = 0.0F;
  static constexpr int kPlaytestSteps = 40;
  // The second player disconnects after this many steps of the playtest.
  static constexpr int kLeavingStep = 25;
  // How far before the newest State's tick each playtest Command says it was seen.
  static constexpr augusta::tick::Tick kSeenDelay = 2;
  // How many ticks off its captured one a reenacted Command may reach the World (ADR-0050).
  static constexpr std::int64_t kTolerance = 2;

  ReenactmentHostTest() : playtest_(Directory("playtest")), reenacted_(Directory("reenacted")) {}
  ~ReenactmentHostTest() override {
    std::filesystem::remove_all(playtest_);
    std::filesystem::remove_all(reenacted_);
  }

  ReenactmentHostTest(const ReenactmentHostTest&) = delete;
  ReenactmentHostTest& operator=(const ReenactmentHostTest&) = delete;
  ReenactmentHostTest(ReenactmentHostTest&&) = delete;
  ReenactmentHostTest& operator=(ReenactmentHostTest&&) = delete;

  // Unique to this test and run: ctest may run tests side by side.
  static std::filesystem::path Directory(const std::string& run) {
    return std::filesystem::temp_directory_path() /
           ("augusta_session_reenactment_" + run + "_" + std::to_string(std::random_device{}()));
  }

  // A server of two players on the floor with spawn_points, capturing into captures.
  static std::unique_ptr<Host> Server(const std::filesystem::path& captures, std::vector<Vec3> spawn_points,
                                      bool reenactments) {
    HostConfig config = TestHostConfig(WithPlayerCount(2));
    config.capture_directory = captures;
    config.reenactments = reenactments;
    return std::make_unique<Host>(
        config,
        Scenario{.collision = {FloorAt(kFloorY)}, .spawn_points = std::move(spawn_points), .characters = {Holder()}});
  }

  static augusta::scenario::Character Holder() {
    return augusta::scenario::Character{.path = kCharacter,
                                        .hitboxes = {},
                                        .skeleton = augusta::clips_fixture::HolderSkeleton(),
                                        .eye = {},
                                        .clips = augusta::clips_fixture::HolderClips()};
  }

  // One step of the whole Match: every client of sessions still connected
  // ticks command_of(its index), the host ticks once each one's Command has
  // reached it, and the State comes back.
  template <typename CommandOf>
  static void Step(Host& host, const std::vector<std::unique_ptr<Session>>& sessions, CommandOf command_of) {
    std::vector<augusta::host::SessionId> sending;
    for (std::size_t i = 0; i < sessions.size(); ++i) {
      Session& session = *sessions[i];
      if (session.GetServerView()->GetPhase() != Phase::kMatch ||
          session.GetConnectionState() != ConnectionState::kConnected) {
        continue;
      }
      // A Captured player with enough Commands already waiting skips the tick.
      const std::optional<Command> command = command_of(i);
      if (!command.has_value()) {
        continue;
      }
      sending.push_back(session.GetServerView()->accepted->session);
      session.Tick(*command, kFixedTick);
    }
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    ExchangeUntil(host, Pointers(sessions), [&] {
      return std::chrono::steady_clock::now() >= give_up ||
             std::ranges::all_of(sending, [&](augusta::host::SessionId id) { return host.QueuedCommands(id) > 0; });
    });
    host.Tick(kFixedTick);
    Settle(host, Pointers(sessions));
  }

  // The playtest's Command of player (from 0) on step: each a view of its own,
  // so each is told apart in a capture, looking a little higher each step and
  // in ADS for every other three, seen kSeenDelay before the newest State.
  static Command PlaytestCommand(const Session& session, std::size_t player, int step) {
    Command command;
    command.movement.direction = Vec3(player == 0 ? 1.0F : -1.0F, 0.0F, 0.0F);
    command.yaw = augusta::math::SnapAngle(0.02F * static_cast<float>(step) + (player == 0 ? 0.0F : 1.0F));
    command.pitch = augusta::math::SnapAngle(-0.6F + (0.03F * static_cast<float>(step)) + (player == 0 ? 0.0F : 0.01F));
    command.ads = (step / 3) % 2 == 1;
    const auto state = session.GetServerView()->authoritative;
    const augusta::tick::Tick newest =
        state.has_value() ? state->tick : session.GetServerView()->match_start->first_tick;
    command.seen_tick = newest - kSeenDelay;
    return command;
  }

  // Records, into seen by where it spawned, what each client of sessions in a
  // Match was told of its own body on the newest State it has.
  void Record(const std::vector<std::unique_ptr<Session>>& sessions, std::map<float, std::vector<Seen>>& seen) {
    for (const auto& session : sessions) {
      const auto& view = *session->GetServerView();
      if (!view.match_start.has_value() || !view.authoritative.has_value() ||
          session->GetConnectionState() != ConnectionState::kConnected) {
        continue;
      }
      for (const auto& player : view.match_start->players) {
        if (player.session != AdmittedSession(view)) {
          continue;
        }
        for (const augusta::link::EntityBody& body : view.authoritative->bodies) {
          if (body.entity != player.entity) {
            continue;
          }
          augusta::animation::SkeletonPose posed;
          poser_.Pose(*holder_.clips, holder_.skeleton,
                      {.body = body.body, .yaw = body.yaw, .pitch = body.pitch, .ads = body.ads}, posed);
          seen[player.spawn.x].push_back(Seen{.pitch = body.pitch, .ads = body.ads, .pose = posed.local});
        }
      }
    }
  }

  // seen with each view held over several ticks once: a Reenactment's Commands
  // reach the World within kTolerance ticks of their own, so a view may be held
  // a tick longer or shorter than the playtest held it.
  static std::vector<Seen> Views(const std::vector<Seen>& seen) {
    std::vector<Seen> views;
    for (const Seen& each : seen) {
      if (views.empty() || views.back().pitch != each.pitch || views.back().ads != each.ads) {
        views.push_back(each);
      }
    }
    return views;
  }

  // The one capture in directory, once its host is gone, as player's Script.
  static Script ReadPlayer(const std::filesystem::path& directory, augusta::capture::Player player) {
    const std::vector<std::filesystem::path> files(std::filesystem::directory_iterator(directory), {});
    EXPECT_EQ(files.size(), 1U);
    std::ifstream in(files.empty() ? std::filesystem::path{} : files.front(), std::ios::binary);
    auto script = ReadScript(in, player);
    EXPECT_TRUE(script.has_value());
    return script.value_or(Script{});
  }

  // Plays the playtest: two players walk apart, each with a view of its own
  // every step, the second leaving at kLeavingStep, until the host ends it.
  void Playtest() {
    const std::unique_ptr<Host> host = Server(playtest_, {Vec3(10, kFloorY, 0), Vec3(20, kFloorY, 5)}, false);
    std::vector<std::unique_ptr<Session>> sessions;
    for (int i = 0; i < 2; ++i) {
      sessions.push_back(
          std::make_unique<Session>(TestSessionConfig(host->ListenEndpoint()), WorldWithFloorAt(kFloorY)));
      sessions.back()->Connect();
    }
    ASSERT_TRUE(DriveIntoMatch(*host, Pointers(sessions)));
    for (int step = 0; step < kPlaytestSteps; ++step) {
      if (step == kLeavingStep) {
        sessions[1]->Disconnect();
      }
      Step(*host, sessions, [&](std::size_t i) { return PlaytestCommand(*sessions[i], i, step); });
      Record(sessions, played_);
    }
    host->EndMatch();
    host->Tick(kFixedTick);
  }

  // Reenacts both players of the playtest at once, the second joining first,
  // until each has left or reached the capture's Match end; returns where
  // the run's Match start put each player of the playtest, by its number.
  std::vector<Vec3> Reenact() {
    const std::unique_ptr<Host> host = Server(reenacted_, {Vec3(-30, kFloorY, 0), Vec3(-40, kFloorY, 5)}, true);
    std::vector<std::unique_ptr<Session>> sessions;
    std::vector<std::unique_ptr<Reenactment>> runs;
    for (const augusta::capture::Player player : {augusta::capture::Player{2}, augusta::capture::Player{1}}) {
      Script script = ReadPlayer(playtest_, player);
      sessions.push_back(std::make_unique<Session>(SessionConfig{.server = {.address = host->ListenEndpoint().address},
                                                                 .character = script.character,
                                                                 .spawn = script.spawn},
                                                   WorldWithFloorAt(kFloorY)));
      sessions.back()->Connect();
      runs.push_back(std::make_unique<Reenactment>(std::move(script)));
    }
    EXPECT_TRUE(DriveIntoMatch(*host, Pointers(sessions)));
    std::vector<Vec3> spawned;
    for (const auto& session : sessions) {
      for (const auto& player : session->GetServerView()->match_start.value().players) {
        if (player.session == AdmittedSession(*session->GetServerView())) {
          spawned.insert(spawned.begin(), player.spawn);
        }
      }
    }
    std::vector<bool> done(runs.size(), false);
    for (int step = 0; step < kPlaytestSteps * 2 && !std::ranges::all_of(done, std::identity{}); ++step) {
      for (std::size_t i = 0; i < runs.size(); ++i) {
        const Progress progress = runs[i]->Check(*sessions[i]->GetServerView(), 0);
        if (progress == Progress::kLeave && !done[i]) {
          sessions[i]->Disconnect();
        }
        done[i] = done[i] || progress != Progress::kPlaying;
      }
      Step(*host, sessions, [&](std::size_t i) {
        return runs[i]->NextCommand(*sessions[i]->GetServerView(), sessions[i]->NextSequence(), 0);
      });
      Record(sessions, reenacted_seen_);
    }
    host->EndMatch();
    host->Tick(kFixedTick);
    return spawned;
  }

  // Expects every Command of played to reach the World in reenacted, in
  // order, within kTolerance ticks of its offset and with its Seen time's
  // delay; reenacted holds the fillers of its gaps besides.
  static void ExpectReenacted(const Script& played, const Script& reenacted) {
    std::size_t next = 0;
    for (const augusta::capture::AtOffset<augusta::capture::Command>& command : played.commands) {
      const auto found =
          std::find_if(reenacted.commands.begin() + static_cast<std::ptrdiff_t>(next), reenacted.commands.end(),
                       [&](const augusta::capture::AtOffset<augusta::capture::Command>& candidate) {
                         return candidate.event.command.yaw == command.event.command.yaw &&
                                candidate.event.command.movement.direction == command.event.command.movement.direction;
                       });
      ASSERT_NE(found, reenacted.commands.end()) << "the Command of offset " << command.offset << " never arrived";
      next = static_cast<std::size_t>(found - reenacted.commands.begin()) + 1;
      EXPECT_LE(std::abs(static_cast<std::int64_t>(found->offset) - command.offset), kTolerance)
          << "captured at " << command.offset << ", reenacted at " << found->offset;
      EXPECT_LE(std::abs((static_cast<std::int64_t>(found->offset) - found->event.seen_offset) -
                         (static_cast<std::int64_t>(command.offset) - command.event.seen_offset)),
                kTolerance)
          << "the Seen time of the Command of offset " << command.offset;
    }
  }

  std::filesystem::path playtest_;
  std::filesystem::path reenacted_;
  augusta::scenario::Character holder_ = Holder();
  augusta::animation::Poser poser_;
  // What each player was told of its own body, tick by tick, by where it
  // spawned: in the playtest and in its Reenactment.
  std::map<float, std::vector<Seen>> played_;
  std::map<float, std::vector<Seen>> reenacted_seen_;
};

// Requirements: US-21
TEST_F(ReenactmentHostTest, EveryCapturedPlayerSpawnsWhereTheCaptureHasItAndItsCommandsReachTheWorldOnTheirTicks) {
  Playtest();
  const std::vector<Vec3> spawned = Reenact();

  for (const augusta::capture::Player player : {augusta::capture::Player{1}, augusta::capture::Player{2}}) {
    SCOPED_TRACE(+player);
    const Script played = ReadPlayer(playtest_, player);
    ASSERT_GE(spawned.size(), 2U);
    EXPECT_EQ(spawned[player - 1], played.spawn);
    // The run numbers its players in its own Match start's order: it is told
    // apart by where it spawned.
    const std::vector<Vec3> run_spawns = ReadPlayer(reenacted_, 1).spawns;
    const auto number = std::ranges::find(run_spawns, played.spawn);
    ASSERT_NE(number, run_spawns.end());
    const Script reenacted =
        ReadPlayer(reenacted_, static_cast<augusta::capture::Player>(number - run_spawns.begin() + 1));
    EXPECT_EQ(reenacted.character, played.character);
    ASSERT_GT(played.commands.size(), 10U);
    ExpectReenacted(played, reenacted);
    if (played.leave.has_value()) {
      ASSERT_TRUE(reenacted.leave.has_value());
      EXPECT_LE(std::abs(static_cast<std::int64_t>(*reenacted.leave) - *played.leave), kTolerance)
          << "left at " << *played.leave << ", reenacted at " << *reenacted.leave;
    }
  }
  // The playtest's players are numbered in the order they were admitted, which
  // the two connecting at once leaves open: whichever it is, one left.
  EXPECT_NE(ReadPlayer(playtest_, 1).leave.has_value(), ReadPlayer(playtest_, 2).leave.has_value());
}

// The Commands re-run reproduce each player's view, its pitch and ADS, and so
// the pose its Clips give its body (ADR-0066): each player's own body, as its
// client is told it, takes the playtest's views in the playtest's order, each
// posed as it was then.
// Requirements: US-21
TEST_F(ReenactmentHostTest, EachPlayersPitchAdsAndPoseAreThePlaytests) {
  Playtest();
  Reenact();

  ASSERT_EQ(played_.size(), 2U);
  for (const auto& [spawn, seen] : played_) {
    SCOPED_TRACE(spawn);
    const std::vector<Seen> played = Views(seen);
    const std::vector<Seen> reenacted = Views(reenacted_seen_[spawn]);
    ASSERT_GT(played.size(), 10U);
    ASSERT_EQ(reenacted.size(), played.size());
    for (std::size_t i = 0; i < played.size(); ++i) {
      SCOPED_TRACE(i);
      EXPECT_EQ(reenacted[i].pitch, played[i].pitch);
      EXPECT_EQ(reenacted[i].ads, played[i].ads);
      ASSERT_EQ(reenacted[i].pose.size(), played[i].pose.size());
      for (std::size_t joint = 0; joint < played[i].pose.size(); ++joint) {
        EXPECT_LT(augusta::math::Length(reenacted[i].pose[joint].translation - played[i].pose[joint].translation),
                  1e-5F);
        EXPECT_GT(std::abs(glm::dot(reenacted[i].pose[joint].rotation, played[i].pose[joint].rotation)), 1.0F - 1e-6F);
      }
    }
  }
}

// A Captured player's Reenact request carries its Run ID as a Join request
// does, which the admission line logs (ADR-0038).
// Requirements: US-21
TEST_F(ReenactmentHostTest, ACapturedPlayerIsLoggedWithTheRunIdItSent) {
  augusta::logging::Init();
  const std::unique_ptr<Host> host = Server(reenacted_, {Vec3(-30, kFloorY, 0), Vec3(-40, kFloorY, 5)}, true);
  const boost::uuids::uuid run = boost::uuids::time_generator_v7{}();
  std::vector<std::unique_ptr<Session>> sessions;
  sessions.push_back(std::make_unique<Session>(SessionConfig{.server = {.address = host->ListenEndpoint().address},
                                                             .character = kCharacter,
                                                             .spawn = Vec3(7, kFloorY, 7),
                                                             .run = run},
                                               WorldWithFloorAt(kFloorY)));
  sessions.back()->Connect();

  testing::internal::CaptureStdout();
  const bool admitted =
      ExchangeUntil(*host, Pointers(sessions), [&] { return sessions.back()->GetServerView()->accepted.has_value(); });
  const std::string log = testing::internal::GetCapturedStdout();

  ASSERT_TRUE(admitted);
  EXPECT_NE(log.find("event=lobby_joined peer="), std::string::npos) << log;
  EXPECT_NE(log.find(" run=" + boost::uuids::to_string(run) + " session="), std::string::npos) << log;
  EXPECT_NE(log.find("event=reenactment_joined"), std::string::npos) << log;
}

// Requirements: US-21
TEST_F(ReenactmentHostTest, AServerWithReenactmentsOnStillAdmitsAJoinRequestWhereGamePolicyPlacesIt) {
  const std::unique_ptr<Host> host = Server(reenacted_, {Vec3(-30, kFloorY, 0), Vec3(-40, kFloorY, 5)}, true);
  std::vector<std::unique_ptr<Session>> sessions;
  sessions.push_back(std::make_unique<Session>(
      SessionConfig{
          .server = {.address = host->ListenEndpoint().address}, .character = kCharacter, .spawn = Vec3(7, kFloorY, 7)},
      WorldWithFloorAt(kFloorY)));
  sessions.push_back(std::make_unique<Session>(TestSessionConfig(host->ListenEndpoint()), WorldWithFloorAt(kFloorY)));
  for (const auto& session : sessions) {
    session->Connect();
  }

  ASSERT_TRUE(DriveIntoMatch(*host, Pointers(sessions)));

  // Both connect at once, so either may be admitted first: the Match start
  // lists them in that order, which this does not look at.
  std::vector<Vec3> spawns;
  for (const auto& player : sessions[0]->GetServerView()->match_start.value().players) {
    spawns.push_back(player.spawn);
  }
  ASSERT_EQ(spawns.size(), 2U);
  EXPECT_EQ(std::ranges::count(spawns, Vec3(7, kFloorY, 7)), 1);
  EXPECT_EQ(std::ranges::count(spawns, Vec3(-30, kFloorY, 0)), 1);
}

}  // namespace
