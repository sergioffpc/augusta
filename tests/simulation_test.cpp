#include "augusta/simulation.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"

// SimulationWorld on flat ground: players are entities with bodies, moved by
// the commands of each tick.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::simulation::EntityId;
using augusta::simulation::PlayerCommand;
using augusta::simulation::State;
using augusta::simulation::World;

constexpr float kTick = 1.0F / 60.0F;
constexpr int kSettleTicks = 30;
constexpr int kWalkTicks = 60;

constexpr EntityId kAlice = static_cast<EntityId>(1);
constexpr EntityId kBob = static_cast<EntityId>(2);

// Every test character's eye, standing, relative to its feet (ADR-0040).
const Vec3 kEye(0.0F, 1.5F, 0.0F);

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

Command Walking(Vec3 direction, Stance stance = Stance::kStanding) {
  Command command;
  command.movement.direction = direction;
  command.movement.desired_stance = stance;
  return command;
}

class SimulationTest : public ::testing::Test {
 protected:
  SimulationTest() : world_(Parameters{}) { EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value()); }

  // Ticks n times with the same commands and returns the last state.
  State Run(int ticks, const std::vector<PlayerCommand>& commands) {
    State state;
    for (int i = 0; i < ticks; ++i) {
      state = world_.Tick(commands, kTick);
    }
    return state;
  }

  static const augusta::physics::BodyState& Body(const State& state, EntityId entity) {
    for (const auto& entry : state.bodies) {
      if (entry.entity == entity) {
        return entry.body;
      }
    }
    ADD_FAILURE() << "player not in state";
    return state.bodies.front().body;
  }

  static float HorizontalSpeed(const augusta::physics::BodyState& body) {
    return std::hypot(body.velocity.x, body.velocity.z);
  }

  World world_;
};

TEST_F(SimulationTest, AWallStopsAWalkingPlayer) {
  constexpr float kWallX = 3.0F;
  constexpr float kHalfWidth = 20.0F;
  constexpr float kHeight = 5.0F;
  constexpr float kBottom = 0.0F;
  ASSERT_TRUE(world_
                  .AddCollisionMesh(
                      CollisionMesh{.points = {Vec3(kWallX, kBottom, -kHalfWidth), Vec3(kWallX, kHeight, -kHalfWidth),
                                               Vec3(kWallX, kHeight, kHalfWidth), Vec3(kWallX, kBottom, kHalfWidth)},
                                    .indices = {0, 1, 2, 0, 2, 3}})
                  .has_value());
  // Dropped a little above the floor: a body placed exactly on it starts overlapping it.
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), kEye);
  Run(kSettleTicks, {});

  const State state = Run(120, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_LT(Body(state, kAlice).position.x, kWallX);
  EXPECT_GT(Body(state, kAlice).position.x, kWallX - 1.0F);
}

TEST_F(SimulationTest, AnEmptyWorldHasAnEmptyState) { EXPECT_TRUE(world_.Tick({}, kTick).bodies.empty()); }

TEST_F(SimulationTest, AddedPlayersAppearInTheStateOrderedById) {
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kEye);
  world_.AddPlayer(kAlice, Vec3(-5.0F, 0.0F, 0.0F), kEye);

  const State state = world_.Tick({}, kTick);

  ASSERT_EQ(state.bodies.size(), 2U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
  EXPECT_EQ(state.bodies[1].entity, kBob);
}

TEST_F(SimulationTest, APlayerStartsStandingWhereItSpawned) {
  world_.AddPlayer(kAlice, Vec3(3.0F, 0.0F, -2.0F), kEye);
  Run(kSettleTicks, {});

  const State state = world_.Tick({}, kTick);

  EXPECT_NEAR(Body(state, kAlice).position.x, 3.0F, 0.05F);
  EXPECT_NEAR(Body(state, kAlice).position.z, -2.0F, 0.05F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kStanding);
}

TEST_F(SimulationTest, AForwardCommandMovesThatPlayerOnly) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);
  world_.AddPlayer(kBob, Vec3(0.0F, 0.0F, 10.0F), kEye);
  Run(kSettleTicks, {});
  const float start = Body(world_.Tick({}, kTick), kAlice).position.x;
  const float bob_start = Body(world_.Tick({}, kTick), kBob).position.x;

  const State state = Run(kWalkTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_GT(Body(state, kAlice).position.x, start + 2.0F);
  EXPECT_NEAR(Body(state, kBob).position.x, bob_start, 0.01F);
}

TEST_F(SimulationTest, StanceCommandsChangeTheStanceAndTheSpeed) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);
  Run(kSettleTicks, {});
  const auto walk_at = [&](Stance stance) {
    const State state =
        Run(kWalkTicks / 4, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(0.0F, 0.0F, 1.0F), stance)}});
    EXPECT_EQ(Body(state, kAlice).stance, stance);
    return HorizontalSpeed(Body(state, kAlice));
  };

  const float standing = walk_at(Stance::kStanding);
  const float crouching = walk_at(Stance::kCrouching);
  const float prone = walk_at(Stance::kProne);

  EXPECT_GT(standing, crouching + 0.3F);
  EXPECT_GT(crouching, prone + 0.3F);
  EXPECT_GT(prone, 0.1F);
}

TEST_F(SimulationTest, APlayerWithNoCommandStopsAndKeepsItsStance) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);
  Run(kSettleTicks, {});
  Run(kWalkTicks / 2,
      {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F), Stance::kCrouching)}});

  const State state = Run(kWalkTicks / 2, {});

  EXPECT_NEAR(HorizontalSpeed(Body(state, kAlice)), 0.0F, 0.01F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kCrouching);
}

TEST_F(SimulationTest, ACommandForAPlayerNotInTheWorldIsIgnored) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);

  const State state = Run(kSettleTicks, {PlayerCommand{.entity = kBob, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
}

TEST_F(SimulationTest, ARemovedPlayerLeavesTheState) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kEye);
  Run(kSettleTicks, {});

  world_.RemovePlayer(kAlice);
  const State state = world_.Tick({}, kTick);

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kBob);
}

TEST_F(SimulationTest, RemovingAPlayerNotInTheWorldChangesNothing) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);

  world_.RemovePlayer(kBob);

  EXPECT_EQ(world_.Tick({}, kTick).bodies.size(), 1U);
}

TEST_F(SimulationTest, AddingAPlayerTwiceKeepsOne) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kEye);
  world_.AddPlayer(kAlice, Vec3(9.0F, 0.0F, 0.0F), kEye);

  EXPECT_EQ(world_.Tick({}, kTick).bodies.size(), 1U);
}

// A rifle of 600 rounds a minute, a round every six ticks of kTick, with a
// magazine that outlasts a test; its rounds fly straight, 10 m a tick.
Parameters WithTheTestRifle() {
  Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.magazine_capacity = 200;
  parameters.rifle.muzzle_velocity = 600.0F;
  parameters.ammo.gravity = 0.0F;
  parameters.ammo.max_range = 1000.0F;
  return parameters;
}

Command Firing() {
  // Braces, so the movement direction is zero rather than left unset.
  Command command{};
  command.fire = true;
  return command;
}

// Alice alone on the floor with a rifle, settled before each test.
class FireTest : public ::testing::Test {
 protected:
  explicit FireTest(const Parameters& parameters = WithTheTestRifle()) : world_(parameters) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
  }

  void SetUp() override {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), kEye);
    for (int i = 0; i < kSettleTicks; ++i) {
      world_.Tick({}, kTick);
    }
  }

  State Tick(const Command& command) {
    return world_.Tick({PlayerCommand{.entity = kAlice, .command = command}}, kTick);
  }

  // The one Shot of a tick on which Alice fires with command.
  augusta::simulation::Shot ShotOf(const Command& command) {
    const State state = Tick(command);
    EXPECT_EQ(state.shots.size(), 1U);
    return state.shots.empty() ? augusta::simulation::Shot{} : state.shots.front();
  }

  // Ticks the given number of times with no command and returns the last state.
  State Run(int ticks) {
    State state;
    for (int i = 0; i < ticks; ++i) {
      state = world_.Tick({}, kTick);
    }
    return state;
  }

  // Where Alice's feet are.
  Vec3 Feet() { return world_.Tick({}, kTick).bodies.front().body.position; }

  // Ticks the given number of times with command; returns on which of them,
  // counted from 0, a Shot was fired.
  std::vector<int> FiringTicks(int ticks, const Command& command) {
    std::vector<int> fired;
    for (int i = 0; i < ticks; ++i) {
      if (!Tick(command).shots.empty()) {
        fired.push_back(i);
      }
    }
    return fired;
  }

  World world_;
};

TEST_F(FireTest, HoldingFireFiresARoundAtOnceAndThenOneEveryFireInterval) {
  EXPECT_EQ(FiringTicks(19, Firing()), (std::vector<int>{0, 6, 12, 18}));
}

TEST_F(FireTest, ATapOfFireFiresExactlyOneRound) {
  EXPECT_EQ(Tick(Firing()).shots.size(), 1U);

  EXPECT_TRUE(FiringTicks(30, Command{}).empty());
}

TEST_F(FireTest, AShotNamesThePlayerWhoFiredIt) {
  world_.AddPlayer(kBob, Vec3(5.0F, 0.5F, 0.0F), kEye);

  const State state = world_.Tick({PlayerCommand{.entity = kBob, .command = Firing()}}, kTick);

  ASSERT_EQ(state.shots.size(), 1U);
  EXPECT_EQ(state.shots[0].shooter, kBob);
}

TEST_F(FireTest, TappingFasterThanTheFireRateFiresNoFasterThanIt) {
  std::vector<int> fired;
  for (int i = 0; i < 19; ++i) {
    // Fire pressed on every other tick.
    if (!Tick(i % 2 == 0 ? Firing() : Command{}).shots.empty()) {
      fired.push_back(i);
    }
  }

  EXPECT_EQ(fired, (std::vector<int>{0, 6, 12, 18}));
}

TEST_F(FireTest, ARifleAtRestFiresNoSoonerForHavingRested) {
  Tick(Firing());
  FiringTicks(60, Command{});

  EXPECT_EQ(FiringTicks(7, Firing()), (std::vector<int>{0, 6}));
}

TEST_F(FireTest, AShotLeavesFromTheShootersEyeAlongTheCommandsView) {
  const Vec3 feet = Feet();
  Command command = Firing();
  command.yaw = 0.75F;
  command.pitch = -0.25F;

  const augusta::simulation::Shot shot = ShotOf(command);

  EXPECT_NEAR(shot.origin.x, feet.x, 0.002F);
  EXPECT_NEAR(shot.origin.y, feet.y + 1.5F, 0.002F);
  EXPECT_NEAR(shot.origin.z, feet.z, 0.002F);
  EXPECT_EQ(shot.yaw, 0.75F);
  EXPECT_EQ(shot.pitch, -0.25F);
}

// The eye is 1.5 m up a body 2.1 m tall standing, 1.3 m crouching and 0.7 m prone.
TEST_F(FireTest, ACrouchedOrProneShooterFiresFromALowerEye) {
  const float feet = Feet().y;
  Command crouched = Firing();
  crouched.movement.desired_stance = Stance::kCrouching;
  Command prone = Firing();
  prone.movement.desired_stance = Stance::kProne;

  EXPECT_NEAR(ShotOf(crouched).origin.y, feet + (1.5F * 1.3F / 2.1F), 0.002F);
  FiringTicks(5, Command{});
  EXPECT_NEAR(ShotOf(prone).origin.y, feet + (1.5F * 0.7F / 2.1F), 0.002F);
}

// A client is told a Shot's numbers as counts of their grids (ADR-0038), so the
// server fires the round those counts stand for.
TEST_F(FireTest, AShotsOriginAndDirectionAreOnTheGridsTheyTravelOn) {
  Command command = Firing();
  command.yaw = 0.1234567F;
  command.pitch = -0.0654321F;

  const augusta::simulation::Shot shot = ShotOf(command);

  EXPECT_EQ(shot.origin, augusta::math::SnapPosition(shot.origin));
  EXPECT_EQ(shot.yaw, augusta::math::SnapAngle(0.1234567F));
  EXPECT_EQ(shot.pitch, augusta::math::SnapAngle(-0.0654321F));
}

// Fire held with the view turned a quarter turn to the right: down +X.
Command FiringDownX() {
  Command command = Firing();
  command.yaw = -std::numbers::pi_v<float> / 2.0F;
  return command;
}

// A wall across +X, 25 m from where Alice stands.
class WallFireTest : public FireTest {
 protected:
  static constexpr float kWallX = 25.0F;

  WallFireTest() {
    constexpr float kHalfWidth = 20.0F;
    constexpr float kHeight = 5.0F;
    EXPECT_TRUE(world_
                    .AddCollisionMesh(
                        CollisionMesh{.points = {Vec3(kWallX, 0.0F, -kHalfWidth), Vec3(kWallX, kHeight, -kHalfWidth),
                                                 Vec3(kWallX, kHeight, kHalfWidth), Vec3(kWallX, 0.0F, kHalfWidth)},
                                      .indices = {0, 1, 2, 0, 2, 3}})
                    .has_value());
  }
};

// Rounds fly 10 m a tick, from the tick they are fired on: the third tick's
// segment, 20 to 30 m out, is the one that crosses the wall.
TEST_F(WallFireTest, ABulletFiredAtAWallEndsOnTheTickItsSegmentReachesTheWall) {
  const Vec3 feet = Feet();

  const State first = Tick(FiringDownX());
  const State second = Tick(Command{});
  const State third = Tick(Command{});
  const State fourth = Tick(Command{});

  EXPECT_EQ(first.bullets_in_flight, 1U);
  EXPECT_TRUE(first.map_impacts.empty());
  EXPECT_EQ(second.bullets_in_flight, 1U);
  EXPECT_TRUE(second.map_impacts.empty());
  EXPECT_EQ(third.bullets_in_flight, 0U);
  ASSERT_EQ(third.map_impacts.size(), 1U);
  EXPECT_NEAR(third.map_impacts[0].x, kWallX, 0.01F);
  EXPECT_NEAR(third.map_impacts[0].y, feet.y + 1.5F, 0.01F);
  EXPECT_NEAR(third.map_impacts[0].z, feet.z, 0.01F);
  EXPECT_EQ(fourth.bullets_in_flight, 0U);
  EXPECT_TRUE(fourth.map_impacts.empty());
}

TEST_F(WallFireTest, EveryBulletOfABurstEndsOnTheWall) {
  std::size_t impacts = 0;
  for (int i = 0; i < 13; ++i) {
    impacts += Tick(FiringDownX()).map_impacts.size();
  }
  // Three rounds, on ticks 0, 6 and 12; the first two have had their three ticks.
  EXPECT_EQ(impacts, 2U);
  EXPECT_EQ(Tick(Command{}).bullets_in_flight, 1U);
}

// Ammunition that flies 25 m and no further.
class ShortRangeFireTest : public FireTest {
 protected:
  static Parameters WithARangeOfTwentyFiveMeters() {
    Parameters parameters = WithTheTestRifle();
    parameters.ammo.max_range = 25.0F;
    return parameters;
  }

  ShortRangeFireTest() : FireTest(WithARangeOfTwentyFiveMeters()) {}
};

// 10 m a tick: the round is past 25 m on its third tick.
TEST_F(ShortRangeFireTest, ABulletFiredIntoOpenSpaceExpiresAtTheAmmosMaxRange) {
  const State first = Tick(FiringDownX());
  const State second = Tick(Command{});
  const State third = Tick(Command{});

  EXPECT_EQ(first.bullets_in_flight, 1U);
  EXPECT_EQ(second.bullets_in_flight, 1U);
  EXPECT_EQ(third.bullets_in_flight, 0U);
  EXPECT_TRUE(third.map_impacts.empty());
}

// 700 rounds a minute is a round every 5.14 ticks: some rounds wait five ticks
// and some six, and over a minute's worth of ticks the rate is the rifle's.
class OddRateFireTest : public FireTest {
 protected:
  static Parameters WithSevenHundredRoundsAMinute() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.rounds_per_minute = 700.0F;
    return parameters;
  }

  OddRateFireTest() : FireTest(WithSevenHundredRoundsAMinute()) {}
};

TEST_F(OddRateFireTest, AFireIntervalThatIsNotAWholeNumberOfTicksIsKeptOnAverage) {
  // Six seconds of holding fire: 70 rounds.
  const std::vector<int> fired = FiringTicks(360, Firing());

  EXPECT_EQ(fired.size(), 70U);
  for (std::size_t i = 1; i < fired.size(); ++i) {
    const int gap = fired[i] - fired[i - 1];
    EXPECT_TRUE(gap == 5 || gap == 6) << "round " << i << " came " << gap << " ticks after the one before";
  }
}

// A rifle asked for more rounds a second than there are ticks fires one a tick.
class FasterThanTheTickFireTest : public FireTest {
 protected:
  static Parameters WithSixThousandRoundsAMinute() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.rounds_per_minute = 6000.0F;
    return parameters;
  }

  FasterThanTheTickFireTest() : FireTest(WithSixThousandRoundsAMinute()) {}
};

TEST_F(FasterThanTheTickFireTest, AtMostOneRoundFiresATick) {
  for (int i = 0; i < 30; ++i) {
    EXPECT_EQ(Tick(Firing()).shots.size(), 1U) << "tick " << i;
  }
}

// A magazine of three rounds.
class SmallMagazineFireTest : public FireTest {
 protected:
  static Parameters WithThreeRounds() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.magazine_capacity = 3;
    return parameters;
  }

  SmallMagazineFireTest() : FireTest(WithThreeRounds()) {}
};

TEST_F(SmallMagazineFireTest, HoldingFireFiresTheMagazineAndThenNothing) {
  EXPECT_EQ(FiringTicks(120, Firing()), (std::vector<int>{0, 6, 12}));
}

TEST_F(SmallMagazineFireTest, AnEmptyMagazineFiresNothingHoweverLongItRests) {
  FiringTicks(30, Firing());
  FiringTicks(120, Command{});

  EXPECT_TRUE(FiringTicks(30, Firing()).empty());
}

// A magazine of three rounds that takes half a second, 30 ticks, to reload.
class ReloadTest : public FireTest {
 protected:
  static constexpr int kReloadTicks = 30;

  static Parameters WithThreeRoundsAndAHalfSecondReload() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.magazine_capacity = 3;
    parameters.rifle.reload_seconds = 0.5F;
    return parameters;
  }

  ReloadTest() : FireTest(WithThreeRoundsAndAHalfSecondReload()) {}

  static Command Reloading(bool fire = false) {
    Command command{};
    command.reload = true;
    command.fire = fire;
    return command;
  }

  // How many rounds Alice's magazine holds.
  std::uint8_t Rounds() { return Tick(Command{}).bodies.front().rifle.rounds; }
};

TEST_F(ReloadTest, AReloadFiresNothingForItsDurationAndThenTheMagazineIsFull) {
  FiringTicks(30, Firing());
  ASSERT_EQ(Rounds(), 0);

  // The press and the 29 ticks after it are the half second; a full magazine follows.
  EXPECT_TRUE(Tick(Reloading(/*fire=*/true)).shots.empty());
  EXPECT_TRUE(FiringTicks(kReloadTicks - 1, Firing()).empty());
  EXPECT_EQ(FiringTicks(120, Firing()), (std::vector<int>{0, 6, 12}));
}

TEST_F(ReloadTest, AReloadOfAPartlyEmptyMagazineFillsIt) {
  Tick(Firing());
  ASSERT_EQ(Rounds(), 2);

  Tick(Reloading());
  FiringTicks(kReloadTicks, Command{});

  EXPECT_EQ(Rounds(), 3);
}

TEST_F(ReloadTest, TheMagazineIsNotFullBeforeTheReloadEnds) {
  Tick(Firing());

  Tick(Reloading());
  const State state = Run(kReloadTicks - 2);

  EXPECT_EQ(state.bodies.front().rifle.rounds, 2);
  EXPECT_GT(state.bodies.front().rifle.reload_remaining, 0.0F);
}

TEST_F(ReloadTest, AReloadPressWithAFullMagazineStartsNothingAndFireGoesOn) {
  EXPECT_EQ(Tick(Reloading(/*fire=*/true)).shots.size(), 1U);

  EXPECT_EQ(FiringTicks(12, Firing()), (std::vector<int>{5, 11}));
}

// A client that sends reload on every tick (the sampler sends it on one) still
// reloads once: a reload under way is not started over.
TEST_F(ReloadTest, HoldingReloadForManyTicksStartsOneReloadNotOneEveryTick) {
  FiringTicks(30, Firing());

  FiringTicks(kReloadTicks, Reloading());

  EXPECT_EQ(Rounds(), 3);
}

TEST_F(ReloadTest, FireHeldThroughAReloadFiresOnTheFirstTickAfterIt) {
  Tick(Firing());
  Tick(Reloading());

  EXPECT_EQ(FiringTicks(kReloadTicks + 1, Firing()), (std::vector<int>{kReloadTicks - 1}));
}

// A reload of no time at all still takes the tick it is pressed on.
class InstantReloadTest : public FireTest {
 protected:
  static Parameters WithThreeRoundsAndNoReloadTime() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.magazine_capacity = 3;
    parameters.rifle.reload_seconds = 0.0F;
    return parameters;
  }

  InstantReloadTest() : FireTest(WithThreeRoundsAndNoReloadTime()) {}
};

TEST_F(InstantReloadTest, AReloadOfNoTimeFillsTheMagazineOnTheTickItIsPressed) {
  FiringTicks(30, Firing());
  Command reload{};
  reload.reload = true;
  reload.fire = true;

  const State state = Tick(reload);

  EXPECT_TRUE(state.shots.empty());
  EXPECT_EQ(state.bodies.front().rifle.rounds, 3);
}

}  // namespace
