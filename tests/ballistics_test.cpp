#include "augusta/ballistics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"

namespace {

using augusta::ballistics::BodyPart;
using augusta::ballistics::BulletConfig;
using augusta::ballistics::Hitbox;
using augusta::ballistics::Outcome;
using augusta::ballistics::Segment;
using augusta::ballistics::StepResult;
using augusta::ballistics::TargetId;
using augusta::ballistics::Triangle;
using augusta::ballistics::World;
using augusta::math::Length;
using augusta::math::Vec3;
using augusta::physics::CollisionMesh;
using augusta::physics::StaminaConfig;

constexpr float kFixedTick = 1.0F / 60.0F;
constexpr float kGravity = 9.81F;

// NFR-03's defined tolerance: how far, in engine units (meters), a bullet's
// position at any tick may be from the golden trajectory's. A millimeter is far
// below anything a hitbox or a tracer can tell apart, and far above the rounding
// of the float arithmetic one compiler may order differently from another.
constexpr float kNfr03Tolerance = 0.001F;

// Bounds a golden flight, so a shot that never leaves its range cannot hang the
// test; every shot below expires well before it.
constexpr int kMaxFlightTicks = 1200;

// A fire event (NFR-03's stimulus) and the position after each tick until the
// bullet expires.
struct Trajectory {
  Vec3 origin;
  Vec3 direction;
  float speed = 0.0F;
  BulletConfig config;
  std::vector<Vec3> positions;
};

// Fires shot's event into a fresh World and records its flight.
std::vector<Vec3> Fly(const Trajectory& shot) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet = world.Fire(shot.origin, shot.direction, shot.speed, shot.config);

  std::vector<Vec3> positions;
  for (int tick = 0; tick < kMaxFlightTicks; ++tick) {
    const StepResult result = world.Step(bullet, kFixedTick, physics_world, {});
    positions.push_back(result.state.position);
    if (result.outcome != Outcome::kInFlight) {
      break;
    }
  }
  return positions;
}

// A spread of directions (flat, up, down, steep, off-axis), speeds and ranges.
std::vector<Trajectory> GoldenShots() {
  const auto shot = [](Vec3 direction, float speed, float max_range) {
    return Trajectory{.origin = Vec3(1.5F, 1.7F, -3.0F),
                      .direction = direction,
                      .speed = speed,
                      .config = {.gravity = kGravity, .max_range = max_range},
                      .positions = {}};
  };
  return {
      shot(Vec3(0.0F, 0.0F, -1.0F), 800.0F, 1000.0F),   // flat, rifle, full range
      shot(Vec3(1.0F, 1.0F, 0.0F), 800.0F, 1000.0F),    // 45 degrees up
      shot(Vec3(0.3F, -0.2F, -1.0F), 800.0F, 150.0F),   // down and off-axis
      shot(Vec3(0.0F, 6.0F, 1.0F), 350.0F, 400.0F),     // steep, slow: rises then falls
      shot(Vec3(-2.0F, 0.1F, 3.0F), 950.0F, 25.0F),     // fast, a two-tick flight
      shot(Vec3(0.7F, 0.05F, 0.7F), 350.0F, 1000.0F),   // slow, long drop
      shot(Vec3(-1.0F, -0.02F, -0.1F), 600.0F, 300.0F)  // slight downward, mid range
  };
}

// The golden file: per shot, a header line with the fire event, then one line
// per tick's position. Floats are written in their shortest exact form.
void WriteGolden(const std::string& path, const std::vector<Trajectory>& shots) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);  // LF on every platform
  out << "# NFR-03 golden ballistic trajectories, written by tests/ballistics_test.cpp.\n"
      << "# shot <origin xyz> <direction xyz> <speed> <gravity> <max_range> <ticks>, then one position per tick.\n";
  for (const Trajectory& shot : shots) {
    out << std::format("shot {} {} {} {} {} {} {} {} {} {}\n", shot.origin.x, shot.origin.y, shot.origin.z,
                       shot.direction.x, shot.direction.y, shot.direction.z, shot.speed, shot.config.gravity,
                       shot.config.max_range, shot.positions.size());
    for (const Vec3& position : shot.positions) {
      out << std::format("{} {} {}\n", position.x, position.y, position.z);
    }
  }
}

std::vector<Trajectory> ReadGolden(const std::string& path) {
  std::ifstream in(path);
  std::vector<Trajectory> shots;
  std::string word;
  while (in >> word) {
    if (word.starts_with('#')) {
      std::getline(in, word);
      continue;
    }
    Trajectory shot;
    std::size_t ticks = 0;
    in >> shot.origin.x >> shot.origin.y >> shot.origin.z >> shot.direction.x >> shot.direction.y >> shot.direction.z >>
        shot.speed >> shot.config.gravity >> shot.config.max_range >> ticks;
    shot.positions.resize(ticks);
    for (Vec3& position : shot.positions) {
      in >> position.x >> position.y >> position.z;
    }
    shots.push_back(shot);
  }
  return shots;
}

// Requirements: US-10
TEST(BallisticsWorldTest, BulletTravelsAlongItsDirectionAtItsSpeed) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet = world.Fire(Vec3(0.0F), Vec3(0.0F, 0.0F, -2.0F), 800.0F, {.gravity = 0.0F, .max_range = 1000.0F});

  const StepResult result = world.Step(bullet, kFixedTick, physics_world, {});

  EXPECT_EQ(result.outcome, Outcome::kInFlight);
  EXPECT_NEAR(result.state.position.z, -800.0F * kFixedTick, 1e-4F);
  EXPECT_NEAR(Length(result.state.velocity), 800.0F, 1e-3F);
}

// Requirements: US-10
TEST(BallisticsWorldTest, GravityDropsTheBulletMoreEachTick) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet =
      world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 800.0F, {.gravity = kGravity, .max_range = 1000.0F});

  const float first = world.Step(bullet, kFixedTick, physics_world, {}).state.position.y;
  const float second = world.Step(bullet, kFixedTick, physics_world, {}).state.position.y;

  EXPECT_LT(first, 0.0F);
  EXPECT_LT(second - first, first);
}

// Requirements: US-10
TEST(BallisticsWorldTest, BulletExpiresOncePastItsMaxRange) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  // 10 units a tick against a 25-unit range: the third tick crosses it.
  const auto bullet =
      world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 10.0F / kFixedTick, {.gravity = 0.0F, .max_range = 25.0F});

  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world, {}).outcome, Outcome::kInFlight);
  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world, {}).outcome, Outcome::kInFlight);
  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world, {}).outcome, Outcome::kExpired);
}

// Requirements: US-10
TEST(BallisticsWorldTest, TheNextSegmentIsTheMovementTheNextStepTests) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet =
      world.Fire(Vec3(0.0F), Vec3(1.0F, 0.5F, -2.0F), 800.0F, {.gravity = kGravity, .max_range = 1000.0F});
  const Vec3 before = world.Step(bullet, kFixedTick, physics_world, {}).state.position;

  const Segment next = world.NextSegment(bullet, kFixedTick);
  const Vec3 after = world.Step(bullet, kFixedTick, physics_world, {}).state.position;

  EXPECT_EQ(next.from, before);
  EXPECT_EQ(next.to, after);
}

// Requirements: US-10
TEST(BallisticsWorldTest, MaxFlightStepsIsTheMaxFlightTimeInStepsRoundedUp) {
  EXPECT_EQ(augusta::ballistics::MaxFlightSteps(2.0F), 3U);
  EXPECT_EQ(augusta::ballistics::MaxFlightSteps(0.3F), 17U);
  // Every tick rate, whichever way 1/rate rounds to a float.
  for (std::uint32_t rate = 1; rate <= std::numeric_limits<std::uint8_t>::max(); ++rate) {
    EXPECT_EQ(augusta::ballistics::MaxFlightSteps(1.0F / static_cast<float>(rate)), 5 * rate) << rate << " Hz";
  }
}

// A metre a second against the longest range a float holds: only the flight
// time ends it.
// Requirements: US-10
TEST(BallisticsWorldTest, ABulletStillFlyingAtTheMaxFlightTimeExpires) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet = world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 1.0F,
                                 {.gravity = 0.0F, .max_range = std::numeric_limits<float>::max()});
  const std::uint32_t steps = augusta::ballistics::MaxFlightSteps(kFixedTick);

  for (std::uint32_t step = 1; step < steps; ++step) {
    ASSERT_EQ(world.Step(bullet, kFixedTick, physics_world, {}).outcome, Outcome::kInFlight) << "step " << step;
  }
  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world, {}).outcome, Outcome::kExpired);
}

// Requirements: US-10
TEST(BallisticsWorldTest, EachBulletKeepsItsOwnConfig) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto heavy = world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 800.0F, {.gravity = 20.0F, .max_range = 1000.0F});
  const auto light = world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 800.0F, {.gravity = 0.0F, .max_range = 1000.0F});

  EXPECT_LT(world.Step(heavy, kFixedTick, physics_world, {}).state.position.y, 0.0F);
  EXPECT_EQ(world.Step(light, kFixedTick, physics_world, {}).state.position.y, 0.0F);
}

// ---- What a tick's segment hits ----

// Every test below fires along +x from x = 0, at 10 units a tick, so the first
// tick's segment runs from x = 0 to x = 10.
constexpr float kTenUnitsATick = 10.0F / kFixedTick;
const Vec3 kAlongX(1.0F, 0.0F, 0.0F);
const Vec3 kMuzzle(0.0F, 1.0F, 0.0F);
constexpr BulletConfig kNoDrop{.gravity = 0.0F, .max_range = 1000.0F};

// A square across the x axis at x, spanning y 0..2 and z -1..1.
std::array<Triangle, 2> Plane(float x) {
  const Vec3 low_near(x, 0.0F, -1.0F);
  const Vec3 low_far(x, 0.0F, 1.0F);
  const Vec3 high_far(x, 2.0F, 1.0F);
  const Vec3 high_near(x, 2.0F, -1.0F);
  return {Triangle{low_near, low_far, high_far}, Triangle{low_near, high_far, high_near}};
}

// A Map with a wall across the x axis at x.
std::unique_ptr<augusta::physics::World> MapWithWall(float x) {
  auto map = std::make_unique<augusta::physics::World>(StaminaConfig{});
  CollisionMesh wall;
  for (const Triangle& triangle : Plane(x)) {
    for (const Vec3& point : {triangle.a, triangle.b, triangle.c}) {
      wall.indices.push_back(static_cast<std::uint32_t>(wall.points.size()));
      wall.points.push_back(point);
    }
  }
  EXPECT_TRUE(map->AddCollisionMesh(wall).has_value());
  return map;
}

// Requirements: US-10
TEST(BallisticsHitTest, ABulletStopsOnTheMapWhereItCrossesIt) {
  const auto map = MapWithWall(5.0F);
  World world;
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, kNoDrop);

  const StepResult result = world.Step(bullet, kFixedTick, *map, {});

  EXPECT_EQ(result.outcome, Outcome::kHitMap);
  EXPECT_NEAR(result.impact_point.x, 5.0F, 1e-3F);
  EXPECT_NEAR(result.impact_point.y, 1.0F, 1e-3F);
}

// Requirements: US-10
TEST(BallisticsHitTest, ABulletShortOfTheMapStaysInFlight) {
  const auto map = MapWithWall(15.0F);
  World world;
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, kNoDrop);

  EXPECT_EQ(world.Step(bullet, kFixedTick, *map, {}).outcome, Outcome::kInFlight);
  EXPECT_EQ(world.Step(bullet, kFixedTick, *map, {}).outcome, Outcome::kHitMap);
}

// A player is hit through its Hitboxes only (ADR-0044): its controller's
// capsule, which the physics scene holds where the controller was made, is
// never what a bullet hits (ADR-0002).
// Requirements: US-11
TEST(BallisticsHitTest, ABulletPassesThroughAPlayersControllerWhenNoHitboxIsHandedIn) {
  augusta::physics::World map{StaminaConfig{}};
  map.CreateBody(Vec3(0.0F));
  // The controller's capsule stands on the world origin (ADR-0002); this line
  // crosses it, as a plain physics raycast confirms.
  const Vec3 muzzle(-5.0F, 0.0F, 0.0F);
  ASSERT_TRUE(map.Raycast(muzzle, kAlongX, 10.0F).has_hit);
  World world;
  const auto bullet = world.Fire(muzzle, kAlongX, kTenUnitsATick, kNoDrop);

  EXPECT_EQ(world.Step(bullet, kFixedTick, map, {}).outcome, Outcome::kInFlight);
}

// Requirements: US-11
TEST(BallisticsHitTest, ABulletThatCrossesAHitboxHitsItsTargetAndBodyPart) {
  const augusta::physics::World map{StaminaConfig{}};
  const auto plane = Plane(5.0F);
  const std::array hitboxes{Hitbox{.target = TargetId{7}, .part = BodyPart::kHead, .triangles = plane}};
  World world;
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, kNoDrop);

  const StepResult result = world.Step(bullet, kFixedTick, map, hitboxes);

  EXPECT_EQ(result.outcome, Outcome::kHitPlayer);
  EXPECT_EQ(result.target, TargetId{7});
  EXPECT_EQ(result.part, BodyPart::kHead);
  EXPECT_NEAR(result.impact_point.x, 5.0F, 1e-4F);
  EXPECT_NEAR(result.impact_point.y, 1.0F, 1e-4F);
}

// Requirements: US-11
TEST(BallisticsHitTest, ABulletThatMissesAHitboxStaysInFlight) {
  const augusta::physics::World map{StaminaConfig{}};
  const auto plane = Plane(5.0F);
  const std::array hitboxes{Hitbox{.target = TargetId{7}, .part = BodyPart::kTorso, .triangles = plane}};
  World world;
  // Level at y = 3, over the top of the hitbox.
  const auto bullet = world.Fire(Vec3(0.0F, 3.0F, 0.0F), kAlongX, kTenUnitsATick, kNoDrop);

  EXPECT_EQ(world.Step(bullet, kFixedTick, map, hitboxes).outcome, Outcome::kInFlight);
}

// Requirements: US-10, US-11
TEST(BallisticsHitTest, AWallInFrontOfAHitboxStopsTheBullet) {
  const auto map = MapWithWall(3.0F);
  const auto plane = Plane(5.0F);
  const std::array hitboxes{Hitbox{.target = TargetId{7}, .part = BodyPart::kTorso, .triangles = plane}};
  World world;
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, kNoDrop);

  const StepResult result = world.Step(bullet, kFixedTick, *map, hitboxes);

  EXPECT_EQ(result.outcome, Outcome::kHitMap);
  EXPECT_NEAR(result.impact_point.x, 3.0F, 1e-3F);
}

// Requirements: US-11
TEST(BallisticsHitTest, AHitboxInFrontOfAWallIsHit) {
  const auto map = MapWithWall(5.0F);
  const auto plane = Plane(3.0F);
  const std::array hitboxes{Hitbox{.target = TargetId{7}, .part = BodyPart::kTorso, .triangles = plane}};
  World world;
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, kNoDrop);

  const StepResult result = world.Step(bullet, kFixedTick, *map, hitboxes);

  EXPECT_EQ(result.outcome, Outcome::kHitPlayer);
  EXPECT_NEAR(result.impact_point.x, 3.0F, 1e-4F);
}

// Requirements: US-11
TEST(BallisticsHitTest, OfTwoPlayersInLineOnlyTheNearerIsHit) {
  const augusta::physics::World map{StaminaConfig{}};
  const auto far = Plane(6.0F);
  const auto near = Plane(4.0F);
  // Handed in far first, so the nearer one wins by distance, not by order.
  const std::array hitboxes{Hitbox{.target = TargetId{1}, .part = BodyPart::kTorso, .triangles = far},
                            Hitbox{.target = TargetId{2}, .part = BodyPart::kLimb, .triangles = near}};
  World world;
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, kNoDrop);

  const StepResult result = world.Step(bullet, kFixedTick, map, hitboxes);

  EXPECT_EQ(result.outcome, Outcome::kHitPlayer);
  EXPECT_EQ(result.target, TargetId{2});
  EXPECT_EQ(result.part, BodyPart::kLimb);
}

// Requirements: US-10, US-11
TEST(BallisticsHitTest, AHitIsNotAnExpiry) {
  const augusta::physics::World map{StaminaConfig{}};
  const auto plane = Plane(9.0F);
  const std::array hitboxes{Hitbox{.target = TargetId{7}, .part = BodyPart::kTorso, .triangles = plane}};
  World world;
  // The range runs out within the same tick the hitbox is crossed.
  const auto bullet = world.Fire(kMuzzle, kAlongX, kTenUnitsATick, {.gravity = 0.0F, .max_range = 9.5F});

  EXPECT_EQ(world.Step(bullet, kFixedTick, map, hitboxes).outcome, Outcome::kHitPlayer);
}

// NFR-03: every golden shot, fired again here, follows its recorded trajectory
// within the tolerance, tick for tick, and ends on the same tick. It runs on the
// Windows (MSVC) and Linux runners alike, so a compiler that computes a
// different trajectory fails its own runner (ADR-0013).
// Requirements: US-10, NFR-03
TEST(BallisticsGoldenTest, TrajectoriesMatchTheGoldenFileWithinNfr03Tolerance) {
  const std::vector<Trajectory> golden = ReadGolden(AUGUSTA_GOLDEN_TRAJECTORIES);
  ASSERT_EQ(golden.size(), GoldenShots().size()) << "regenerate " << AUGUSTA_GOLDEN_TRAJECTORIES;

  for (std::size_t i = 0; i < golden.size(); ++i) {
    SCOPED_TRACE(std::format("shot {}", i));
    const std::vector<Vec3> flown = Fly(golden[i]);
    ASSERT_EQ(flown.size(), golden[i].positions.size());
    for (std::size_t tick = 0; tick < flown.size(); ++tick) {
      SCOPED_TRACE(std::format("tick {}", tick));
      EXPECT_NEAR(flown[tick].x, golden[i].positions[tick].x, kNfr03Tolerance);
      EXPECT_NEAR(flown[tick].y, golden[i].positions[tick].y, kNfr03Tolerance);
      EXPECT_NEAR(flown[tick].z, golden[i].positions[tick].z, kNfr03Tolerance);
    }
  }
}

// Not a check: rewrites the golden file from GoldenShots() when the ballistics
// model changes on purpose. Disabled, so ctest never runs it; the
// augusta_golden_trajectories build target does (tests/CMakeLists.txt).
TEST(BallisticsGoldenTest, DISABLED_RegenerateGoldenFile) {
  std::vector<Trajectory> shots = GoldenShots();
  for (Trajectory& shot : shots) {
    shot.positions = Fly(shot);
    ASSERT_LT(shot.positions.size(), static_cast<std::size_t>(kMaxFlightTicks)) << "a golden shot must expire";
  }
  WriteGolden(AUGUSTA_GOLDEN_TRAJECTORIES, shots);
}

}  // namespace
