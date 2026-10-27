#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/hitbox.h"
#include "augusta/limits.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"
#include "character_fixture.h"
#include "clips_fixture.h"

// SimulationWorld's Shot judgements (ADR-0038, ADR-0074): how it judged each
// round, told once the round's flight ends, in a Development build alone.
namespace {

using augusta::body_part::BodyPart;
using augusta::command::Command;
using augusta::hitbox::CharacterHitbox;
using augusta::limits::kMaxShotJudgementEntries;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::simulation::Character;
using augusta::simulation::EntityId;
using augusta::simulation::JudgedPlayer;
using augusta::simulation::PlayerCommand;
using augusta::simulation::ShotJudgement;
using augusta::simulation::Struck;
using augusta::simulation::TickResult;
using augusta::simulation::World;

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;
constexpr int kSettleTicks = 30;

constexpr EntityId kAlice = static_cast<EntityId>(1);
constexpr EntityId kBob = static_cast<EntityId>(2);
constexpr EntityId kCarol = static_cast<EntityId>(3);

// Where a round is aimed, above a target's feet: its torso.
constexpr float kTorsoHeight = 1.2F;
// How near a point struck is to where the test works it out to be.
constexpr float kTolerance = 0.01F;

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A wall across Z = z, facing the players.
CollisionMesh WallAt(float z) {
  constexpr float kHalfWidth = 20.0F;
  constexpr float kHeight = 5.0F;
  return CollisionMesh{.points = {Vec3(-kHalfWidth, 0.0F, z), Vec3(-kHalfWidth, kHeight, z),
                                  Vec3(kHalfWidth, kHeight, z), Vec3(kHalfWidth, 0.0F, z)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A hitbox for part: the box from low to high, as the twelve triangles of its faces.
CharacterHitbox Box(BodyPart part, const Vec3& low, const Vec3& high) {
  const std::array<Vec3, 8> corners = {Vec3(low.x, low.y, low.z),    Vec3(high.x, low.y, low.z),
                                       Vec3(high.x, high.y, low.z),  Vec3(low.x, high.y, low.z),
                                       Vec3(low.x, low.y, high.z),   Vec3(high.x, low.y, high.z),
                                       Vec3(high.x, high.y, high.z), Vec3(low.x, high.y, high.z)};
  constexpr std::array<std::array<int, 3>, 12> kFaces = {{{0, 1, 2},
                                                          {0, 2, 3},
                                                          {4, 6, 5},
                                                          {4, 7, 6},
                                                          {0, 4, 5},
                                                          {0, 5, 1},
                                                          {3, 2, 6},
                                                          {3, 6, 7},
                                                          {0, 3, 7},
                                                          {0, 7, 4},
                                                          {1, 5, 6},
                                                          {1, 6, 2}}};
  CharacterHitbox hitbox{.part = part, .triangles = {}};
  for (const auto& face : kFaces) {
    hitbox.triangles.push_back({.a = corners.at(face[0]), .b = corners.at(face[1]), .c = corners.at(face[2])});
  }
  return hitbox;
}

// A character 1.8 m tall that sees from inside its head, its torso 20 cm deep:
// a round aimed at it down -Z strikes the torso's face 10 cm before its feet.
Character Target() {
  return augusta::character_fixture::Rigged(
      Character{.eye = Vec3(0.0F, 1.6F, 0.0F),
                .hitboxes = {Box(BodyPart::kHead, Vec3(-0.1F, 1.5F, -0.1F), Vec3(0.1F, 1.8F, 0.1F)),
                             Box(BodyPart::kTorso, Vec3(-0.2F, 0.9F, -0.1F), Vec3(0.2F, 1.5F, 0.1F)),
                             Box(BodyPart::kLimb, Vec3(-0.2F, 0.0F, -0.1F), Vec3(0.2F, 0.9F, 0.1F))}});
}

// A rifle of 600 rounds a minute whose rounds fly straight, 10 m a tick.
Parameters WithTheTestRifle() {
  Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.magazine_capacity = 200;
  parameters.rifle.muzzle_velocity = 600.0F;
  parameters.ammo.flight.gravity = 0.0F;
  parameters.ammo.flight.max_range = 1000.0F;
  return parameters;
}

// Alice, at the origin, shoots at Bob, placed by each test, with Shot
// judgements switched on. The test is Alice's client: it keeps where the State
// of each tick puts Bob and aims at where a Seen time of them shows him.
class ShotJudgementTest : public ::testing::Test {
 protected:
  explicit ShotJudgementTest(const Parameters& parameters = WithTheTestRifle(),
                             const augusta::weapon::Markers& rifle = {})
      : world_(parameters, kTickRate, {}, rifle) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
    world_.JudgeShots(true);
  }

  // Adds Alice at the origin and Bob at bob, standing on the floor, and lets them settle.
  void Place(const Vec3& bob) {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), Target());
    world_.AddPlayer(kBob, bob + Vec3(0.0F, 0.5F, 0.0F), Target());
    Wait(kSettleTicks);
  }

  // A tick on which Alice does command and Bob what he was last told.
  TickResult Tick(const Command& command) {
    TickResult result = world_.Tick(
        {PlayerCommand{.entity = kAlice, .command = command}, PlayerCommand{.entity = kBob, .command = bob_}}, kTick);
    for (const auto& entry : result.state.bodies) {
      if (entry.entity == kBob) {
        seen_[result.state.tick] = entry.body.position;
      } else if (entry.entity == kAlice) {
        alice_ = entry.body.position;
      }
    }
    last_tick_ = result.state.tick;
    return result;
  }

  // Ticks the given number of times with Alice idle; returns every Shot judgement of them.
  std::vector<ShotJudgement> Wait(int ticks) {
    std::vector<ShotJudgement> judgements;
    for (int i = 0; i < ticks; ++i) {
      const TickResult result = Tick(Command{});
      judgements.insert(judgements.end(), result.shot_judgements.begin(), result.shot_judgements.end());
    }
    return judgements;
  }

  // A one-tick press of fire by Alice, aimed at point, on a Command that
  // reports the Seen time of seen.
  [[nodiscard]] Command FiringAt(const Vec3& point, augusta::tick::Tick seen) const {
    const Vec3 aim = point - (alice_ + Target().eye);
    Command command{};
    command.fire = true;
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.seen_tick = seen;
    return command;
  }

  // Alice does command for a tick, then idles for ticks more; returns every
  // Shot judgement of them.
  std::vector<ShotJudgement> Shoot(const Command& command, int ticks = 6) {
    std::vector<ShotJudgement> judgements = Tick(command).shot_judgements;
    const std::vector<ShotJudgement> later = Wait(ticks);
    judgements.insert(judgements.end(), later.begin(), later.end());
    return judgements;
  }

  // The tick Alice's next Command is taken in on.
  [[nodiscard]] augusta::tick::Tick Next() const { return last_tick_ + 1; }

  // Where the State of tick put Bob's feet.
  [[nodiscard]] Vec3 BobAt(augusta::tick::Tick tick) const { return seen_.at(tick); }

  World world_;
  // What Bob is told to do on every tick.
  Command bob_{};
  Vec3 alice_{};
  std::map<augusta::tick::Tick, Vec3> seen_;
  augusta::tick::Tick last_tick_ = 0;
};

// Bob stands 10 m away, which a round reaches on the tick it is fired.
// Requirements: US-11
TEST_F(ShotJudgementTest, ARoundThatStrikesAHitboxReportsItsBodyPartPointAndTarget) {
  Place(Vec3(0.0F, 0.0F, -10.0F));
  const augusta::tick::Tick seen = Next() - 1;
  const Vec3 bob = BobAt(seen);

  const std::vector<ShotJudgement> judgements = Shoot(FiringAt(bob + Vec3(0.0F, kTorsoHeight, 0.0F), seen));

  ASSERT_EQ(judgements.size(), 1U);
  const ShotJudgement& judgement = judgements[0];
  EXPECT_EQ(judgement.shooter, kAlice);
  EXPECT_EQ(judgement.shot_tick, seen + 1);
  EXPECT_FLOAT_EQ(judgement.shooters_delay, 1.0F);
  EXPECT_EQ(judgement.struck, Struck::kHitbox);
  EXPECT_EQ(judgement.part, BodyPart::kTorso);
  EXPECT_EQ(judgement.target, std::optional<EntityId>(kBob));
  EXPECT_NEAR(judgement.point.x, bob.x, kTolerance);
  EXPECT_NEAR(judgement.point.z, bob.z + 0.1F, kTolerance);
  ASSERT_EQ(judgement.entries.size(), 1U);
  EXPECT_EQ(judgement.entries[0].ticks_after_shot, 0U);
  EXPECT_EQ(judgement.entries[0].players,
            std::vector<JudgedPlayer>({{.entity = kBob, .position = bob, .stance = Stance::kStanding, .yaw = 0.0F}}));
  EXPECT_FALSE(judgement.entries_left_out);
}

// A wall 5 m away, between Alice and Bob, 10 m away: the round's segment
// reaches Bob's bounds, but the wall stops it first.
// Requirements: US-10
TEST_F(ShotJudgementTest, ARoundThatStrikesTheMapReportsTheMapAndNoTarget) {
  ASSERT_TRUE(world_.AddCollisionMesh(WallAt(-5.0F)).has_value());
  Place(Vec3(0.0F, 0.0F, -10.0F));
  const augusta::tick::Tick seen = Next() - 1;

  const std::vector<ShotJudgement> judgements = Shoot(FiringAt(BobAt(seen) + Vec3(0.0F, kTorsoHeight, 0.0F), seen));

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_EQ(judgements[0].struck, Struck::kMap);
  EXPECT_EQ(judgements[0].target, std::nullopt);
  EXPECT_NEAR(judgements[0].point.z, -5.0F, kTolerance);
}

// Alice fires the other way from Bob, into open space.
// Requirements: US-10
TEST_F(ShotJudgementTest, ARoundThatExpiresReportsNothingStruckAndARoundPastNoPlayerHasNoEntries) {
  Place(Vec3(0.0F, 0.0F, -10.0F));
  const augusta::tick::Tick seen = Next() - 1;

  const std::vector<ShotJudgement> judgements = Shoot(FiringAt(alice_ + Vec3(0.0F, 1.6F, 10.0F), seen), 120);

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_EQ(judgements[0].struck, Struck::kNothing);
  EXPECT_EQ(judgements[0].target, std::nullopt);
  EXPECT_TRUE(judgements[0].entries.empty());
  EXPECT_FALSE(judgements[0].entries_left_out);
}

// Carol stands a metre behind Bob: the segment that strikes Bob reaches her
// bounds too, and Alice's own, where it leaves from.
// Requirements: US-11
TEST_F(ShotJudgementTest, EveryPlayerTheSegmentReachesIsAnEntryButNeverTheShooter) {
  world_.AddPlayer(kCarol, Vec3(0.0F, 0.5F, -11.0F), Target());
  Place(Vec3(0.0F, 0.0F, -10.0F));
  const augusta::tick::Tick seen = Next() - 1;

  const std::vector<ShotJudgement> judgements = Shoot(FiringAt(BobAt(seen) + Vec3(0.0F, kTorsoHeight, 0.0F), seen));

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_EQ(judgements[0].target, std::optional<EntityId>(kBob));
  ASSERT_EQ(judgements[0].entries.size(), 1U);
  const std::vector<JudgedPlayer>& players = judgements[0].entries[0].players;
  ASSERT_EQ(players.size(), 2U);
  EXPECT_EQ(players[0].entity, kBob);
  EXPECT_EQ(players[1].entity, kCarol);
}

// Bob walks across Alice's view, 5 cm a tick, 8 m away; Alice aims where a
// Seen time ten ticks old shows him.
// Requirements: NFR-02
TEST_F(ShotJudgementTest, ARoundReportsThePoseOfAPlayerAtItsShootersDelayNotItsPresentOne) {
  Place(Vec3(-3.0F, 0.0F, -8.0F));
  bob_.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  Wait(60);
  const augusta::tick::Tick seen = Next() - 10;
  const Vec3 then = BobAt(seen);
  ASSERT_GT(BobAt(Next() - 1).x - then.x, 0.4F);

  const std::vector<ShotJudgement> judgements = Shoot(FiringAt(then + Vec3(0.0F, kTorsoHeight, 0.0F), seen));

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_FLOAT_EQ(judgements[0].shooters_delay, 10.0F);
  ASSERT_EQ(judgements[0].entries.size(), 1U);
  ASSERT_EQ(judgements[0].entries[0].players.size(), 1U);
  const JudgedPlayer& judged = judgements[0].entries[0].players[0];
  EXPECT_EQ(judged.entity, kBob);
  EXPECT_NEAR(judged.position.x, then.x, 1e-4F);
  EXPECT_NEAR(judged.position.z, then.z, 1e-4F);
}

// A rifle whose Muzzle is kHolderMuzzle ahead of the Grip Alice holds at her
// Eye (clips_fixture::HolderRifle), and a wall half a metre ahead of her, which
// her barrel pokes through (ADR-0044).
class MuzzleInAWallShotJudgementTest : public ShotJudgementTest {
 protected:
  static constexpr float kWallZ = -0.5F;

  MuzzleInAWallShotJudgementTest() : ShotJudgementTest(WithTheTestRifle(), augusta::clips_fixture::HolderRifle()) {}
};

// Bob stands 10 m away, behind the wall: the round strikes the wall where the
// barrel enters it, on the tick it is fired, and passes no player.
// Requirements: US-10
TEST_F(MuzzleInAWallShotJudgementTest, ARoundFromAMuzzleThroughAWallReportsTheWallWhereTheBarrelEntersIt) {
  ASSERT_TRUE(world_.AddCollisionMesh(WallAt(kWallZ)).has_value());
  Place(Vec3(0.0F, 0.0F, -10.0F));
  const augusta::tick::Tick seen = Next() - 1;

  const std::vector<ShotJudgement> judgements = Shoot(FiringAt(BobAt(seen) + Vec3(0.0F, kTorsoHeight, 0.0F), seen));

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_EQ(judgements[0].shooter, kAlice);
  EXPECT_EQ(judgements[0].shot_tick, seen + 1);
  EXPECT_FLOAT_EQ(judgements[0].shooters_delay, 1.0F);
  EXPECT_EQ(judgements[0].struck, Struck::kMap);
  EXPECT_EQ(judgements[0].target, std::nullopt);
  EXPECT_NEAR(judgements[0].point.z, kWallZ, kTolerance);
  EXPECT_TRUE(judgements[0].entries.empty());
}

// Rounds that crawl a metre a second and fly half a metre: Alice fires one
// past Bob, standing 80 cm beside her, whose bounds it stays within for all
// 30 ticks of its flight.
class SlowRoundTest : public ShotJudgementTest {
 protected:
  static constexpr int kFlightTicks = 30;

  static Parameters Slow() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.muzzle_velocity = 1.0F;
    parameters.ammo.flight.max_range = 0.5F;
    return parameters;
  }

  SlowRoundTest() : ShotJudgementTest(Slow()) {}

  void SetUp() override { Place(Vec3(0.8F, 0.0F, 0.0F)); }

  [[nodiscard]] Command FiringAway() const { return FiringAt(alice_ + Vec3(0.0F, 1.6F, -10.0F), Next() - 1); }
};

// Requirements: US-30
TEST_F(SlowRoundTest, AFlightCrossingMoreTicksThanTheEntriesHoldKeepsTheFirstAndSaysSomeWereLeftOut) {
  const std::vector<ShotJudgement> judgements = Shoot(FiringAway(), kFlightTicks + 6);

  ASSERT_EQ(judgements.size(), 1U);
  EXPECT_EQ(judgements[0].struck, Struck::kNothing);
  ASSERT_EQ(judgements[0].entries.size(), kMaxShotJudgementEntries);
  for (std::uint32_t i = 0; i < kMaxShotJudgementEntries; ++i) {
    EXPECT_EQ(judgements[0].entries[i].ticks_after_shot, i);
  }
  EXPECT_TRUE(judgements[0].entries_left_out);
}

// The same flight, its match left running, is judged once it ends.
// Requirements: US-30
TEST_F(SlowRoundTest, ARoundStillInFlightWhenItsMatchEndsHasNoJudgement) {
  ASSERT_EQ(Shoot(FiringAway(), kFlightTicks + 6).size(), 1U);
  ASSERT_TRUE(Tick(FiringAway()).shot_judgements.empty());

  world_.EndMatch();
  Place(Vec3(0.8F, 0.0F, 0.0F));

  EXPECT_TRUE(Wait(kFlightTicks + 6).empty());
}

// A World nobody switched Shot judgements on in.
class UnjudgedShotTest : public ShotJudgementTest {
 protected:
  UnjudgedShotTest() { world_.JudgeShots(false); }
};

// Requirements: US-30
TEST_F(UnjudgedShotTest, WithShotJudgementsOffATickCarriesNone) {
  Place(Vec3(0.0F, 0.0F, -10.0F));
  const augusta::tick::Tick seen = Next() - 1;

  const TickResult fired = Tick(FiringAt(BobAt(seen) + Vec3(0.0F, kTorsoHeight, 0.0F), seen));

  ASSERT_EQ(fired.state.hits.size(), 1U);
  EXPECT_TRUE(fired.shot_judgements.empty());
}

}  // namespace
