#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/content.h"
#include "augusta/harness.h"
#include "augusta/host.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"
#include "session_test.h"

// Shot judgements reaching a Session (ADR-0038, ADR-0074): end to end, on a
// real server host and real client sessions over loopback, and against a
// server speaking the protocol by hand for what a real one would not send.
// Built only into a Development build's Session tests, whose session_test.cpp
// starts the Link.
namespace {

using augusta::command::Command;
using augusta::harness::DebugSubscribe;
using augusta::harness::Phase;
using augusta::harness::Session;
using augusta::harness::Shot;
using augusta::harness::ShotJudgement;
using augusta::host::Host;
using augusta::host::HostConfig;
using augusta::host::MessageType;
using augusta::link::ConnectionState;
using augusta::link::Struck;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::protocol::ShotJudgementWire;
using augusta::scenario::Scenario;
using augusta::session_test::DriveIntoMatch;
using augusta::session_test::ExchangeUntil;
using augusta::session_test::FloorAt;
using augusta::session_test::kCharacter;
using augusta::session_test::kFixedTick;
using augusta::session_test::kScriptedEntity;
using augusta::session_test::NeverArrived;
using augusta::session_test::Pointers;
using augusta::session_test::ScriptedServer;
using augusta::session_test::Settle;
using augusta::session_test::TestHostConfig;
using augusta::session_test::TestSessionConfig;
using augusta::session_test::WithPlayerCount;
using augusta::session_test::WorldWithFloorAt;

// A hitbox of part on RootSkeleton's Joint: the box from low to high, as the
// twelve triangles of its faces.
augusta::assets::JointHitbox BoxHitbox(augusta::assets::BodyPart part, const Vec3& low, const Vec3& high) {
  return augusta::session_test::OnRoot(augusta::assets::HitboxData{
      .part = part,
      .mesh = {.points = {Vec3(low.x, low.y, low.z), Vec3(high.x, low.y, low.z), Vec3(high.x, high.y, low.z),
                          Vec3(low.x, high.y, low.z), Vec3(low.x, low.y, high.z), Vec3(high.x, low.y, high.z),
                          Vec3(high.x, high.y, high.z), Vec3(low.x, high.y, high.z)},
               .indices = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                           3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2}}});
}

// A match of two standing 10 m apart down -Z on a floor, a view of yaw 0
// looking from the first, the shooter, at the second, the target. Both play a
// character that is one torso Hitbox 1.8 m tall and sees from 1.6 m up.
// shot_judgements is the server's `debug.shot_judgements`.
class ShotJudgementMatchTest : public ::testing::Test {
 protected:
  static constexpr float kFloorY = 0.0F;
  static constexpr float kSpacing = 10.0F;
  static constexpr float kEyeHeight = 1.6F;
  static constexpr float kTorsoHeight = 1.2F;
  static constexpr int kSettleTicks = 30;
  static constexpr auto kNetworkDelay = std::chrono::milliseconds(8);

  explicit ShotJudgementMatchTest(bool shot_judgements = true) : host_(Config(shot_judgements), InALine()) {}

  static HostConfig Config(bool shot_judgements) {
    Parameters parameters = WithPlayerCount(2);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = 30;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.ammo.flight.max_range = 200.0F;
    parameters.starting_health = 100.0F;
    HostConfig config = TestHostConfig(parameters);
    config.shot_judgements = shot_judgements;
    return config;
  }

  static Scenario InALine() {
    return Scenario{.collision = {FloorAt(kFloorY)},
                    .spawn_points = {Vec3(0.0F, kFloorY, 0.0F), Vec3(0.0F, kFloorY, -kSpacing)},
                    .characters = {{.path = kCharacter,
                                    .hitboxes = {BoxHitbox(augusta::assets::BodyPart::kTorso, Vec3(-0.2F, 0.0F, -0.1F),
                                                           Vec3(0.2F, 1.8F, 0.1F))},
                                    .skeleton = augusta::session_test::RootSkeleton(),
                                    .eye = Vec3(0.0F, kEyeHeight, 0.0F)}}};
  }

  // Joins two clients and plays them into a match, then settles it.
  void SetUp() override {
    for (int i = 0; i < 2; ++i) {
      sessions_.push_back(
          std::make_unique<Session>(TestSessionConfig(host_.ListenEndpoint()), WorldWithFloorAt(kFloorY)));
      Session& client = *sessions_.back();
      client.Connect();
      ASSERT_TRUE(
          ExchangeUntil(host_, Pointers(sessions_), [&] { return client.GetServerView()->accepted.has_value(); }));
    }
    ASSERT_TRUE(DriveIntoMatch(host_, Pointers(sessions_)));
    commands_.assign(sessions_.size(), Command{});
    Fight(kSettleTicks);
  }

  // The client whose player spawned rank places down the line: 0 is the shooter.
  Session& Standing(int rank) {
    for (const auto& session : sessions_) {
      for (const auto& player : session->GetServerView()->match_start.value().players) {
        if (player.session == session->GetServerView()->accepted->session &&
            std::abs(player.spawn.z + (kSpacing * static_cast<float>(rank))) < 0.5F) {
          return *session;
        }
      }
    }
    ADD_FAILURE() << "no client spawned " << rank << " places down the line";
    return *sessions_.front();
  }

  // Has client send a Debug subscribe of flags, and runs the network until the
  // server has received it.
  void Subscribe(Session& client, std::uint8_t flags) {
    client.SubscribeDebug(flags);
    ++subscribes_;
    ASSERT_TRUE(ExchangeUntil(host_, Pointers(sessions_), [&] {
      return host_.Metrics().messages_received[MessageType::kDebugSubscribe].Value() == subscribes_;
    }));
  }

  // Runs the match for ticks ticks, each client on its command: every client
  // predicts and sends it, the server ticks once they have all reached it, and
  // the states come back. No test here loses a command, so only a fault holds
  // one back past ExchangeUntil's kPollDeadline: the test fails fatally there,
  // naming it, rather than tick the server without it.
  void Fight(int ticks) {
    for (int tick = 0; tick < ticks && !HasFatalFailure(); ++tick) {
      for (std::size_t i = 0; i < sessions_.size(); ++i) {
        sessions_[i]->Tick(commands_[i], kFixedTick);
      }
      // The first client whose command the server does not hold yet.
      const auto waiting = [&] {
        return std::ranges::find_if(sessions_, [&](const auto& session) {
          return host_.QueuedCommands(session->GetServerView()->accepted->session) == 0;
        });
      };
      if (!ExchangeUntil(host_, Pointers(sessions_), [&] { return waiting() == sessions_.end(); })) {
        const auto never = waiting();
        if (never != sessions_.end()) {
          FAIL() << NeverArrived(static_cast<std::size_t>(never - sessions_.begin()), **never);
        }
      }
      host_.Tick(kFixedTick);
      std::this_thread::sleep_for(kNetworkDelay);
      Exchange();
    }
  }

  void Exchange() {
    host_.PumpNetwork(std::chrono::steady_clock::now());
    for (const auto& session : sessions_) {
      session->PumpEvents();
      session->ExchangeMessages();
    }
  }

  // The shooter taps fire once at the target's torso, as the newest update it
  // has shows the target, and the match runs until its rifle is ready again.
  void ShootAtTarget() {
    Session& shooter = Standing(0);
    Session& target = Standing(1);
    const auto seen = shooter.GetServerView()->authoritative.value();
    Vec3 eye{};
    Vec3 aimed{};
    for (const auto& body : seen.bodies) {
      if (body.entity == *shooter.GetServerView()->OwnEntity()) {
        eye = body.body.position + Vec3(0.0F, kEyeHeight, 0.0F);
      }
      if (body.entity == *target.GetServerView()->OwnEntity()) {
        aimed = body.body.position + Vec3(0.0F, kTorsoHeight, 0.0F);
      }
    }
    const Vec3 aim = aimed - eye;
    Command& command = commands_[Index(shooter)];
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.seen_tick = seen.tick;
    command.fire = true;
    Fight(1);
    command.fire = false;
    Fight(8);
    Settle(host_, Pointers(sessions_));
  }

  std::size_t Index(const Session& client) const {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
      if (sessions_[i].get() == &client) {
        return i;
      }
    }
    ADD_FAILURE() << "not a client of this match";
    return 0;
  }

  static std::vector<ShotJudgement> JudgementsOf(Session& client) {
    return client.TakeShotJudgements(*client.GetServerView());
  }

  Host host_;
  std::vector<std::unique_ptr<Session>> sessions_;
  std::vector<Command> commands_;
  double subscribes_ = 0.0;
};

class ShotJudgementsOffTest : public ShotJudgementMatchTest {
 protected:
  ShotJudgementsOffTest() : ShotJudgementMatchTest(false) {}
};

TEST_F(ShotJudgementMatchTest, ASubscribedShooterIsHandedTheJudgementOfItsRoundWithItsShotsTickAndTheTargetsPose) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  Subscribe(shooter, DebugSubscribe::kShotJudgements);

  ShootAtTarget();

  const std::vector<Shot> shots = shooter.TakeShots(*shooter.GetServerView());
  ASSERT_EQ(shots.size(), 1U);
  const std::vector<ShotJudgement> judgements = JudgementsOf(shooter);
  ASSERT_EQ(judgements.size(), 1U);
  const ShotJudgement& judgement = judgements.front();
  EXPECT_EQ(judgement.shot_tick, shots.front().tick);
  EXPECT_EQ(judgement.struck, Struck::kHitbox);
  EXPECT_EQ(judgement.target, target.GetServerView()->OwnEntity());
  ASSERT_FALSE(judgement.entries.empty());
  ASSERT_EQ(judgement.entries.back().players.size(), 1U);
  const auto& judged = judgement.entries.back().players.front();
  EXPECT_EQ(judged.entity, *target.GetServerView()->OwnEntity());
  EXPECT_NEAR(judged.position.x, 0.0F, 0.05F);
  EXPECT_NEAR(judged.position.z, -kSpacing, 0.05F);
  EXPECT_TRUE(JudgementsOf(shooter).empty());
}

TEST_F(ShotJudgementMatchTest, OnlyTheShooterIsHandedItsRoundsJudgement) {
  Session& shooter = Standing(0);
  Session& target = Standing(1);
  Subscribe(shooter, DebugSubscribe::kShotJudgements);
  Subscribe(target, DebugSubscribe::kShotJudgements);

  ShootAtTarget();

  EXPECT_EQ(JudgementsOf(shooter).size(), 1U);
  EXPECT_TRUE(JudgementsOf(target).empty());
}

TEST_F(ShotJudgementMatchTest, AShooterThatNeverSubscribedIsHandedNone) {
  Session& shooter = Standing(0);

  ShootAtTarget();

  ASSERT_FALSE(shooter.TakeShots(*shooter.GetServerView()).empty());
  EXPECT_TRUE(JudgementsOf(shooter).empty());
}

TEST_F(ShotJudgementMatchTest, AShooterThatSubscribedThenClearedTheBitIsHandedNone) {
  Session& shooter = Standing(0);
  Subscribe(shooter, DebugSubscribe::kShotJudgements);
  ShootAtTarget();
  ASSERT_EQ(JudgementsOf(shooter).size(), 1U);
  Subscribe(shooter, 0);

  ShootAtTarget();

  EXPECT_TRUE(JudgementsOf(shooter).empty());
}

TEST_F(ShotJudgementsOffTest, ASubscribedShooterIsHandedNoneAndStaysConnected) {
  Session& shooter = Standing(0);
  Subscribe(shooter, DebugSubscribe::kShotJudgements);

  ShootAtTarget();

  EXPECT_FALSE(shooter.TakeShots(*shooter.GetServerView()).empty());
  EXPECT_TRUE(JudgementsOf(shooter).empty());
  EXPECT_EQ(shooter.GetConnectionState(), ConnectionState::kConnected);
  EXPECT_EQ(shooter.GetServerView()->GetPhase(), Phase::kMatch);
}

// A client admitted by a server that sends it Shot judgements by hand.
class ScriptedShotJudgementTest : public ::testing::Test {
 protected:
  ScriptedShotJudgementTest() : session_(TestSessionConfig(server_.LocalEndpoint()), WorldWithFloorAt(0.0F)) {}

  void SetUp() override { ASSERT_TRUE(server_.Admit(session_, Parameters{})); }

  // Lets the client take in whatever was sent: long enough that a message that
  // was going to arrive has.
  void Settle() {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) {
      server_.Pump();
      session_.PumpEvents();
      session_.ExchangeMessages();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  // A judgement of a round fired on shot_tick that struck the client's own player.
  static ShotJudgementWire JudgementOf(augusta::tick::Tick shot_tick) {
    return ShotJudgementWire{.shot_tick = shot_tick,
                             .entries = {{.players = {{.position = Vec3(1.0F, 0.0F, -2.0F), .entity = kScriptedEntity}},
                                          .ticks_after_shot = 0}},
                             .point = Vec3(1.0F, 1.0F, -2.0F),
                             .target = kScriptedEntity,
                             .struck = augusta::protocol::StruckWire::kHitbox};
  }

  [[nodiscard]] std::vector<ShotJudgement> Taken() { return session_.TakeShotJudgements(*session_.GetServerView()); }

  ScriptedServer server_;
  Session session_;
};

TEST_F(ScriptedShotJudgementTest, AJudgementIsHandedOutOnceAsItWasSent) {
  server_.Send(JudgementOf(7));
  Settle();

  const std::vector<ShotJudgement> judgements = Taken();

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_EQ(judgements[0].shot_tick, 7U);
  EXPECT_EQ(judgements[0].struck, Struck::kHitbox);
  EXPECT_EQ(judgements[0].target, *session_.GetServerView()->OwnEntity());
  ASSERT_EQ(judgements[0].entries.size(), 1U);
  ASSERT_EQ(judgements[0].entries[0].players.size(), 1U);
  EXPECT_EQ(judgements[0].entries[0].players[0].position, Vec3(1.0F, 0.0F, -2.0F));
  EXPECT_TRUE(Taken().empty());
}

TEST_F(ScriptedShotJudgementTest, AJudgementThatArrivesAfterMatchEndIsDropped) {
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(JudgementOf(7));
  Settle();

  EXPECT_TRUE(Taken().empty());
}

TEST_F(ScriptedShotJudgementTest, AJudgementOfAnEarlierMatchIsNotHandedOutInTheNextOne) {
  server_.Send(JudgementOf(7));
  server_.Send(augusta::protocol::MatchEndWire{});
  server_.Send(ScriptedServer::StartOfAlone());
  Settle();
  ASSERT_EQ(session_.GetServerView()->GetPhase(), Phase::kMatch);

  EXPECT_TRUE(Taken().empty());
}

// A client nobody asks keeps the newest Shot judgements, not all of them for ever.
TEST_F(ScriptedShotJudgementTest, AClientThatIsNeverAskedKeepsOnlyTheNewestJudgements) {
  const std::size_t sent = augusta::harness::kMaxPendingShotJudgements + 10;
  for (std::size_t i = 1; i <= sent; ++i) {
    server_.Send(JudgementOf(static_cast<augusta::tick::Tick>(i)));
  }
  Settle();

  const std::vector<ShotJudgement> judgements = Taken();

  ASSERT_EQ(judgements.size(), augusta::harness::kMaxPendingShotJudgements);
  EXPECT_EQ(judgements.front().shot_tick, 11U);
  EXPECT_EQ(judgements.back().shot_tick, static_cast<augusta::tick::Tick>(sent));
}

}  // namespace
