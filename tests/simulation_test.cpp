#include "augusta/simulation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/weapon.h"

// SimulationWorld on flat ground: players are entities with bodies, moved by
// the commands of each tick.
namespace {

using augusta::ballistics::BodyPart;
using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::simulation::Character;
using augusta::simulation::CharacterHitbox;
using augusta::simulation::Death;
using augusta::simulation::EntityId;
using augusta::simulation::Hit;
using augusta::simulation::PlayerCommand;
using augusta::simulation::State;
using augusta::simulation::World;

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;
constexpr int kSettleTicks = 30;
constexpr int kWalkTicks = 60;

constexpr EntityId kAlice = static_cast<EntityId>(1);
constexpr EntityId kBob = static_cast<EntityId>(2);

// The character of the tests that judge no hit: its eye, standing, relative to
// its feet (ADR-0040), and no hitbox.
const Character kCharacter{.eye = Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};

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
  SimulationTest() : world_(Parameters{}, kTickRate) { EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value()); }

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
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), kCharacter);
  Run(kSettleTicks, {});

  const State state = Run(120, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_LT(Body(state, kAlice).position.x, kWallX);
  EXPECT_GT(Body(state, kAlice).position.x, kWallX - 1.0F);
}

TEST_F(SimulationTest, AnEmptyWorldHasAnEmptyState) { EXPECT_TRUE(world_.Tick({}, kTick).bodies.empty()); }

TEST_F(SimulationTest, AddedPlayersAppearInTheStateOrderedById) {
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kAlice, Vec3(-5.0F, 0.0F, 0.0F), kCharacter);

  const State state = world_.Tick({}, kTick);

  ASSERT_EQ(state.bodies.size(), 2U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
  EXPECT_EQ(state.bodies[1].entity, kBob);
}

TEST_F(SimulationTest, APlayerStartsStandingWhereItSpawned) {
  world_.AddPlayer(kAlice, Vec3(3.0F, 0.0F, -2.0F), kCharacter);
  Run(kSettleTicks, {});

  const State state = world_.Tick({}, kTick);

  EXPECT_NEAR(Body(state, kAlice).position.x, 3.0F, 0.05F);
  EXPECT_NEAR(Body(state, kAlice).position.z, -2.0F, 0.05F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kStanding);
}

TEST_F(SimulationTest, AForwardCommandMovesThatPlayerOnly) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kBob, Vec3(0.0F, 0.0F, 10.0F), kCharacter);
  Run(kSettleTicks, {});
  const float start = Body(world_.Tick({}, kTick), kAlice).position.x;
  const float bob_start = Body(world_.Tick({}, kTick), kBob).position.x;

  const State state = Run(kWalkTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_GT(Body(state, kAlice).position.x, start + 2.0F);
  EXPECT_NEAR(Body(state, kBob).position.x, bob_start, 0.01F);
}

TEST_F(SimulationTest, StanceCommandsChangeTheStanceAndTheSpeed) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
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
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  Run(kSettleTicks, {});
  Run(kWalkTicks / 2,
      {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F), Stance::kCrouching)}});

  const State state = Run(kWalkTicks / 2, {});

  EXPECT_NEAR(HorizontalSpeed(Body(state, kAlice)), 0.0F, 0.01F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kCrouching);
}

TEST_F(SimulationTest, ACommandForAPlayerNotInTheWorldIsIgnored) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);

  const State state = Run(kSettleTicks, {PlayerCommand{.entity = kBob, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
}

TEST_F(SimulationTest, ARemovedPlayerLeavesTheState) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kCharacter);
  Run(kSettleTicks, {});

  world_.RemovePlayer(kAlice);
  const State state = world_.Tick({}, kTick);

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kBob);
}

TEST_F(SimulationTest, RemovingAPlayerNotInTheWorldChangesNothing) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);

  world_.RemovePlayer(kBob);

  EXPECT_EQ(world_.Tick({}, kTick).bodies.size(), 1U);
}

TEST_F(SimulationTest, AddingAPlayerTwiceKeepsOne) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kAlice, Vec3(9.0F, 0.0F, 0.0F), kCharacter);

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
  explicit FireTest(const Parameters& parameters = WithTheTestRifle()) : world_(parameters, kTickRate) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
  }

  void SetUp() override {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), kCharacter);
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
  world_.AddPlayer(kBob, Vec3(5.0F, 0.5F, 0.0F), kCharacter);

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

// The test rifle with recoil (US-09): a pattern of three kicks, each a whole
// count of the angle grid's step so their sums are exact, halved in ADS, and an
// offset that recovers by 0.00625 rad a tick. Its reload takes half a second.
class RecoilTest : public FireTest {
 protected:
  static constexpr float kFirstPitch = 1.0F / 64.0F;
  static constexpr float kSecondPitch = 1.0F / 64.0F;
  static constexpr float kSecondYaw = 1.0F / 256.0F;
  static constexpr float kThirdPitch = 1.0F / 32.0F;
  static constexpr float kThirdYaw = -1.0F / 128.0F;
  static constexpr float kRecoveryPerTick = 0.00625F;
  // Half a grid step, and the rounding of the recovery itself.
  static constexpr float kAngleTolerance = 1e-6F;
  // The view every burst is fired from.
  static constexpr float kViewYaw = 0.75F;
  static constexpr float kViewPitch = -0.25F;

  static Parameters WithRecoil() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.recoil_pattern = {{.pitch = kFirstPitch, .yaw = 0.0F},
                                       {.pitch = kSecondPitch, .yaw = kSecondYaw},
                                       {.pitch = kThirdPitch, .yaw = kThirdYaw}};
    parameters.rifle.recoil_recovery_per_second = kRecoveryPerTick * kTickRate;
    parameters.rifle.ads_recoil_scale = 0.5F;
    parameters.rifle.reload_seconds = 0.5F;
    return parameters;
  }

  RecoilTest() : FireTest(WithRecoil()) {}

  // Fire held from the hip, or in ADS, looking along the tests' view.
  static Command FiringFromTheView(bool ads = false) {
    Command command = Firing();
    command.yaw = kViewYaw;
    command.pitch = kViewPitch;
    command.ads = ads;
    return command;
  }

  // Holds command until it has fired the given number of rounds; returns their
  // Shots. The trigger is still held when it returns.
  std::vector<augusta::simulation::Shot> Burst(int rounds, const Command& command = FiringFromTheView()) {
    constexpr int kTicksPerRound = 6;
    std::vector<augusta::simulation::Shot> shots;
    for (int i = 0; i < rounds * kTicksPerRound && std::cmp_less(shots.size(), rounds); ++i) {
      const State state = Tick(command);
      shots.insert(shots.end(), state.shots.begin(), state.shots.end());
    }
    EXPECT_EQ(shots.size(), static_cast<std::size_t>(rounds));
    shots.resize(static_cast<std::size_t>(rounds));
    return shots;
  }

  // How far Alice's rifle points off her view after a tick with no command.
  augusta::weapon::RecoilOffset RecoilAfterARestingTick() { return Tick(Command{}).bodies.front().rifle.recoil; }
};

TEST_F(RecoilTest, EachRoundOfABurstLeavesOffTheViewByTheKicksOfTheRoundsBeforeIt) {
  const auto shots = Burst(4);

  EXPECT_EQ(shots[0].yaw, kViewYaw);
  EXPECT_EQ(shots[0].pitch, kViewPitch);
  EXPECT_EQ(shots[1].yaw, kViewYaw);
  EXPECT_EQ(shots[1].pitch, kViewPitch + kFirstPitch);
  EXPECT_EQ(shots[2].yaw, kViewYaw + kSecondYaw);
  EXPECT_EQ(shots[2].pitch, kViewPitch + kFirstPitch + kSecondPitch);
  EXPECT_EQ(shots[3].yaw, kViewYaw + kSecondYaw + kThirdYaw);
  EXPECT_EQ(shots[3].pitch, kViewPitch + kFirstPitch + kSecondPitch + kThirdPitch);
}

TEST_F(RecoilTest, PastThePatternsLastKickTheLastRepeats) {
  const auto shots = Burst(6);

  EXPECT_EQ(shots[5].yaw - shots[4].yaw, kThirdYaw);
  EXPECT_EQ(shots[5].pitch - shots[4].pitch, kThirdPitch);
  EXPECT_EQ(shots[4].yaw - shots[3].yaw, kThirdYaw);
  EXPECT_EQ(shots[4].pitch - shots[3].pitch, kThirdPitch);
}

// US-09: the pattern is the same every burst, and the aim settles back between them.
TEST_F(RecoilTest, ASecondBurstFromTheSameViewAfterTheRecoilHasRecoveredLeavesAsTheFirstDid) {
  const auto first = Burst(4);
  Run(60);

  const auto second = Burst(4);

  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(second[i].yaw, first[i].yaw) << "round " << i;
    EXPECT_EQ(second[i].pitch, first[i].pitch) << "round " << i;
  }
}

TEST_F(RecoilTest, ABurstFiredBeforeTheLastHasRecoveredStartsThePatternOverOnTopOfWhatIsLeft) {
  Burst(3);
  // One tick off the trigger: the burst is over, and little of it has recovered.
  Tick(Command{});

  const auto shots = Burst(2);

  const float left = shots[0].pitch - kViewPitch;
  EXPECT_GT(left, 0.0F);
  EXPECT_LT(left, kFirstPitch + kSecondPitch + kThirdPitch);
  EXPECT_EQ(shots[1].pitch - shots[0].pitch, kFirstPitch);
  EXPECT_EQ(shots[1].yaw - shots[0].yaw, 0.0F);
}

TEST_F(RecoilTest, AimingDownSightsScalesEveryKick) {
  const auto shots = Burst(4, FiringFromTheView(/*ads=*/true));

  EXPECT_EQ(shots[0].pitch, kViewPitch);
  EXPECT_EQ(shots[1].pitch, kViewPitch + (kFirstPitch / 2.0F));
  EXPECT_EQ(shots[2].yaw, kViewYaw + (kSecondYaw / 2.0F));
  EXPECT_EQ(shots[3].yaw, kViewYaw + ((kSecondYaw + kThirdYaw) / 2.0F));
  EXPECT_EQ(shots[3].pitch, kViewPitch + ((kFirstPitch + kSecondPitch + kThirdPitch) / 2.0F));
}

TEST_F(RecoilTest, TheRecoilHoldsWhileTheTriggerIsHeldAndRecoversAtTheParametersRateOnceItIsNot) {
  Burst(1);
  // Held between two rounds, the trigger keeps what the first round kicked.
  EXPECT_EQ(Tick(FiringFromTheView()).bodies.front().rifle.recoil.pitch, kFirstPitch);

  EXPECT_NEAR(RecoilAfterARestingTick().pitch, kFirstPitch - kRecoveryPerTick, kAngleTolerance);
  EXPECT_NEAR(RecoilAfterARestingTick().pitch, kFirstPitch - (2.0F * kRecoveryPerTick), kAngleTolerance);
  EXPECT_EQ(RecoilAfterARestingTick(), augusta::weapon::RecoilOffset{});
  EXPECT_EQ(RecoilAfterARestingTick(), augusta::weapon::RecoilOffset{});
}

// The rate is the offset's own, not each angle's: it shrinks along its line.
TEST_F(RecoilTest, ARecoveringOffsetShrinksStraightTowardZero) {
  Burst(2);
  const float pitch = kFirstPitch + kSecondPitch;
  const float length = std::hypot(pitch, kSecondYaw);

  const augusta::weapon::RecoilOffset recoil = RecoilAfterARestingTick();

  EXPECT_NEAR(std::hypot(recoil.pitch, recoil.yaw), length - kRecoveryPerTick, kAngleTolerance);
  EXPECT_NEAR(recoil.yaw / recoil.pitch, kSecondYaw / pitch, 1e-3F);
}

// A rifle being reloaded pulls no trigger, whatever the fire control does.
TEST_F(RecoilTest, AReloadEndsTheBurstThoughFireIsStillHeld) {
  Burst(3);
  Command reload = FiringFromTheView();
  reload.reload = true;
  Tick(reload);
  // Fire held through the rest of the reload's half second, and on.
  EXPECT_TRUE(FiringTicks(29, FiringFromTheView()).empty());

  const auto shots = Burst(2);

  EXPECT_EQ(shots[0].yaw, kViewYaw);
  EXPECT_EQ(shots[0].pitch, kViewPitch);
  EXPECT_EQ(shots[1].pitch, kViewPitch + kFirstPitch);
}

TEST_F(RecoilTest, AShotOffTheViewIsStillOnTheGridItTravelsOn) {
  Command command = Firing();
  command.yaw = 0.1234567F;
  command.pitch = -0.0654321F;

  const auto shots = Burst(3, command);

  EXPECT_EQ(shots[2].yaw, augusta::math::SnapAngle(shots[2].yaw));
  EXPECT_EQ(shots[2].pitch, augusta::math::SnapAngle(shots[2].pitch));
  EXPECT_NEAR(shots[2].pitch, -0.0654321F + kFirstPitch + kSecondPitch, kAngleTolerance);
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

// Where a shot at each part of the target character is aimed, above its feet, standing.
constexpr float kHeadHeight = 1.65F;
constexpr float kTorsoHeight = 1.2F;
constexpr float kLegsHeight = 0.45F;

// A character 1.8 m tall that sees from inside its head: a head, a torso and
// legs on its axis, and a right arm (a limb) beside the torso, at +X when it
// faces yaw 0.
Character Target() {
  return Character{.eye = Vec3(0.0F, 1.6F, 0.0F),
                   .hitboxes = {Box(BodyPart::kHead, Vec3(-0.1F, 1.5F, -0.1F), Vec3(0.1F, 1.8F, 0.1F)),
                                Box(BodyPart::kTorso, Vec3(-0.2F, 0.9F, -0.1F), Vec3(0.2F, 1.5F, 0.1F)),
                                Box(BodyPart::kLimb, Vec3(-0.2F, 0.0F, -0.1F), Vec3(0.2F, 0.9F, 0.1F)),
                                Box(BodyPart::kLimb, Vec3(0.3F, 0.9F, -0.1F), Vec3(0.4F, 1.5F, 0.1F))}};
}

// Alice and Bob, 10 m apart, on the floor: Alice, at the origin, shoots at Bob,
// down -Z where a view of yaw 0 looks. A round reaches Bob on the tick it is
// fired. Bob starts with 100 of health, and a round takes 50 off it at the
// head, 20 at the torso and 10 at a limb.
class HitTest : public ::testing::Test {
 protected:
  static constexpr float kStartingHealth = 100.0F;
  static constexpr float kHeadDamage = 50.0F;
  static constexpr float kTorsoDamage = 20.0F;
  static constexpr float kLimbDamage = 10.0F;

  static Parameters WithDamage() {
    Parameters parameters = WithTheTestRifle();
    parameters.ammo.damage = {.head = kHeadDamage, .torso = kTorsoDamage, .limb = kLimbDamage};
    parameters.starting_health = kStartingHealth;
    return parameters;
  }

  HitTest() : world_(WithDamage(), kTickRate) { EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value()); }

  void SetUp() override {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), Target());
    world_.AddPlayer(kBob, Vec3(0.0F, 0.5F, -10.0F), Target());
    bob_ = Command{};
    Wait(kSettleTicks);
  }

  // A tick on which Alice does command and Bob what he was last told.
  State Tick(const Command& command) {
    return world_.Tick(
        {PlayerCommand{.entity = kAlice, .command = command}, PlayerCommand{.entity = kBob, .command = bob_}}, kTick);
  }

  // Ticks the given number of times with Alice idle, and returns every hit of them.
  std::vector<Hit> Wait(int ticks) {
    std::vector<Hit> hits;
    for (int i = 0; i < ticks; ++i) {
      const State state = Tick(Command{});
      hits.insert(hits.end(), state.hits.begin(), state.hits.end());
    }
    return hits;
  }

  static const augusta::simulation::EntityState& Entity(const State& state, EntityId entity) {
    for (const auto& entry : state.bodies) {
      if (entry.entity == entity) {
        return entry;
      }
    }
    ADD_FAILURE() << "player not in state";
    return state.bodies.front();
  }

  // A one-tick press of fire by Alice, aimed at the point offset from Bob's
  // feet as the State of a tick shows them, which is the view it reports.
  Command FiringAt(const Vec3& offset) {
    const State state = Tick(Command{});
    const Vec3 eye = Entity(state, kAlice).body.position + Target().eye;
    const Vec3 aim = Entity(state, kBob).body.position + offset - eye;
    Command command = Firing();
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.view_tick = state.tick;
    return command;
  }

  // Alice fires one round at the point offset from Bob's feet; returns the hits
  // of that tick and of those it takes the rifle to be ready again.
  std::vector<Hit> ShootAt(const Vec3& offset) { return Shoot(FiringAt(offset)); }

  // As ShootAt, on a Command that reports the view of view_tick and view_fraction.
  std::vector<Hit> ShootAt(const Vec3& offset, std::uint64_t view_tick, float view_fraction) {
    Command command = FiringAt(offset);
    command.view_tick = view_tick;
    command.view_fraction = view_fraction;
    return Shoot(command);
  }

  // Alice does command for a tick; returns the hits of that tick and of those
  // it takes the rifle to be ready again.
  std::vector<Hit> Shoot(const Command& command) {
    std::vector<Hit> hits = Tick(command).hits;
    const std::vector<Hit> later = Wait(6);
    hits.insert(hits.end(), later.begin(), later.end());
    return hits;
  }

  World world_;
  // What Bob is told to do on every tick.
  Command bob_;
};

TEST_F(HitTest, APlayerStartsAMatchWithTheParametersStartingHealth) {
  const State state = Tick(Command{});

  EXPECT_EQ(Entity(state, kAlice).health, kStartingHealth);
  EXPECT_EQ(Entity(state, kBob).health, kStartingHealth);
}

TEST_F(HitTest, AHitNamesItsShooterItsTargetAndTheBodyPartItCrossed) {
  const std::vector<Hit> hits = ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);
  EXPECT_EQ(hits[0].part, BodyPart::kTorso);
}

TEST_F(HitTest, AHitTakesTheAmmosDamageForItsBodyPartOffTheTargetsHealthAlone) {
  const std::vector<Hit> head = ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  const std::vector<Hit> torso = ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));
  const std::vector<Hit> limb = ShootAt(Vec3(0.0F, kLegsHeight, 0.0F));

  ASSERT_TRUE(head.size() == 1U && torso.size() == 1U && limb.size() == 1U);
  EXPECT_EQ(head[0].part, BodyPart::kHead);
  EXPECT_EQ(head[0].damage, kHeadDamage);
  EXPECT_EQ(head[0].health, kStartingHealth - kHeadDamage);
  EXPECT_EQ(torso[0].part, BodyPart::kTorso);
  EXPECT_EQ(torso[0].damage, kTorsoDamage);
  EXPECT_EQ(torso[0].health, kStartingHealth - kHeadDamage - kTorsoDamage);
  EXPECT_EQ(limb[0].part, BodyPart::kLimb);
  EXPECT_EQ(limb[0].damage, kLimbDamage);
  EXPECT_EQ(limb[0].health, kStartingHealth - kHeadDamage - kTorsoDamage - kLimbDamage);
  const State state = Tick(Command{});
  EXPECT_EQ(Entity(state, kBob).health, limb[0].health);
  EXPECT_EQ(Entity(state, kAlice).health, kStartingHealth);
}

TEST_F(HitTest, HealthStopsAtZeroAndReachingItIsReportedOnce) {
  // Head, torso, torso: 100, 50, 30, 10. The fourth would take it below zero.
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));
  const std::vector<Hit> third = ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));
  const std::vector<Hit> fourth = ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));

  ASSERT_TRUE(third.size() == 1U && fourth.size() == 1U);
  EXPECT_EQ(third[0].health, 10.0F);
  EXPECT_FALSE(third[0].reached_zero);
  EXPECT_EQ(fourth[0].damage, kHeadDamage);
  EXPECT_EQ(fourth[0].health, 0.0F);
  EXPECT_TRUE(fourth[0].reached_zero);
}

// Bob, at 50 of health after a head shot, dies of the second one.
TEST_F(HitTest, APlayerDiesOnTheTickAHitTakesItsHealthToZero) {
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  const State before = Tick(Command{});
  ASSERT_EQ(before.alive, (std::vector<EntityId>{kAlice, kBob}));
  ASSERT_TRUE(before.deaths.empty());

  const State state = Tick(FiringAt(Vec3(0.0F, kHeadHeight, 0.0F)));

  ASSERT_EQ(state.hits.size(), 1U);
  ASSERT_TRUE(state.hits[0].reached_zero);
  ASSERT_EQ(state.shots.size(), 1U);
  ASSERT_EQ(state.deaths.size(), 1U);
  const Death& death = state.deaths[0];
  EXPECT_EQ(death.victim, kBob);
  EXPECT_EQ(death.killer, kAlice);
  EXPECT_EQ(death.part, BodyPart::kHead);
  EXPECT_EQ(death.yaw, state.shots[0].yaw);
  EXPECT_EQ(death.pitch, state.shots[0].pitch);
  EXPECT_EQ(state.alive, std::vector<EntityId>{kAlice});
  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
}

// A death is told on the tick it happens, and never again.
TEST_F(HitTest, ADeadPlayerStaysDeadAndOutOfTheStateForTheRestOfTheMatch) {
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  bob_ = Walking(Vec3(1.0F, 0.0F, 0.0F));

  for (int i = 0; i < 120; ++i) {
    const State state = Tick(Command{});
    EXPECT_TRUE(state.deaths.empty()) << "tick " << i;
    EXPECT_EQ(state.alive, std::vector<EntityId>{kAlice}) << "tick " << i;
    ASSERT_EQ(state.bodies.size(), 1U) << "tick " << i;
  }
}

// Carol stands 5 m behind Bob, on the line Alice shoots along. Alice aims at
// Bob's torso while he lives, and fires once he has died.
TEST_F(HitTest, ABulletAimedThroughWhereADeadPlayerStoodHitsWhatIsBehindIt) {
  constexpr EntityId kCarol = static_cast<EntityId>(3);
  world_.AddPlayer(kCarol, Vec3(0.0F, 0.5F, -15.0F), Target());
  Wait(kSettleTicks);
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  const Command aimed = FiringAt(Vec3(0.0F, kTorsoHeight, 0.0F));
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));

  const std::vector<Hit> hits = Shoot(aimed);

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].target, kCarol);
}

TEST_F(HitTest, ADeadPlayersFireAndMovementChangeNothing) {
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  // Bob walks at Alice, turned to face her, firing.
  bob_ = Firing();
  bob_.movement.direction = Vec3(0.0F, 0.0F, 1.0F);
  bob_.yaw = std::numbers::pi_v<float>;

  for (int i = 0; i < 120; ++i) {
    const State state = Tick(Command{});
    EXPECT_TRUE(state.shots.empty()) << "tick " << i;
    EXPECT_TRUE(state.hits.empty()) << "tick " << i;
    EXPECT_EQ(state.bodies.size(), 1U) << "tick " << i;
  }
}

// A dead body is no obstacle: Alice walks on through where Bob fell.
TEST_F(HitTest, ADeadPlayersBodyNoLongerBlocksMovement) {
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));

  State state;
  for (int i = 0; i < 360; ++i) {
    state = Tick(Walking(Vec3(0.0F, 0.0F, -1.0F)));
  }

  EXPECT_LT(Entity(state, kAlice).body.position.z, -15.0F);
}

// Carol stands 60 m down the line, 6 ticks of flight from Bob: Bob fires at
// her, and Alice kills him on the next tick, before his round arrives.
TEST_F(HitTest, ABulletADeadPlayerFiredWhileAliveStillHits) {
  constexpr EntityId kCarol = static_cast<EntityId>(3);
  world_.AddPlayer(kCarol, Vec3(0.0F, 0.5F, -70.0F), Target());
  Wait(kSettleTicks);
  ShootAt(Vec3(0.0F, kHeadHeight, 0.0F));
  bob_ = Firing();
  const Command kill = FiringAt(Vec3(0.0F, kHeadHeight, 0.0F));
  bob_ = Command{};

  std::vector<Hit> hits = Tick(kill).hits;
  const std::vector<Hit> later = Wait(10);
  hits.insert(hits.end(), later.begin(), later.end());

  ASSERT_EQ(hits.size(), 2U);
  EXPECT_EQ(hits[0].target, kBob);
  EXPECT_TRUE(hits[0].reached_zero);
  EXPECT_EQ(hits[1].shooter, kBob);
  EXPECT_EQ(hits[1].target, kCarol);
}

TEST_F(HitTest, HealthDoesNotComeBackWithTime) {
  ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));

  Wait(300);

  EXPECT_EQ(Entity(Tick(Command{}), kBob).health, kStartingHealth - kTorsoDamage);
}

TEST_F(HitTest, AShotPastTheTargetHitsNoOne) {
  EXPECT_TRUE(ShootAt(Vec3(1.0F, kTorsoHeight, 0.0F)).empty());
  EXPECT_TRUE(ShootAt(Vec3(0.0F, 2.0F, 0.0F)).empty());
}

// Crouched, a body 2.1 m tall standing is 1.3 m tall, and its hitboxes with it.
TEST_F(HitTest, ACrouchedTargetsHitboxesAreLoweredWithItsBody) {
  bob_.movement.desired_stance = Stance::kCrouching;
  Wait(kSettleTicks);

  EXPECT_TRUE(ShootAt(Vec3(0.0F, kHeadHeight, 0.0F)).empty());
  const std::vector<Hit> lowered = ShootAt(Vec3(0.0F, kHeadHeight * 1.3F / 2.1F, 0.0F));

  ASSERT_EQ(lowered.size(), 1U);
  EXPECT_EQ(lowered[0].part, BodyPart::kHead);
}

// Its right arm is at +X while it faces yaw 0, and at -X once it has turned half a turn.
TEST_F(HitTest, ATargetsHitboxesTurnWithWhereItFaces) {
  const Vec3 right_of_it(0.35F, kTorsoHeight, 0.0F);
  const Vec3 left_of_it(-0.35F, kTorsoHeight, 0.0F);
  EXPECT_EQ(ShootAt(right_of_it).size(), 1U);
  EXPECT_TRUE(ShootAt(left_of_it).empty());

  bob_.yaw = std::numbers::pi_v<float>;
  Wait(1);

  EXPECT_TRUE(ShootAt(right_of_it).empty());
  const std::vector<Hit> turned = ShootAt(left_of_it);
  ASSERT_EQ(turned.size(), 1U);
  EXPECT_EQ(turned[0].part, BodyPart::kLimb);
  EXPECT_EQ(Entity(Tick(Command{}), kBob).yaw, augusta::math::SnapAngle(std::numbers::pi_v<float>));
}

// Bob crouches on one tick. A view between the tick before and that one shows
// him as the nearer of the two has him, as a client does.
TEST_F(HitTest, AViewBetweenTwoStancesIsJudgedInTheStanceOfTheNearerTick) {
  bob_.movement.desired_stance = Stance::kCrouching;
  std::uint64_t crouched = 0;
  for (int i = 0; i < kSettleTicks && crouched == 0; ++i) {
    const State state = Tick(Command{});
    if (Entity(state, kBob).body.stance == Stance::kCrouching) {
      crouched = state.tick;
    }
  }
  ASSERT_NE(crouched, 0U);
  const Vec3 standing_head(0.0F, kHeadHeight, 0.0F);

  const std::vector<Hit> nearer_standing = ShootAt(standing_head, crouched - 1, 0.4F);
  const std::vector<Hit> nearer_crouched = ShootAt(standing_head, crouched - 1, 0.6F);

  ASSERT_EQ(nearer_standing.size(), 1U);
  EXPECT_EQ(nearer_standing[0].part, BodyPart::kHead);
  EXPECT_TRUE(nearer_crouched.empty());
}

// Bob faces just short of half a turn one way, then just short of it the
// other: halfway between the two he faces half a turn, his right arm at -X, and
// not yaw 0, which is the long way round.
TEST_F(HitTest, AViewBetweenTwoFacingsTurnsTheHitboxesAlongTheShorterArc) {
  const Vec3 left_of_it(-0.35F, kTorsoHeight, 0.0F);
  bob_.yaw = 3.0F;
  Wait(2);
  bob_.yaw = -3.0F;
  const std::uint64_t turned = Tick(Command{}).tick;

  const std::vector<Hit> hits = ShootAt(left_of_it, turned - 1, 0.5F);

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].part, BodyPart::kLimb);
}

TEST_F(HitTest, APlayerWithNoCommandKeepsWhereItFaced) {
  bob_.yaw = 1.25F;
  Wait(1);

  const State state = world_.Tick({}, kTick);

  EXPECT_EQ(Entity(state, kBob).yaw, 1.25F);
}

// The eye is inside the shooter's own head hitbox, which every round leaves through.
TEST_F(HitTest, AShooterFiringForwardWhileMovingNeverHitsItself) {
  Command command = Firing();
  command.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
  // Up and away from Bob, along the way Alice walks.
  command.pitch = 0.5F;

  for (int i = 0; i < 60; ++i) {
    const State state = Tick(command);
    EXPECT_TRUE(state.hits.empty()) << "tick " << i;
    EXPECT_EQ(Entity(state, kAlice).health, kStartingHealth);
  }
}

// Carol stands 5 m behind Bob, on the line Alice shoots along.
TEST_F(HitTest, WithTwoTargetsInLineOnlyTheNearerIsHit) {
  constexpr EntityId kCarol = static_cast<EntityId>(3);
  world_.AddPlayer(kCarol, Vec3(0.0F, 0.5F, -15.0F), Target());
  Wait(kSettleTicks);

  const std::vector<Hit> hits = ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].target, kBob);
  EXPECT_EQ(Entity(Tick(Command{}), kCarol).health, kStartingHealth);
}

TEST_F(HitTest, ATargetBehindAWallIsNotHitAndTheWallIs) {
  constexpr float kWallZ = -5.0F;
  ASSERT_TRUE(world_
                  .AddCollisionMesh(CollisionMesh{.points = {Vec3(-20.0F, 0.0F, kWallZ), Vec3(-20.0F, 5.0F, kWallZ),
                                                             Vec3(20.0F, 5.0F, kWallZ), Vec3(20.0F, 0.0F, kWallZ)},
                                                  .indices = {0, 1, 2, 0, 2, 3}})
                  .has_value());

  const State state = Tick(FiringAt(Vec3(0.0F, kTorsoHeight, 0.0F)));

  EXPECT_TRUE(state.hits.empty());
  ASSERT_EQ(state.map_impacts.size(), 1U);
  EXPECT_NEAR(state.map_impacts[0].z, kWallZ, 0.01F);
  EXPECT_EQ(Entity(state, kBob).health, kStartingHealth);
}

TEST_F(SimulationTest, AStateNamesItsTickFromOne) {
  EXPECT_EQ(world_.Tick({}, kTick).tick, 1U);
  EXPECT_EQ(world_.Tick({}, kTick).tick, 2U);
}

// What the Shooter's delay's cap of 250 ms needs, and no more (ADR-0044).
TEST(HitboxHistoryTest, TheHistoryHoldsTheCapsWorthOfTicksAtTheTickRate) {
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(60), 15U);
  // A cap that is not a whole number of ticks takes the tick that covers it.
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(30), 8U);
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(255), 64U);
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(1), 1U);
}

// A character that is one upright sliver, 2 cm across and 1.8 m tall: a round
// strikes it only where it is judged to be, to the centimetre.
Character Sliver() {
  return Character{.eye = Vec3(0.0F, 1.6F, 0.0F),
                   .hitboxes = {Box(BodyPart::kTorso, Vec3(-0.01F, 0.0F, -0.01F), Vec3(0.01F, 1.8F, 0.01F))}};
}

// Alice, at the origin, shoots down -Z at Bob, who walks across her view along
// +X, 5 cm a tick, distance away: 8 m unless a test says otherwise, within the
// 10 m a round flies on the tick it is fired. The test is Alice's client: it
// keeps where the State of each tick puts Bob, aims at where a view of them
// shows him, and reports that view with its Command (ADR-0044).
class LagCompensationTest : public ::testing::Test {
 protected:
  static constexpr float kAimHeight = 1.2F;
  // The Shooter's delay's cap, 250 ms, in ticks of kTick.
  static constexpr std::uint32_t kCapTicks = 15;

  explicit LagCompensationTest(float distance = 8.0F) : world_(WithTheTestRifle(), kTickRate) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), Sliver());
    world_.AddPlayer(kBob, Vec3(-3.0F, 0.5F, -distance), Sliver());
  }

  void SetUp() override {
    for (int i = 0; i < kSettleTicks; ++i) {
      Tick(Command{});
    }
    bob_ = Walking(Vec3(1.0F, 0.0F, 0.0F));
    // More ticks of walking than any view here looks back.
    for (int i = 0; i < 60; ++i) {
      Tick(Command{});
    }
  }

  // A tick on which Alice does command and Bob what he was last told.
  State Tick(const Command& command) {
    State state = world_.Tick(
        {PlayerCommand{.entity = kAlice, .command = command}, PlayerCommand{.entity = kBob, .command = bob_}}, kTick);
    for (const auto& entry : state.bodies) {
      (entry.entity == kBob ? seen_[state.tick] : alice_) = entry.body.position;
    }
    last_tick_ = state.tick;
    return state;
  }

  // The tick Alice's next Command is taken in on.
  [[nodiscard]] std::uint64_t Next() const { return last_tick_ + 1; }

  // Where a view of tick and fraction shows Bob's feet: between where the
  // States of tick and of the one after it put them.
  [[nodiscard]] Vec3 BobAt(std::uint64_t tick, float fraction = 0.0F) const {
    return fraction == 0.0F ? seen_.at(tick) : augusta::math::Lerp(seen_.at(tick), seen_.at(tick + 1), fraction);
  }

  // Alice taps fire on the next tick, aimed at kAimHeight above feet, on a
  // Command that reports the view of view_tick and view_fraction; returns the
  // hits of that tick and of the six after it.
  std::vector<Hit> Shoot(const Vec3& feet, std::uint64_t view_tick, float view_fraction) {
    const Vec3 aim = feet + Vec3(0.0F, kAimHeight, 0.0F) - (alice_ + Sliver().eye);
    Command command = Firing();
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.view_tick = view_tick;
    command.view_fraction = view_fraction;
    std::vector<Hit> hits = Tick(command).hits;
    for (int i = 0; i < 6; ++i) {
      const State state = Tick(Command{});
      hits.insert(hits.end(), state.hits.begin(), state.hits.end());
    }
    return hits;
  }

  World world_;
  Command bob_{};
  Vec3 alice_{};
  // Where the State of each tick put Bob's feet.
  std::map<std::uint64_t, Vec3> seen_;
  std::uint64_t last_tick_ = 0;
};

TEST_F(LagCompensationTest, ARoundIsJudgedAgainstHitboxesInterpolatedAtTheFractionItsViewReports) {
  std::uint64_t view = Next() - 6;
  const std::vector<Hit> hits = Shoot(BobAt(view, 0.5F), view, 0.5F);
  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);

  // Aimed the same, a view that names either of the two ticks misses: Bob is
  // 2.5 cm to one side or the other of where the round passes.
  view = Next() - 6;
  EXPECT_TRUE(Shoot(BobAt(view, 0.5F), view, 0.0F).empty());
  view = Next() - 6;
  EXPECT_TRUE(Shoot(BobAt(view, 0.5F), view + 1, 0.0F).empty());
}

TEST_F(LagCompensationTest, ATargetThatHasSinceMovedAwayIsStillHitWhereItWasSeen) {
  const std::uint64_t view = Next() - 10;
  // Bob has walked most of half a meter since.
  ASSERT_GT(BobAt(Next() - 1).x - BobAt(view).x, 0.4F);

  EXPECT_EQ(Shoot(BobAt(view), view, 0.0F).size(), 1U);
}

TEST_F(LagCompensationTest, ARoundAimedWhereTheTargetIsNowMissesOnAnOlderView) {
  EXPECT_TRUE(Shoot(BobAt(Next() - 1), Next() - 10, 0.0F).empty());
}

TEST_F(LagCompensationTest, AViewAsOldAsTheCapIsJudgedWhereTheTargetWasThen) {
  const std::uint64_t view = Next() - kCapTicks;

  EXPECT_EQ(Shoot(BobAt(view), view, 0.0F).size(), 1U);
}

// A shooter with a delay past the cap still fires, and is judged against the
// oldest view the cap allows, not against the one it reports.
TEST_F(LagCompensationTest, AViewOlderThanTheCapIsJudgedAtTheCapNotRefused) {
  EXPECT_EQ(Shoot(BobAt(Next() - kCapTicks), Next() - 40, 0.0F).size(), 1U);
  EXPECT_TRUE(Shoot(BobAt(Next() - 40), Next() - 40, 0.0F).empty());
  // A view of no tick at all, as of a client that reports none.
  EXPECT_EQ(Shoot(BobAt(Next() - kCapTicks), 0, 0.0F).size(), 1U);
}

// No client has been shown more than the last tick's State.
TEST_F(LagCompensationTest, AViewNewerThanTheLastStateIsJudgedAtTheLastState) {
  EXPECT_EQ(Shoot(BobAt(Next() - 1), Next() + 100, 0.0F).size(), 1U);
  EXPECT_EQ(Shoot(BobAt(Next() - 1), Next() - 1, 0.5F).size(), 1U);
}

TEST_F(LagCompensationTest, AFractionOutsideZeroToOneIsHeldWithinIt) {
  std::uint64_t view = Next() - 6;
  EXPECT_EQ(Shoot(BobAt(view + 1), view, 7.0F).size(), 1U);
  view = Next() - 6;
  EXPECT_EQ(Shoot(BobAt(view), view, -3.0F).size(), 1U);
}

// Bob walks 35 m away: at 10 m a tick, a round crosses his path on its fourth
// tick, three ticks after it is fired.
class LongShotTest : public LagCompensationTest {
 protected:
  static constexpr std::uint32_t kFlightTicks = 3;

  LongShotTest() : LagCompensationTest(35.0F) {}
};

// The lead is judged on the shooter's screen: by the time the round arrives,
// its view shows Bob three ticks further along than when it fired.
TEST_F(LongShotTest, ABulletInFlightKeepsItsShootersDelaySoACorrectlyLedMovingTargetIsHit) {
  const std::uint64_t view = Next() - 8;

  const std::vector<Hit> hits = Shoot(BobAt(view + kFlightTicks), view, 0.0F);

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].target, kBob);
}

// With the freshest view there is, a round is judged a tick behind the
// present: Bob is long past where that lead put it.
TEST_F(LongShotTest, TheSameLeadJudgedAgainstThePresentMisses) {
  const std::uint64_t view = Next() - 8;

  EXPECT_TRUE(Shoot(BobAt(view + kFlightTicks), Next() - 1, 0.0F).empty());
}

// A bullet flies on after its shooter has left the Match, and still does its damage.
TEST_F(HitTest, ABulletWhoseShooterLeftStillHits) {
  // 10 m a tick and Bob 40 m away: the round is still flying when Alice leaves.
  world_.RemovePlayer(kBob);
  world_.AddPlayer(kBob, Vec3(0.0F, 0.5F, -40.0F), Target());
  Wait(kSettleTicks);
  ASSERT_TRUE(Tick(FiringAt(Vec3(0.0F, kTorsoHeight, 0.0F))).hits.empty());

  world_.RemovePlayer(kAlice);
  std::vector<Hit> hits;
  for (int i = 0; i < 6; ++i) {
    const State state = world_.Tick({}, kTick);
    hits.insert(hits.end(), state.hits.begin(), state.hits.end());
  }

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);
}

}  // namespace
