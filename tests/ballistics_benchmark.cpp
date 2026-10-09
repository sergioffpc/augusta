#include <cstddef>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/ballistics.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/primitives.h"
#include "benchmark_scene.h"

// One bullet's ballistics::World::Step (ADR-0002): what SimulationWorld's
// Ballistics phase runs for every bullet in flight, every tick. The bullet
// flies over a floor, past every other player's hitboxes, so each Step tests
// its segment against the Map and against all of them, until it is past its
// range.
namespace {

using augusta::ballistics::BulletConfig;
using augusta::ballistics::Hitbox;
using augusta::ballistics::Outcome;
using augusta::ballistics::TargetId;
using augusta::ballistics::Triangle;
using augusta::benchmarks::kBodySquares;
using augusta::benchmarks::kMuzzleVelocity;
using augusta::math::Vec3;
using augusta::physics::StaminaConfig;

constexpr float kTick = 1.0F / 60.0F;
// Every other player of a full Match.
constexpr std::size_t kTargets = augusta::primitives::kMaxPlayers - 1;

void BM_BallisticsStep(benchmark::State& state) {
  augusta::physics::World map{StaminaConfig{}};
  if (!map.AddCollisionMesh(augusta::benchmarks::Floor()).has_value()) {
    state.SkipWithError("the floor does not build");
    return;
  }

  // The targets stand in a row beside the bullet's path.
  std::vector<std::vector<Triangle>> triangles;
  for (std::size_t target = 0; target < kTargets; ++target) {
    const Vec3 feet(3.0F * static_cast<float>(target + 1), 0.0F, -20.0F);
    for (const auto& square : kBodySquares) {
      triangles.push_back(augusta::benchmarks::Triangles(square, feet));
    }
  }
  std::vector<Hitbox> hitboxes;
  for (std::size_t index = 0; index < triangles.size(); ++index) {
    hitboxes.push_back(Hitbox{.target = static_cast<TargetId>(index / kBodySquares.size()),
                              .part = kBodySquares.at(index % kBodySquares.size()).part,
                              .triangles = triangles[index]});
  }

  const Vec3 origin(0.0F, 1.5F, 0.0F);
  const Vec3 direction(0.0F, 0.0F, -1.0F);
  const BulletConfig config{.gravity = 9.81F, .max_range = 1000.0F};
  augusta::ballistics::World world;
  auto bullet = world.Fire(origin, direction, kMuzzleVelocity, config);
  for (auto _ : state) {
    // A bullet past its range is replaced, its Fire timed with the Steps: one
    // in about 75.
    if (world.Step(bullet, kTick, map, hitboxes).outcome != Outcome::kInFlight) {
      bullet = world.Fire(origin, direction, kMuzzleVelocity, config);
    }
  }
}
BENCHMARK(BM_BallisticsStep);

}  // namespace
