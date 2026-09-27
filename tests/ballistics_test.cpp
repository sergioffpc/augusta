#include "augusta/ballistics.h"

#include <cstddef>
#include <format>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"

namespace {

using augusta::ballistics::BulletConfig;
using augusta::ballistics::Outcome;
using augusta::ballistics::StepResult;
using augusta::ballistics::World;
using augusta::math::Length;
using augusta::math::Vec3;
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
    const StepResult result = world.Step(bullet, kFixedTick, physics_world);
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

TEST(BallisticsWorldTest, BulletTravelsAlongItsDirectionAtItsSpeed) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet = world.Fire(Vec3(0.0F), Vec3(0.0F, 0.0F, -2.0F), 800.0F, {.gravity = 0.0F, .max_range = 1000.0F});

  const StepResult result = world.Step(bullet, kFixedTick, physics_world);

  EXPECT_EQ(result.outcome, Outcome::kInFlight);
  EXPECT_NEAR(result.state.position.z, -800.0F * kFixedTick, 1e-4F);
  EXPECT_NEAR(Length(result.state.velocity), 800.0F, 1e-3F);
}

TEST(BallisticsWorldTest, GravityDropsTheBulletMoreEachTick) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto bullet =
      world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 800.0F, {.gravity = kGravity, .max_range = 1000.0F});

  const float first = world.Step(bullet, kFixedTick, physics_world).state.position.y;
  const float second = world.Step(bullet, kFixedTick, physics_world).state.position.y;

  EXPECT_LT(first, 0.0F);
  EXPECT_LT(second - first, first);
}

TEST(BallisticsWorldTest, BulletExpiresOncePastItsMaxRange) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  // 10 units a tick against a 25-unit range: the third tick crosses it.
  const auto bullet =
      world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 10.0F / kFixedTick, {.gravity = 0.0F, .max_range = 25.0F});

  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world).outcome, Outcome::kInFlight);
  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world).outcome, Outcome::kInFlight);
  EXPECT_EQ(world.Step(bullet, kFixedTick, physics_world).outcome, Outcome::kExpired);
}

TEST(BallisticsWorldTest, EachBulletKeepsItsOwnConfig) {
  const augusta::physics::World physics_world{StaminaConfig{}};
  World world;
  const auto heavy = world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 800.0F, {.gravity = 20.0F, .max_range = 1000.0F});
  const auto light = world.Fire(Vec3(0.0F), Vec3(1.0F, 0.0F, 0.0F), 800.0F, {.gravity = 0.0F, .max_range = 1000.0F});

  EXPECT_LT(world.Step(heavy, kFixedTick, physics_world).state.position.y, 0.0F);
  EXPECT_EQ(world.Step(light, kFixedTick, physics_world).state.position.y, 0.0F);
}

// NFR-03: every golden shot, fired again here, follows its recorded trajectory
// within the tolerance, tick for tick, and ends on the same tick. It runs on the
// Windows (MSVC) and Linux runners alike, so a compiler that computes a
// different trajectory fails its own runner (ADR-0013).
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
