#include "augusta/simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"
#include "clips_fixture.h"

// SimulationWorld on flat ground: players are entities with bodies, moved by
// the commands of each tick.
namespace {

using augusta::body_part::BodyPart;
using augusta::command::Command;
using augusta::hitbox::CharacterHitbox;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::physics::Stance;
using augusta::simulation::Character;
using augusta::simulation::Death;
using augusta::simulation::EntityId;
using augusta::simulation::Hit;
using augusta::simulation::PlayerCommand;
using augusta::simulation::State;
using augusta::simulation::TickResult;
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
      state = world_.Tick(commands, kTick).state;
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

// Requirements: US-04
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

TEST_F(SimulationTest, AnEmptyWorldHasAnEmptyState) { EXPECT_TRUE(world_.Tick({}, kTick).state.bodies.empty()); }

TEST_F(SimulationTest, AddedPlayersAppearInTheStateOrderedById) {
  world_.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kAlice, Vec3(-5.0F, 0.0F, 0.0F), kCharacter);

  const State state = world_.Tick({}, kTick).state;

  ASSERT_EQ(state.bodies.size(), 2U);
  EXPECT_EQ(state.bodies[0].entity, kAlice);
  EXPECT_EQ(state.bodies[1].entity, kBob);
}

// Requirements: US-03
TEST_F(SimulationTest, APlayerStartsStandingWhereItSpawned) {
  world_.AddPlayer(kAlice, Vec3(3.0F, 0.0F, -2.0F), kCharacter);
  Run(kSettleTicks, {});

  const State state = world_.Tick({}, kTick).state;

  EXPECT_NEAR(Body(state, kAlice).position.x, 3.0F, 0.05F);
  EXPECT_NEAR(Body(state, kAlice).position.z, -2.0F, 0.05F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kStanding);
}

// Requirements: US-04
TEST_F(SimulationTest, AForwardCommandMovesThatPlayerOnly) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kBob, Vec3(0.0F, 0.0F, 10.0F), kCharacter);
  Run(kSettleTicks, {});
  const float start = Body(world_.Tick({}, kTick).state, kAlice).position.x;
  const float bob_start = Body(world_.Tick({}, kTick).state, kBob).position.x;

  const State state = Run(kWalkTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  EXPECT_GT(Body(state, kAlice).position.x, start + 2.0F);
  EXPECT_NEAR(Body(state, kBob).position.x, bob_start, 0.01F);
}

// Requirements: US-04
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

// Requirements: US-04
TEST_F(SimulationTest, APlayerWithNoCommandStopsAndKeepsItsStance) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  Run(kSettleTicks, {});
  Run(kWalkTicks / 2,
      {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F), Stance::kCrouching)}});

  const State state = Run(kWalkTicks / 2, {});

  EXPECT_NEAR(HorizontalSpeed(Body(state, kAlice)), 0.0F, 0.01F);
  EXPECT_EQ(Body(state, kAlice).stance, Stance::kCrouching);
}

// Every side learns where a body looks and whether it aims down sights from
// the state alone (ADR-0066), on the grid the pitch travels on (ADR-0038).
TEST_F(SimulationTest, APlayersStateCarriesThePitchAndAdsOfTheCommandItApplied) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kBob, Vec3(0.0F, 0.0F, 10.0F), kCharacter);
  Command aiming;
  aiming.pitch = -0.1234567F;
  aiming.ads = true;

  const State state = world_.Tick({PlayerCommand{.entity = kAlice, .command = aiming}}, kTick).state;

  ASSERT_EQ(state.bodies.size(), 2U);
  EXPECT_EQ(state.bodies[0].pitch, augusta::math::SnapAngle(-0.1234567F));
  EXPECT_TRUE(state.bodies[0].ads);
  EXPECT_EQ(state.bodies[1].pitch, 0.0F);
  EXPECT_FALSE(state.bodies[1].ads);
}

// As it keeps where it faces.
TEST_F(SimulationTest, APlayerWithNoCommandKeepsItsPitchAndAds) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  Command aiming;
  aiming.pitch = 0.5F;
  aiming.ads = true;
  world_.Tick({PlayerCommand{.entity = kAlice, .command = aiming}}, kTick);

  const State state = Run(3, {});

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].pitch, 0.5F);
  EXPECT_TRUE(state.bodies[0].ads);
}

// The Stride (ADR-0066): one byte, 256 steps to a cycle of two steps.
constexpr int kStrideSteps = 256;

// How far apart two Strides are, the short way round the cycle.
int StrideDistance(int a, int b) {
  const int forward = ((a - b) % kStrideSteps + kStrideSteps) % kStrideSteps;
  return std::min(forward, kStrideSteps - forward);
}

// A character with Clips authored for the test (clips_fixture.h).
Character WithClips() {
  Character character = kCharacter;
  character.clips = augusta::clips_fixture::TestClips();
  return character;
}

// Movement advances a player's Stride by the distance it walks over the stride
// length of its stance's blend space there: 30 ticks at 3 m/s walk 1.5 m. Each
// tick's advance is rounded to a whole step, so a tick may be up to half a step off.
TEST_F(SimulationTest, AWalkingPlayersStrideAdvancesByTheDistanceOverItsClipsStrideLength) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), WithClips());
  const std::uint8_t start = Body(Run(kSettleTicks, {}), kAlice).stride;
  constexpr int kTicks = 30;

  const State state = Run(kTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(0.0F, 0.0F, -1.0F))}});

  // Ahead, half of the forward Clip's 3 m stride.
  EXPECT_LE(StrideDistance(Body(state, kAlice).stride, start + (kStrideSteps / 2)), (kTicks / 2) + 1);
}

TEST_F(SimulationTest, AStrafingPlayersStrideAdvancesByItsSideClipsStrideLength) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), WithClips());
  const std::uint8_t start = Body(Run(kSettleTicks, {}), kAlice).stride;
  constexpr int kTicks = 30;

  const State state = Run(kTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(1.0F, 0.0F, 0.0F))}});

  // To its right, 1.5 m of the right Clip's 2.4 m stride.
  const int expected = start + static_cast<int>(1.5F / augusta::clips_fixture::kStandingRight * kStrideSteps);
  EXPECT_LE(StrideDistance(Body(state, kAlice).stride, expected), (kTicks / 2) + 1);
}

TEST_F(SimulationTest, AStillPlayerHoldsItsStride) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), WithClips());
  Run(kSettleTicks, {});
  const std::uint8_t walked =
      Body(Run(10, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(0.0F, 0.0F, -1.0F))}}), kAlice).stride;

  const State state = Run(kSettleTicks, {});

  EXPECT_EQ(Body(state, kAlice).stride, walked);
}

// Movement advances a player's change of stance over its Clips' transition
// duration for the two stances: 0.25 s from standing to crouching, 17 steps of
// 255 a tick, as PredictionWorld advances the local player's.
TEST_F(SimulationTest, ACrouchingPlayersProgressAdvancesOverItsClipsTransitionDuration) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), WithClips());
  Run(kSettleTicks, {});
  Command crouch{};
  crouch.movement.desired_stance = Stance::kCrouching;

  const State state = Run(4, {PlayerCommand{.entity = kAlice, .command = crouch}});

  EXPECT_EQ(Body(state, kAlice).stance, Stance::kCrouching);
  EXPECT_EQ(Body(state, kAlice).previous_stance, Stance::kStanding);
  EXPECT_EQ(Body(state, kAlice).stance_progress, 4 * 17);
}

TEST_F(SimulationTest, APlayerWhoseCharacterHasNoClipsHoldsItsStride) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  Run(kSettleTicks, {});

  const State state = Run(kWalkTicks, {PlayerCommand{.entity = kAlice, .command = Walking(Vec3(0.0F, 0.0F, -1.0F))}});

  EXPECT_EQ(Body(state, kAlice).stride, 0);
}

// Requirements: US-15
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
  const State state = world_.Tick({}, kTick).state;

  ASSERT_EQ(state.bodies.size(), 1U);
  EXPECT_EQ(state.bodies[0].entity, kBob);
}

TEST_F(SimulationTest, RemovingAPlayerNotInTheWorldChangesNothing) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);

  world_.RemovePlayer(kBob);

  EXPECT_EQ(world_.Tick({}, kTick).state.bodies.size(), 1U);
}

TEST_F(SimulationTest, AddingAPlayerTwiceKeepsOne) {
  world_.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  world_.AddPlayer(kAlice, Vec3(9.0F, 0.0F, 0.0F), kCharacter);

  EXPECT_EQ(world_.Tick({}, kTick).state.bodies.size(), 1U);
}

// A rifle of 600 rounds a minute, a round every six ticks of kTick, with a
// magazine that outlasts a test; its rounds fly straight, 10 m a tick.
Parameters WithTheTestRifle() {
  Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.magazine_capacity = 200;
  parameters.rifle.muzzle_velocity = 600.0F;
  parameters.ammo.flight.gravity = 0.0F;
  parameters.ammo.flight.max_range = 1000.0F;
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
  explicit FireTest(const Parameters& parameters = WithTheTestRifle(), const augusta::weapon::Markers& rifle = {})
      : world_(parameters, kTickRate, {}, rifle) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
  }

  void SetUp() override {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), kCharacter);
    for (int i = 0; i < kSettleTicks; ++i) {
      world_.Tick({}, kTick);
    }
  }

  State Tick(const Command& command) {
    return world_.Tick({PlayerCommand{.entity = kAlice, .command = command}}, kTick).state;
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
      state = world_.Tick({}, kTick).state;
    }
    return state;
  }

  // Where Alice's feet are.
  Vec3 Feet() { return world_.Tick({}, kTick).state.bodies.front().body.position; }

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

// Requirements: US-07
TEST_F(FireTest, HoldingFireFiresARoundAtOnceAndThenOneEveryFireInterval) {
  EXPECT_EQ(FiringTicks(19, Firing()), (std::vector<int>{0, 6, 12, 18}));
}

// Requirements: US-07
TEST_F(FireTest, ATapOfFireFiresExactlyOneRound) {
  EXPECT_EQ(Tick(Firing()).shots.size(), 1U);

  EXPECT_TRUE(FiringTicks(30, Command{}).empty());
}

// Requirements: US-07
TEST_F(FireTest, AShotNamesThePlayerWhoFiredIt) {
  world_.AddPlayer(kBob, Vec3(5.0F, 0.5F, 0.0F), kCharacter);

  const State state = world_.Tick({PlayerCommand{.entity = kBob, .command = Firing()}}, kTick).state;

  ASSERT_EQ(state.shots.size(), 1U);
  EXPECT_EQ(state.shots[0].shooter, kBob);
}

// Requirements: US-07
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

// Requirements: US-07
TEST_F(FireTest, ARifleAtRestFiresNoSoonerForHavingRested) {
  Tick(Firing());
  FiringTicks(60, Command{});

  EXPECT_EQ(FiringTicks(7, Firing()), (std::vector<int>{0, 6}));
}

// Requirements: US-07
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
// Requirements: US-07
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
// Requirements: US-07, NFR-03
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
// Requirements: US-10
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

// Requirements: US-10
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
    parameters.ammo.flight.max_range = 25.0F;
    return parameters;
  }

  ShortRangeFireTest() : FireTest(WithARangeOfTwentyFiveMeters()) {}
};

// 10 m a tick: the round is past 25 m on its third tick.
// Requirements: US-10
TEST_F(ShortRangeFireTest, ABulletFiredIntoOpenSpaceExpiresAtTheAmmosMaxRange) {
  const State first = Tick(FiringDownX());
  const State second = Tick(Command{});
  const State third = Tick(Command{});

  EXPECT_EQ(first.bullets_in_flight, 1U);
  EXPECT_EQ(second.bullets_in_flight, 1U);
  EXPECT_EQ(third.bullets_in_flight, 0U);
  EXPECT_TRUE(third.map_impacts.empty());
}

// The most rounds valid Parameters put in the air: two players whose rifles
// fire every tick, rounds that crawl a metre a second toward the longest range
// a float holds, and a tick rate of 10 Hz, at which a bullet flies 50 ticks at
// most (ballistics::kMaxFlightTime) and a magazine lasts five times that.
class ExtremeFireTest : public ::testing::Test {
 protected:
  static constexpr std::uint8_t kSlowTickRate = 10;
  static constexpr float kSlowTick = 1.0F / kSlowTickRate;

  static Parameters Extreme() {
    Parameters parameters;
    parameters.rifle.rounds_per_minute = std::numeric_limits<float>::max();
    parameters.rifle.magazine_capacity = std::numeric_limits<std::uint8_t>::max();
    parameters.rifle.muzzle_velocity = 1.0F;
    parameters.ammo.flight.gravity = 0.0F;
    parameters.ammo.flight.max_range = std::numeric_limits<float>::max();
    return parameters;
  }

  ExtremeFireTest() : world_(Extreme(), kSlowTickRate) {
    EXPECT_TRUE(augusta::parameters::Validate(Extreme()).has_value());
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), kCharacter);
    world_.AddPlayer(kBob, Vec3(5.0F, 0.5F, 0.0F), kCharacter);
  }

  State BothFire() {
    return world_
        .Tick(
            {PlayerCommand{.entity = kAlice, .command = Firing()}, PlayerCommand{.entity = kBob, .command = Firing()}},
            kSlowTick)
        .state;
  }

  World world_;
};

// Each tick adds a round a player and the oldest expire: a lifetime of rounds
// less the one that just expired, a player, and never more.
// Requirements: US-10
TEST_F(ExtremeFireTest, BulletsInFlightStopAtAFlightTimeOfRoundsAPlayer) {
  const std::uint32_t flight_ticks = augusta::ballistics::MaxFlightSteps(kSlowTick);
  ASSERT_EQ(flight_ticks, 50U);

  State state;
  for (std::uint32_t tick = 1; tick <= 4 * flight_ticks; ++tick) {
    state = BothFire();
    ASSERT_EQ(state.shots.size(), 2U) << "tick " << tick;
    ASSERT_EQ(state.bullets_in_flight, 2 * std::min(tick, flight_ticks - 1)) << "tick " << tick;
  }
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

// Requirements: US-07
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

// Requirements: US-07
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

// Requirements: US-07
TEST_F(SmallMagazineFireTest, HoldingFireFiresTheMagazineAndThenNothing) {
  EXPECT_EQ(FiringTicks(120, Firing()), (std::vector<int>{0, 6, 12}));
}

// Requirements: US-07
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

// Requirements: US-08
TEST_F(ReloadTest, AReloadFiresNothingForItsDurationAndThenTheMagazineIsFull) {
  FiringTicks(30, Firing());
  ASSERT_EQ(Rounds(), 0);

  // The press and the 29 ticks after it are the half second; a full magazine follows.
  EXPECT_TRUE(Tick(Reloading(/*fire=*/true)).shots.empty());
  EXPECT_TRUE(FiringTicks(kReloadTicks - 1, Firing()).empty());
  EXPECT_EQ(FiringTicks(120, Firing()), (std::vector<int>{0, 6, 12}));
}

// Requirements: US-08
TEST_F(ReloadTest, AReloadOfAPartlyEmptyMagazineFillsIt) {
  Tick(Firing());
  ASSERT_EQ(Rounds(), 2);

  Tick(Reloading());
  FiringTicks(kReloadTicks, Command{});

  EXPECT_EQ(Rounds(), 3);
}

// Requirements: US-08
TEST_F(ReloadTest, TheMagazineIsNotFullBeforeTheReloadEnds) {
  Tick(Firing());

  Tick(Reloading());
  const State state = Run(kReloadTicks - 2);

  EXPECT_EQ(state.bodies.front().rifle.rounds, 2);
  EXPECT_GT(state.bodies.front().rifle.reload_remaining, 0.0F);
}

// Requirements: US-08
TEST_F(ReloadTest, AReloadPressWithAFullMagazineStartsNothingAndFireGoesOn) {
  EXPECT_EQ(Tick(Reloading(/*fire=*/true)).shots.size(), 1U);

  EXPECT_EQ(FiringTicks(12, Firing()), (std::vector<int>{5, 11}));
}

// A client that sends reload on every tick (the sampler sends it on one) still
// reloads once: a reload under way is not started over.
// Requirements: US-08
TEST_F(ReloadTest, HoldingReloadForManyTicksStartsOneReloadNotOneEveryTick) {
  FiringTicks(30, Firing());

  FiringTicks(kReloadTicks, Reloading());

  EXPECT_EQ(Rounds(), 3);
}

// Requirements: US-08
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

// Requirements: US-08
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

// Requirements: US-09
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

// Requirements: US-09
TEST_F(RecoilTest, PastThePatternsLastKickTheLastRepeats) {
  const auto shots = Burst(6);

  EXPECT_EQ(shots[5].yaw - shots[4].yaw, kThirdYaw);
  EXPECT_EQ(shots[5].pitch - shots[4].pitch, kThirdPitch);
  EXPECT_EQ(shots[4].yaw - shots[3].yaw, kThirdYaw);
  EXPECT_EQ(shots[4].pitch - shots[3].pitch, kThirdPitch);
}

// US-09: the pattern is the same every burst, and the aim settles back between them.
// Requirements: US-09
TEST_F(RecoilTest, ASecondBurstFromTheSameViewAfterTheRecoilHasRecoveredLeavesAsTheFirstDid) {
  const auto first = Burst(4);
  Run(60);

  const auto second = Burst(4);

  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(second[i].yaw, first[i].yaw) << "round " << i;
    EXPECT_EQ(second[i].pitch, first[i].pitch) << "round " << i;
  }
}

// Requirements: US-09
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

// Requirements: US-09, US-06
TEST_F(RecoilTest, AimingDownSightsScalesEveryKick) {
  const auto shots = Burst(4, FiringFromTheView(/*ads=*/true));

  EXPECT_EQ(shots[0].pitch, kViewPitch);
  EXPECT_EQ(shots[1].pitch, kViewPitch + (kFirstPitch / 2.0F));
  EXPECT_EQ(shots[2].yaw, kViewYaw + (kSecondYaw / 2.0F));
  EXPECT_EQ(shots[3].yaw, kViewYaw + ((kSecondYaw + kThirdYaw) / 2.0F));
  EXPECT_EQ(shots[3].pitch, kViewPitch + ((kFirstPitch + kSecondPitch + kThirdPitch) / 2.0F));
}

// Requirements: US-09
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
// Requirements: US-09
TEST_F(RecoilTest, ARecoveringOffsetShrinksStraightTowardZero) {
  Burst(2);
  const float pitch = kFirstPitch + kSecondPitch;
  const float length = std::hypot(pitch, kSecondYaw);

  const augusta::weapon::RecoilOffset recoil = RecoilAfterARestingTick();

  EXPECT_NEAR(std::hypot(recoil.pitch, recoil.yaw), length - kRecoveryPerTick, kAngleTolerance);
  EXPECT_NEAR(recoil.yaw / recoil.pitch, kSecondYaw / pitch, 1e-3F);
}

// A rifle being reloaded pulls no trigger, whatever the fire control does.
// Requirements: US-09
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

// Requirements: US-09, NFR-03
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
    return world_
        .Tick({PlayerCommand{.entity = kAlice, .command = command}, PlayerCommand{.entity = kBob, .command = bob_}},
              kTick)
        .state;
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
  // feet as the State of a tick shows them, which is the Seen time it reports.
  Command FiringAt(const Vec3& offset) { return FiringAt(offset, Tick(Command{})); }

  // As FiringAt, at Bob's feet as state, the newest tick's, shows them.
  static Command FiringAt(const Vec3& offset, const State& state) {
    const Vec3 eye = Entity(state, kAlice).body.position + Target().eye;
    const Vec3 aim = Entity(state, kBob).body.position + offset - eye;
    Command command = Firing();
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.seen_tick = state.tick;
    return command;
  }

  // Alice fires one round at the point offset from Bob's feet; returns the hits
  // of that tick and of those it takes the rifle to be ready again.
  std::vector<Hit> ShootAt(const Vec3& offset) { return Shoot(FiringAt(offset)); }

  // As ShootAt, on a Command that reports the Seen time of seen_tick and seen_fraction.
  std::vector<Hit> ShootAt(const Vec3& offset, augusta::tick::Tick seen_tick, float seen_fraction) {
    Command command = FiringAt(offset);
    command.seen_tick = seen_tick;
    command.seen_fraction = seen_fraction;
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

// Requirements: US-03
TEST_F(HitTest, APlayerStartsAMatchWithTheParametersStartingHealth) {
  const State state = Tick(Command{});

  EXPECT_EQ(Entity(state, kAlice).health, kStartingHealth);
  EXPECT_EQ(Entity(state, kBob).health, kStartingHealth);
}

// Requirements: US-11
TEST_F(HitTest, AHitNamesItsShooterItsTargetAndTheBodyPartItCrossed) {
  const std::vector<Hit> hits = ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);
  EXPECT_EQ(hits[0].part, BodyPart::kTorso);
}

// Requirements: US-12
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

// Requirements: US-12, US-13
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
// Requirements: US-13
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
// Requirements: US-13
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
// Requirements: US-13
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

// Requirements: US-13, US-15
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
// Requirements: US-13
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
// Requirements: US-10, US-13
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

// Requirements: US-12
TEST_F(HitTest, HealthDoesNotComeBackWithTime) {
  ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));

  Wait(300);

  EXPECT_EQ(Entity(Tick(Command{}), kBob).health, kStartingHealth - kTorsoDamage);
}

// Requirements: US-11
TEST_F(HitTest, AShotPastTheTargetHitsNoOne) {
  EXPECT_TRUE(ShootAt(Vec3(1.0F, kTorsoHeight, 0.0F)).empty());
  EXPECT_TRUE(ShootAt(Vec3(0.0F, 2.0F, 0.0F)).empty());
}

// Its right arm is at +X while it faces yaw 0, and at -X once it has turned half a turn.
// Requirements: US-11
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

// Bob faces just short of half a turn one way, then just short of it the
// other: halfway between the two he faces half a turn, his right arm at -X, and
// not yaw 0, which is the long way round.
// Requirements: US-11, NFR-02
TEST_F(HitTest, ASeenTimeBetweenTwoFacingsTurnsTheHitboxesAlongTheShorterArc) {
  const Vec3 left_of_it(-0.35F, kTorsoHeight, 0.0F);
  bob_.yaw = 3.0F;
  Wait(2);
  bob_.yaw = -3.0F;
  const augusta::tick::Tick turned = Tick(Command{}).tick;

  const std::vector<Hit> hits = ShootAt(left_of_it, turned - 1, 0.5F);

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].part, BodyPart::kLimb);
}

// Requirements: US-04
TEST_F(HitTest, APlayerWithNoCommandKeepsWhereItFaced) {
  bob_.yaw = 1.25F;
  Wait(1);

  const State state = world_.Tick({}, kTick).state;

  EXPECT_EQ(Entity(state, kBob).yaw, 1.25F);
}

// The eye is inside the shooter's own head hitbox, which every round leaves through.
// Requirements: US-11
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
// Requirements: US-11
TEST_F(HitTest, WithTwoTargetsInLineOnlyTheNearerIsHit) {
  constexpr EntityId kCarol = static_cast<EntityId>(3);
  world_.AddPlayer(kCarol, Vec3(0.0F, 0.5F, -15.0F), Target());
  Wait(kSettleTicks);

  const std::vector<Hit> hits = ShootAt(Vec3(0.0F, kTorsoHeight, 0.0F));

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].target, kBob);
  EXPECT_EQ(Entity(Tick(Command{}), kCarol).health, kStartingHealth);
}

// Requirements: US-10, US-11
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

// A round aimed a metre beside Bob strikes Carol, standing there just short of
// him, on the tick it is fired: her Hitboxes are posed for that Step. With a
// wall just short of her, the same round strikes the wall on that tick instead,
// and the Map it strikes is never read as one of the Hitboxes posed beside it.
// Requirements: US-10, US-11
TEST_F(HitTest, ARoundStrikingTheMapWhileHitboxesArePosedForItsStepHitsNoOne) {
  constexpr EntityId kCarol = static_cast<EntityId>(3);
  world_.AddPlayer(kCarol, Vec3(1.0F, 0.5F, -9.5F), Target());
  Wait(kSettleTicks);
  const Vec3 beside_bob(1.0F, kTorsoHeight, 0.0F);
  const State unwalled = Tick(FiringAt(beside_bob));
  ASSERT_EQ(unwalled.hits.size(), 1U);
  ASSERT_EQ(unwalled.hits[0].target, kCarol);
  const float carols_health = Entity(unwalled, kCarol).health;
  Wait(6);
  constexpr float kWallZ = -9.0F;
  ASSERT_TRUE(world_
                  .AddCollisionMesh(CollisionMesh{.points = {Vec3(-20.0F, 0.0F, kWallZ), Vec3(-20.0F, 5.0F, kWallZ),
                                                             Vec3(20.0F, 5.0F, kWallZ), Vec3(20.0F, 0.0F, kWallZ)},
                                                  .indices = {0, 1, 2, 0, 2, 3}})
                  .has_value());

  const State state = Tick(FiringAt(beside_bob));

  EXPECT_TRUE(state.hits.empty());
  ASSERT_EQ(state.map_impacts.size(), 1U);
  EXPECT_NEAR(state.map_impacts[0].z, kWallZ, 0.01F);
  EXPECT_EQ(Entity(state, kBob).health, kStartingHealth);
  EXPECT_EQ(Entity(state, kCarol).health, carols_health);
}

// Alice and Bob fire at each other on one tick, from the same Seen time: each
// round is judged against the other, never against its own shooter.
// Requirements: US-11
TEST_F(HitTest, TwoPlayersFiringAtEachOtherOnOneTickEachHitTheOther) {
  const Command alice = FiringAt(Vec3(0.0F, kTorsoHeight, 0.0F));
  // Half a turn round, Bob aims back along Alice's line: level and as far.
  bob_ = alice;
  bob_.yaw = alice.yaw + std::numbers::pi_v<float>;

  std::vector<Hit> hits = Tick(alice).hits;
  bob_ = Command{};
  const std::vector<Hit> later = Wait(6);
  hits.insert(hits.end(), later.begin(), later.end());

  ASSERT_EQ(hits.size(), 2U);
  std::ranges::sort(hits, {}, &Hit::shooter);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);
  EXPECT_EQ(hits[1].shooter, kBob);
  EXPECT_EQ(hits[1].target, kAlice);
}

// The top of Bob's head is the farthest any of his hitboxes reaches from his
// feet: a round that crosses it, a centimetre below, still strikes it.
// Requirements: US-11
TEST_F(HitTest, ARoundThroughTheTopOfTheHeadHitsIt) {
  const std::vector<Hit> hits = ShootAt(Vec3(0.0F, 1.79F, 0.0F));

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].part, BodyPart::kHead);
}

// Where a character reaching out to its right holds its hand: 2 m to the right
// of its feet, at 1.4 m, while it stands facing yaw 0.
constexpr float kHandSide = 2.0F;
constexpr float kHandHeight = 1.4F;

// A character whose Skeleton (ADR-0061) holds its arm out to its right: its
// Shoulder Joint, 20 cm right of its feet at 1.4 m, is turned a quarter turn
// about Z, so the Hand Joint 1.8 m down the arm sits kHandSide to the right of
// its feet. Its torso is bound to its root Joint and its hand to the Hand
// Joint, each Hitbox in its own Joint's space.
Character Reaching() {
  using augusta::assets::JointData;
  using augusta::math::Mat4;
  using augusta::math::Quat;
  const Quat quarter_turn_about_z = glm::angleAxis(std::numbers::pi_v<float> / 2.0F, Vec3(0.0F, 0.0F, 1.0F));
  const Mat4 shoulder = augusta::math::ToMat4(Vec3(0.2F, kHandHeight, 0.0F), quarter_turn_about_z, Vec3(1.0F));
  const Mat4 hand = shoulder * augusta::math::ToMat4(Vec3(0.0F, -1.8F, 0.0F), Quat(1.0F, 0.0F, 0.0F, 0.0F), Vec3(1.0F));
  CharacterHitbox palm = Box(BodyPart::kLimb, Vec3(-0.1F), Vec3(0.1F));
  palm.joint = 2;
  return Character{
      .eye = Vec3(0.0F, 1.6F, 0.0F),
      .hitboxes = {Box(BodyPart::kTorso, Vec3(-0.2F, 0.9F, -0.1F), Vec3(0.2F, 1.5F, 0.1F)), palm},
      .skeleton = {.joints = {JointData{.name = "Root"},
                              JointData{.name = "Root/Shoulder",
                                        .parent = 0,
                                        .translation = Vec3(0.2F, kHandHeight, 0.0F),
                                        .rotation = quarter_turn_about_z},
                              JointData{
                                  .name = "Root/Shoulder/Hand", .parent = 1, .translation = Vec3(0.0F, -1.8F, 0.0F)}},
                   .rest_transforms = {Mat4(1.0F), shoulder, hand}},
  };
}

// HitTest's Alice and Bob, Bob playing Reaching: Alice's rounds are judged
// against his Hitboxes as the Posing phase poses them by his Skeleton
// (ADR-0061).
class PosedHitTest : public HitTest {
 protected:
  void SetUp() override {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), Target());
    world_.AddPlayer(kBob, Vec3(0.0F, 0.5F, -10.0F), Reaching());
    bob_ = Command{};
    Wait(kSettleTicks);
  }
};

// The hand is far out of reach of the Hitboxes' own geometry: a round that
// strikes it was neither placed nor culled by that geometry alone.
// Requirements: US-11
TEST_F(PosedHitTest, AHitboxIsJudgedWhereItsJointHoldsIt) {
  const std::vector<Hit> hand = ShootAt(Vec3(kHandSide, kHandHeight, 0.0F));
  const std::vector<Hit> torso = ShootAt(Vec3(0.0F, 1.2F, 0.0F));

  ASSERT_EQ(hand.size(), 1U);
  EXPECT_EQ(hand[0].part, BodyPart::kLimb);
  ASSERT_EQ(torso.size(), 1U);
  EXPECT_EQ(torso[0].part, BodyPart::kTorso);
}

// The stance does not scale the Skeleton's root (ADR-0040): how low a body is
// is its stance's Clips', and Bob has none, so crouched his hand is where it
// was standing.
// Requirements: US-11
TEST_F(PosedHitTest, TheStanceDoesNotScaleTheSkeletonsRoot) {
  bob_.movement.desired_stance = Stance::kCrouching;
  Wait(kSettleTicks);
  ASSERT_EQ(Entity(Tick(Command{}), kBob).body.stance, Stance::kCrouching);

  const std::vector<Hit> hand = ShootAt(Vec3(kHandSide, kHandHeight, 0.0F));

  ASSERT_EQ(hand.size(), 1U);
  EXPECT_EQ(hand[0].part, BodyPart::kLimb);
}

// Requirements: US-11
TEST_F(PosedHitTest, TheSkeletonsRootTurnsItsHitboxesWithWhereItFaces) {
  bob_.yaw = std::numbers::pi_v<float>;
  Wait(1);

  EXPECT_TRUE(ShootAt(Vec3(kHandSide, kHandHeight, 0.0F)).empty());
  const std::vector<Hit> turned = ShootAt(Vec3(-kHandSide, kHandHeight, 0.0F));

  ASSERT_EQ(turned.size(), 1U);
  EXPECT_EQ(turned[0].part, BodyPart::kLimb);
}

// Bob turns a quarter turn on one tick, swinging his hand toward Alice, so a
// round crosses it on the tick it is fired. Halfway between that tick and the
// one before, Lag compensation turns his root's facing along the shorter arc
// and composes his Joints, the Rest pose's on both ticks, onto it (ADR-0044):
// his hand an eighth of a turn round, still kHandSide from his feet, and not on
// the chord between where the two ticks held it. The Rest pose being his only
// pose, no Joint's own interpolation is exercised.
// Requirements: US-11, NFR-02
TEST_F(PosedHitTest, ASeenTimeBetweenTwoFacingsTurnsTheRootAlongTheArcInTheRestPose) {
  bob_.yaw = -std::numbers::pi_v<float> / 2.0F;
  const augusta::tick::Tick turned = Tick(Command{}).tick;
  const float eighth_turn = std::numbers::pi_v<float> / 4.0F;
  const Vec3 on_the_arc(kHandSide * std::cos(eighth_turn), kHandHeight, kHandSide * std::sin(eighth_turn));
  const Vec3 on_the_chord(kHandSide / 2.0F, kHandHeight, kHandSide / 2.0F);

  const std::vector<Hit> arc = ShootAt(on_the_arc, turned - 1, 0.5F);
  const std::vector<Hit> chord = ShootAt(on_the_chord, turned - 1, 0.5F);

  ASSERT_EQ(arc.size(), 1U);
  EXPECT_EQ(arc[0].part, BodyPart::kLimb);
  EXPECT_TRUE(chord.empty());
}

// The Walker (clips_fixture.h), its Hitboxes on its Joints: a head on its Head
// Joint, a torso up from its Pelvis and a leg down each of its legs, 1 m long.
// Standing, its head is 1.6 m to 1.9 m up and its torso 1 m to 1.6 m;
// crouched, its Pelvis 0.6 m up, its head is 1.2 m to 1.5 m.
Character Walker() {
  namespace fixture = augusta::clips_fixture;
  CharacterHitbox head = Box(BodyPart::kHead, Vec3(-0.1F, 0.0F, -0.1F), Vec3(0.1F, 0.3F, 0.1F));
  head.joint = fixture::kWalkerHead;
  CharacterHitbox torso = Box(BodyPart::kTorso, Vec3(-0.2F, 0.0F, -0.1F), Vec3(0.2F, 0.6F, 0.1F));
  torso.joint = fixture::kWalkerPelvis;
  CharacterHitbox left_leg = Box(BodyPart::kLimb, Vec3(-0.07F, -1.0F, -0.07F), Vec3(0.07F, 0.0F, 0.07F));
  left_leg.joint = fixture::kWalkerLeftLeg;
  CharacterHitbox right_leg = left_leg;
  right_leg.joint = fixture::kWalkerRightLeg;
  return Character{.eye = Vec3(0.0F, 1.7F, 0.0F),
                   .hitboxes = {head, torso, left_leg, right_leg},
                   .skeleton = fixture::WalkerSkeleton(),
                   .clips = fixture::WalkerClips()};
}

// HitTest's Alice and Bob, Bob playing the Walker: Alice's rounds are judged
// against his Hitboxes as his Clips pose them (ADR-0066).
class AnimatedHitTest : public HitTest {
 protected:
  void SetUp() override {
    world_.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), Target());
    world_.AddPlayer(kBob, Vec3(0.0F, 0.5F, -10.0F), Walker());
    bob_ = Command{};
    Wait(kSettleTicks);
  }

  // Puts Bob back 3 m to Alice's right, walks him ahead at his forward walk's
  // speed facing -X, across Alice's line of fire, and ticks with Alice idle
  // until his Stride is within a few steps of one of strides: that tick's
  // State, Bob within a few meters of straight ahead of Alice. Fails the test
  // if no tick of two cycles gets there.
  State UntilWalkingBobsStrideNears(std::initializer_list<int> strides) {
    constexpr int kNear = 3;
    world_.RemovePlayer(kBob);
    world_.AddPlayer(kBob, Vec3(3.0F, 0.5F, -10.0F), Walker());
    bob_ = Walking(Vec3(-1.0F, 0.0F, 0.0F));
    bob_.yaw = std::numbers::pi_v<float> / 2.0F;
    Wait(kSettleTicks);
    for (int i = 0; i < 2 * kTickRate; ++i) {
      const State state = Tick(Command{});
      for (const int stride : strides) {
        if (StrideDistance(Entity(state, kBob).body.stride, stride) <= kNear) {
          return state;
        }
      }
    }
    ADD_FAILURE() << "Bob's Stride never neared it";
    return Tick(Command{});
  }
};

// Requirements: US-11
TEST_F(AnimatedHitTest, AShotAtAStandingHeadsHeightHitsAStandingPlayerAndMissesACrouchedOne) {
  const Vec3 standing_head(0.0F, 1.75F, 0.0F);
  const std::vector<Hit> standing = ShootAt(standing_head);

  bob_.movement.desired_stance = Stance::kCrouching;
  Wait(kSettleTicks);
  const std::vector<Hit> crouched = ShootAt(standing_head);
  const std::vector<Hit> crouched_head = ShootAt(Vec3(0.0F, 1.35F, 0.0F));

  ASSERT_EQ(standing.size(), 1U);
  EXPECT_EQ(standing[0].part, BodyPart::kHead);
  EXPECT_TRUE(crouched.empty());
  ASSERT_EQ(crouched_head.size(), 1U);
  EXPECT_EQ(crouched_head[0].part, BodyPart::kHead);
}

// Bob crouches, his Pelvis going down from 1 m to 0.6 m over the Walker's
// 0.25 s from standing to crouching: 17 steps of progress a tick, 119 of 255
// on the 7th tick and 136 on the 8th. A Seen time halfway between those two
// ticks is judged against Joints Lag compensation interpolates between them
// (ADR-0044), the change exactly halfway: his Pelvis 0.8 m up, his head 1.4 m
// to 1.7 m and his torso 0.8 m to 1.4 m. A Shot at 1.55 m, which would hit his
// standing torso and miss his crouched head, hits his head there; one at
// 1.8 m, his standing head, misses.
// Requirements: US-11, NFR-02
TEST_F(AnimatedHitTest, HalfwayThroughAChangeOfStanceAShotHitsTheBodyHalfwayDown) {
  bob_.movement.desired_stance = Stance::kCrouching;
  augusta::tick::Tick past_halfway = 0;
  for (int i = 0; i < kSettleTicks && past_halfway == 0; ++i) {
    const State state = Tick(Command{});
    const auto& body = Entity(state, kBob).body;
    if (body.stance == Stance::kCrouching && body.stance_progress >= 128) {
      ASSERT_EQ(body.stance_progress, 136);
      past_halfway = state.tick;
    }
  }
  ASSERT_NE(past_halfway, 0U);

  const std::vector<Hit> between = ShootAt(Vec3(0.0F, 1.55F, 0.0F), past_halfway - 1, 0.5F);
  const std::vector<Hit> standing_head = ShootAt(Vec3(0.0F, 1.8F, 0.0F), past_halfway - 1, 0.5F);

  ASSERT_EQ(between.size(), 1U);
  EXPECT_EQ(between[0].part, BodyPart::kHead);
  EXPECT_TRUE(standing_head.empty());
}

// Bob walks ahead at his forward walk's speed, facing -X, so his legs swing
// across Alice's line of fire. At a Foot contact (a Stride of 0 or 128) one leg
// is 30 degrees ahead: 0.6 m down it is 0.3 m ahead of his hips, 0.48 m up. A
// quarter of a cycle later his legs pass each other, straight down. Each round
// is judged where the Stride had him on the tick its Seen time names.
// Requirements: US-11
TEST_F(AnimatedHitTest, AShotAtAWalkingPlayersLegHitsItWhereTheStrideHasIt) {
  const Vec3 ahead_of_his_hips(-0.3F, 0.48F, 0.0F);
  const Vec3 under_his_hips(0.0F, 0.48F, 0.0F);

  const std::vector<Hit> stepping = Shoot(FiringAt(ahead_of_his_hips, UntilWalkingBobsStrideNears({0, 128})));
  const std::vector<Hit> passing = Shoot(FiringAt(ahead_of_his_hips, UntilWalkingBobsStrideNears({64, 192})));
  const std::vector<Hit> passing_under = Shoot(FiringAt(under_his_hips, UntilWalkingBobsStrideNears({64, 192})));

  ASSERT_EQ(stepping.size(), 1U);
  EXPECT_EQ(stepping[0].part, BodyPart::kLimb);
  EXPECT_TRUE(passing.empty());
  ASSERT_EQ(passing_under.size(), 1U);
  EXPECT_EQ(passing_under[0].part, BodyPart::kLimb);
}

TEST_F(SimulationTest, AStateNamesItsTickFromOne) {
  EXPECT_EQ(world_.Tick({}, kTick).state.tick, 1U);
  EXPECT_EQ(world_.Tick({}, kTick).state.tick, 2U);
}

// What the Shooter's delay's cap of 300 ms needs, and no more (ADR-0044).
// Requirements: NFR-02
TEST(HitboxHistoryTest, TheHistoryHoldsTheCapsWorthOfTicksAtTheTickRate) {
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(60), 18U);
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(30), 9U);
  // A cap that is not a whole number of ticks takes the tick that covers it.
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(255), 77U);
  EXPECT_EQ(augusta::simulation::HitboxHistoryTicks(25), 8U);
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
// keeps where the State of each tick puts Bob, aims at where a Seen time of
// them shows him, and reports that Seen time with its Command (ADR-0044).
class LagCompensationTest : public ::testing::Test {
 protected:
  static constexpr float kAimHeight = 1.2F;
  // The Shooter's delay's cap, 300 ms, in ticks of kTick.
  static constexpr std::uint32_t kCapTicks = 18;

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
    // More ticks of walking than any Seen time here looks back.
    for (int i = 0; i < 60; ++i) {
      Tick(Command{});
    }
  }

  // A tick on which Alice does command and Bob what he was last told.
  State Tick(const Command& command) { return TickWith(command).state; }

  // As Tick, with all the tick resolved.
  TickResult TickWith(const Command& command) {
    TickResult result = world_.Tick(
        {PlayerCommand{.entity = kAlice, .command = command}, PlayerCommand{.entity = kBob, .command = bob_}}, kTick);
    const State& state = result.state;
    for (const auto& entry : state.bodies) {
      if (entry.entity == kBob) {
        seen_[state.tick] = entry.body.position;
      } else if (entry.entity == kAlice) {
        alice_ = entry.body.position;
      }
    }
    last_tick_ = state.tick;
    return result;
  }

  // The tick Alice's next Command is taken in on.
  [[nodiscard]] augusta::tick::Tick Next() const { return last_tick_ + 1; }

  // Where a Seen time of tick and fraction shows Bob's feet: between where the
  // States of tick and of the one after it put them.
  [[nodiscard]] Vec3 BobAt(augusta::tick::Tick tick, float fraction = 0.0F) const {
    return fraction == 0.0F ? seen_.at(tick) : augusta::math::Lerp(seen_.at(tick), seen_.at(tick + 1), fraction);
  }

  // Alice taps fire on the next tick, aimed at kAimHeight above feet, on a
  // Command that reports the Seen time of seen_tick and seen_fraction; returns the
  // hits of that tick and of the six after it.
  std::vector<Hit> Shoot(const Vec3& feet, augusta::tick::Tick seen_tick, float seen_fraction) {
    const Vec3 aim = feet + Vec3(0.0F, kAimHeight, 0.0F) - (alice_ + Sliver().eye);
    Command command = Firing();
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.seen_tick = seen_tick;
    command.seen_fraction = seen_fraction;
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
  std::map<augusta::tick::Tick, Vec3> seen_;
  augusta::tick::Tick last_tick_ = 0;
};

// Requirements: US-11, NFR-02
TEST_F(LagCompensationTest, ARoundIsJudgedAgainstHitboxesInterpolatedAtTheFractionItsSeenTimeReports) {
  augusta::tick::Tick seen = Next() - 6;
  const std::vector<Hit> hits = Shoot(BobAt(seen, 0.5F), seen, 0.5F);
  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);

  // Aimed the same, a Seen time that names either of the two ticks misses: Bob is
  // 2.5 cm to one side or the other of where the round passes.
  seen = Next() - 6;
  EXPECT_TRUE(Shoot(BobAt(seen, 0.5F), seen, 0.0F).empty());
  seen = Next() - 6;
  EXPECT_TRUE(Shoot(BobAt(seen, 0.5F), seen + 1, 0.0F).empty());
}

// Requirements: US-11, NFR-02
TEST_F(LagCompensationTest, ATargetThatHasSinceMovedAwayIsStillHitWhereItWasSeen) {
  const augusta::tick::Tick seen = Next() - 10;
  // Bob has walked most of half a meter since.
  ASSERT_GT(BobAt(Next() - 1).x - BobAt(seen).x, 0.4F);

  EXPECT_EQ(Shoot(BobAt(seen), seen, 0.0F).size(), 1U);
}

// Requirements: US-11, NFR-02
TEST_F(LagCompensationTest, ARoundAimedWhereTheTargetIsNowMissesOnAnOlderSeenTime) {
  EXPECT_TRUE(Shoot(BobAt(Next() - 1), Next() - 10, 0.0F).empty());
}

// Requirements: NFR-02
TEST_F(LagCompensationTest, ASeenTimeAsOldAsTheCapIsJudgedWhereTheTargetWasThen) {
  const augusta::tick::Tick seen = Next() - kCapTicks;

  EXPECT_EQ(Shoot(BobAt(seen), seen, 0.0F).size(), 1U);
}

// 280 ms is 16.8 ticks, short of the cap: judged where it reports, not at the cap.
// Requirements: NFR-02
TEST_F(LagCompensationTest, ASeenTimeShortOfTheCapIsJudgedWhereItReports) {
  const augusta::tick::Tick seen = Next() - 17;

  EXPECT_EQ(Shoot(BobAt(seen, 0.2F), seen, 0.2F).size(), 1U);
}

// A shooter with a delay past the cap still fires, and is judged against the
// oldest Seen time the cap allows, not against the one it reports.
// Requirements: NFR-02, US-15
TEST_F(LagCompensationTest, ASeenTimeOlderThanTheCapIsJudgedAtTheCapNotRefused) {
  EXPECT_EQ(Shoot(BobAt(Next() - kCapTicks), Next() - 40, 0.0F).size(), 1U);
  EXPECT_TRUE(Shoot(BobAt(Next() - 40), Next() - 40, 0.0F).empty());
  // A Seen time of no tick at all, as of a client that reports none.
  EXPECT_EQ(Shoot(BobAt(Next() - kCapTicks), 0, 0.0F).size(), 1U);
}

// No client has been shown more than the last tick's State.
// Requirements: NFR-02, US-15
TEST_F(LagCompensationTest, ASeenTimeNewerThanTheLastStateIsJudgedAtTheLastState) {
  EXPECT_EQ(Shoot(BobAt(Next() - 1), Next() + 100, 0.0F).size(), 1U);
  EXPECT_EQ(Shoot(BobAt(Next() - 1), Next() - 1, 0.5F).size(), 1U);
}

// Requirements: NFR-02, US-15
TEST_F(LagCompensationTest, AFractionOutsideZeroToOneIsHeldWithinIt) {
  augusta::tick::Tick seen = Next() - 6;
  EXPECT_EQ(Shoot(BobAt(seen + 1), seen, 7.0F).size(), 1U);
  seen = Next() - 6;
  EXPECT_EQ(Shoot(BobAt(seen), seen, -3.0F).size(), 1U);
}

// The Shooter's delay of every round a tick fired is what it is judged at, in seconds.
// Requirements: NFR-02
TEST_F(LagCompensationTest, ATickReportsTheShootersDelayOfEachRoundItFiredInSeconds) {
  Command command = Firing();
  command.seen_tick = Next() - 10;

  const std::vector<float> delays = TickWith(command).shooters_delays;

  ASSERT_EQ(delays.size(), 1U);
  EXPECT_FLOAT_EQ(delays[0], 10.0F / kTickRate);
}

// Requirements: NFR-02
TEST_F(LagCompensationTest, ARoundHeldAtTheCapReportsExactlyTheCap) {
  Command command = Firing();
  command.seen_tick = Next() - 40;

  const std::vector<float> delays = TickWith(command).shooters_delays;

  ASSERT_EQ(delays.size(), 1U);
  EXPECT_EQ(delays[0], 0.3F);
}

// Requirements: NFR-02
TEST_F(LagCompensationTest, ARoundShortOfTheCapReportsItsOwnDelay) {
  Command command = Firing();
  command.seen_tick = Next() - 17;
  command.seen_fraction = 0.2F;

  const std::vector<float> delays = TickWith(command).shooters_delays;

  ASSERT_EQ(delays.size(), 1U);
  EXPECT_NEAR(delays[0], 0.28F, 1e-5F);
}

// Requirements: NFR-02
TEST_F(LagCompensationTest, ATickThatFiresNothingReportsNoShootersDelay) {
  EXPECT_TRUE(TickWith(Command{}).shooters_delays.empty());
}

// Carol stands a meter right of Alice and fires on the same tick from an older
// Seen time: each round is judged against Bob where its own Seen time showed him.
// Requirements: US-11, NFR-02
TEST_F(LagCompensationTest, RoundsFiredOnOneTickAreEachJudgedAtTheirOwnShootersDelay) {
  constexpr EntityId kCarol = static_cast<EntityId>(3);
  world_.AddPlayer(kCarol, alice_ + Vec3(1.0F, 0.5F, 0.0F), Sliver());
  Vec3 carol;
  for (int i = 0; i < kSettleTicks; ++i) {
    for (const auto& entry : Tick(Command{}).bodies) {
      if (entry.entity == kCarol) {
        carol = entry.body.position;
      }
    }
  }
  const augusta::tick::Tick alice_seen = Next() - 2;
  const augusta::tick::Tick carol_seen = Next() - 10;
  // Bob walks 5 cm a tick: the two Seen times show him 40 cm apart.
  ASSERT_GT(BobAt(alice_seen).x - BobAt(carol_seen).x, 0.3F);
  const auto firing_at = [](const Vec3& eye, const Vec3& feet, augusta::tick::Tick seen) {
    const Vec3 aim = feet + Vec3(0.0F, kAimHeight, 0.0F) - eye;
    Command command = Firing();
    command.yaw = std::atan2(-aim.x, -aim.z);
    command.pitch = std::asin(aim.y / augusta::math::Length(aim));
    command.seen_tick = seen;
    return command;
  };

  const State state =
      world_
          .Tick({PlayerCommand{.entity = kAlice,
                               .command = firing_at(alice_ + Sliver().eye, BobAt(alice_seen), alice_seen)},
                 PlayerCommand{.entity = kBob, .command = bob_},
                 PlayerCommand{.entity = kCarol,
                               .command = firing_at(carol + Sliver().eye, BobAt(carol_seen), carol_seen)}},
                kTick)
          .state;

  ASSERT_EQ(state.hits.size(), 2U);
  EXPECT_EQ(state.hits[0].target, kBob);
  EXPECT_EQ(state.hits[1].target, kBob);
  EXPECT_NE(state.hits[0].shooter, state.hits[1].shooter);
}

// Bob walks 35 m away: at 10 m a tick, a round crosses his path on its fourth
// tick, three ticks after it is fired.
class LongShotTest : public LagCompensationTest {
 protected:
  static constexpr std::uint32_t kFlightTicks = 3;

  LongShotTest() : LagCompensationTest(35.0F) {}
};

// The lead is judged on the shooter's screen: by the time the round arrives,
// its screen shows Bob three ticks further along than when it fired.
// Requirements: US-10, NFR-02
TEST_F(LongShotTest, ABulletInFlightKeepsItsShootersDelaySoACorrectlyLedMovingTargetIsHit) {
  const augusta::tick::Tick seen = Next() - 8;

  const std::vector<Hit> hits = Shoot(BobAt(seen + kFlightTicks), seen, 0.0F);

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].target, kBob);
}

// With the freshest Seen time there is, a round is judged a tick behind the
// present: Bob is long past where that lead put it.
// Requirements: US-10, NFR-02
TEST_F(LongShotTest, TheSameLeadJudgedAgainstThePresentMisses) {
  const augusta::tick::Tick seen = Next() - 8;

  EXPECT_TRUE(Shoot(BobAt(seen + kFlightTicks), Next() - 1, 0.0F).empty());
}

// A bullet flies on after its shooter has left the Match, and still does its damage.
// Requirements: US-10
TEST_F(HitTest, ABulletWhoseShooterLeftStillHits) {
  // 10 m a tick and Bob 40 m away: the round is still flying when Alice leaves.
  world_.RemovePlayer(kBob);
  world_.AddPlayer(kBob, Vec3(0.0F, 0.5F, -40.0F), Target());
  Wait(kSettleTicks);
  ASSERT_TRUE(Tick(FiringAt(Vec3(0.0F, kTorsoHeight, 0.0F))).hits.empty());

  world_.RemovePlayer(kAlice);
  std::vector<Hit> hits;
  for (int i = 0; i < 6; ++i) {
    const State state = world_.Tick({}, kTick).state;
    hits.insert(hits.end(), state.hits.begin(), state.hits.end());
  }

  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].shooter, kAlice);
  EXPECT_EQ(hits[0].target, kBob);
}

// Alice's rifle, held where her view puts it (ADR-0064): the test rifle with
// its markers authored along -Z, the Grip at its origin, the Sight 8 cm above
// it and 10 cm behind, and the Muzzle 2 cm above it and 70 cm ahead, so the
// Muzzle is 6 cm below the Sight and 80 cm ahead of it. From the hip, it is
// held 20 cm right of the ADS hold, 30 cm below and 10 cm back. It kicks up by
// 1/32 rad a round and never recovers.
class HeldRifleTest : public FireTest {
 protected:
  static constexpr float kViewYaw = 0.75F;
  static constexpr float kViewPitch = -0.25F;
  static constexpr float kKick = 1.0F / 32.0F;
  static constexpr float kTolerance = 0.002F;

  static Parameters WithAHipOffsetAndRecoil() {
    Parameters parameters = WithTheTestRifle();
    parameters.rifle.hip_offset = Vec3(0.2F, -0.3F, 0.1F);
    parameters.rifle.recoil_pattern = {{.pitch = kKick, .yaw = 0.0F}};
    return parameters;
  }

  static augusta::weapon::Markers TheRifle() {
    return augusta::weapon::Markers{
        .grip = augusta::math::Mat4(1.0F),
        .muzzle = glm::translate(augusta::math::Mat4(1.0F), Vec3(0.0F, 0.02F, -0.7F)),
        .sight = glm::translate(augusta::math::Mat4(1.0F), Vec3(0.0F, 0.08F, 0.1F)),
    };
  }

  HeldRifleTest() : FireTest(WithAHipOffsetAndRecoil(), TheRifle()) {}

  static Command Aiming(float yaw, float pitch, bool ads) {
    Command command{};
    command.yaw = yaw;
    command.pitch = pitch;
    command.ads = ads;
    return command;
  }

  // Where Alice's rifle is after a tick of command.
  augusta::simulation::HeldRifle HeldAfter(const Command& command) {
    Tick(command);
    const auto held = world_.HeldRifleOf(kAlice);
    EXPECT_TRUE(held.has_value());
    return held.value_or(augusta::simulation::HeldRifle{});
  }

  static Vec3 PositionOf(const augusta::math::Mat4& marker) { return augusta::math::TranslationOf(marker); }

  // Where a marker points: its -Z, as the barrel does.
  static Vec3 PointingOf(const augusta::math::Mat4& marker) { return augusta::math::Normalize(-Vec3(marker[2])); }

  static void ExpectNear(const Vec3& actual, const Vec3& expected) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
  }
};

// Requirements: US-06
TEST_F(HeldRifleTest, InAdsTheSightIsOnTheEyeAndTheMuzzleAheadOfItAlongTheView) {
  const Vec3 eye = Feet() + Vec3(0.0F, 1.5F, 0.0F);

  const auto held = HeldAfter(Aiming(0.0F, 0.0F, true));

  ExpectNear(PositionOf(held.sight), eye);
  ExpectNear(PositionOf(held.muzzle), eye + Vec3(0.0F, -0.06F, -0.8F));
  ExpectNear(PointingOf(held.muzzle), Vec3(0.0F, 0.0F, -1.0F));
}

// Requirements: US-06
TEST_F(HeldRifleTest, InAdsTheBarrelFollowsTheYawAndThePitchOfTheView) {
  const Vec3 eye = Feet() + Vec3(0.0F, 1.5F, 0.0F);
  const Vec3 view = augusta::command::ViewDirection(kViewYaw, kViewPitch);

  const auto held = HeldAfter(Aiming(kViewYaw, kViewPitch, true));

  ExpectNear(PositionOf(held.sight), eye);
  ExpectNear(PointingOf(held.muzzle), view);
  const Vec3 ahead = PositionOf(held.muzzle) - eye;
  EXPECT_NEAR(augusta::math::Dot(ahead, view), 0.8F, kTolerance);
  EXPECT_NEAR(augusta::math::Length(ahead - (view * augusta::math::Dot(ahead, view))), 0.06F, kTolerance);
}

// Requirements: US-07
TEST_F(HeldRifleTest, FromTheHipTheRifleIsHeldAtTheHipOffsetFromTheAdsHold) {
  const Vec3 eye = Feet() + Vec3(0.0F, 1.5F, 0.0F);

  const auto held = HeldAfter(Aiming(0.0F, 0.0F, false));

  ExpectNear(PositionOf(held.sight), eye + Vec3(0.2F, -0.3F, 0.1F));
  ExpectNear(PositionOf(held.muzzle), eye + Vec3(0.2F, -0.36F, -0.7F));
}

// Requirements: US-07
TEST_F(HeldRifleTest, FromTheHipTheMuzzleIsAheadOfTheBodyAlongTheViewAndOffTheLineOfSight) {
  const Vec3 eye = Feet() + Vec3(0.0F, 1.5F, 0.0F);
  const Vec3 view = augusta::command::ViewDirection(kViewYaw, kViewPitch);

  const auto held = HeldAfter(Aiming(kViewYaw, kViewPitch, false));

  const Vec3 ahead = PositionOf(held.muzzle) - eye;
  EXPECT_GT(augusta::math::Dot(ahead, view), 0.5F);
  EXPECT_GT(augusta::math::Length(ahead - (view * augusta::math::Dot(ahead, view))), 0.2F);
  ExpectNear(PointingOf(held.muzzle), view);
}

// The eye is 1.5 m up a body 2.1 m tall standing and 1.3 m crouching.
// Requirements: US-06
TEST_F(HeldRifleTest, TheSightFollowsTheEyeLoweredForTheStance) {
  Command crouched = Aiming(0.0F, 0.0F, true);
  crouched.movement.desired_stance = Stance::kCrouching;
  const Vec3 feet = Feet();

  const auto held = HeldAfter(crouched);

  ExpectNear(PositionOf(held.sight), feet + Vec3(0.0F, 1.5F * 1.3F / 2.1F, 0.0F));
}

// Requirements: US-09
TEST_F(HeldRifleTest, TheBarrelFollowsTheRecoilOffset) {
  Command firing = Aiming(kViewYaw, kViewPitch, false);
  firing.fire = true;

  const State state = Tick(firing);
  const auto held = world_.HeldRifleOf(kAlice);

  ASSERT_TRUE(held.has_value());
  ASSERT_EQ(state.bodies.front().rifle.recoil.pitch, kKick);
  ExpectNear(PointingOf(held->muzzle), augusta::command::ViewDirection(kViewYaw, kViewPitch + kKick));
}

// Requirements: US-07
TEST_F(HeldRifleTest, APlayerNotInTheWorldHoldsNoRifle) { EXPECT_FALSE(world_.HeldRifleOf(kBob).has_value()); }

}  // namespace
