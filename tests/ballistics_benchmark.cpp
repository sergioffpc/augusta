#include <array>
#include <cstddef>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/ballistics.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// One bullet's ballistics::World::Step (ADR-0002): what SimulationWorld's
// Ballistics phase runs for every bullet in flight, every tick. The bullet
// flies over a floor, past every other player's hitboxes, so each Step tests
// its segment against the Map and against all of them, until it is past its
// range.
namespace {

using augusta::ballistics::BodyPart;
using augusta::ballistics::BulletConfig;
using augusta::ballistics::Hitbox;
using augusta::ballistics::Outcome;
using augusta::ballistics::TargetId;
using augusta::ballistics::Triangle;
using augusta::math::Vec3;
using augusta::physics::CollisionMesh;
using augusta::physics::StaminaConfig;

constexpr float kTick = 1.0F / 60.0F;
constexpr std::size_t kTargets = 7;

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A square facing the bullet's way, half_size wide each side of center.
std::array<Triangle, 2> Square(Vec3 center, float half_size) {
  const Vec3 right(half_size, 0.0F, 0.0F);
  const Vec3 up(0.0F, half_size, 0.0F);
  return {Triangle{.a = center - right - up, .b = center + right - up, .c = center + right + up},
          Triangle{.a = center - right - up, .b = center + right + up, .c = center - right + up}};
}

void BM_BallisticsStep(benchmark::State& state) {
  augusta::physics::World map{StaminaConfig{}};
  if (!map.AddCollisionMesh(Floor()).has_value()) {
    state.SkipWithError("the floor does not build");
    return;
  }

  // Every other player of a full Match, standing in a row beside the bullet's path.
  std::vector<std::array<Triangle, 2>> squares;
  for (std::size_t target = 0; target < kTargets; ++target) {
    const float x = 3.0F * static_cast<float>(target + 1);
    squares.push_back(Square(Vec3(x, 1.6F, -20.0F), 0.15F));
    squares.push_back(Square(Vec3(x, 1.1F, -20.0F), 0.3F));
    squares.push_back(Square(Vec3(x, 0.4F, -20.0F), 0.4F));
  }
  std::vector<Hitbox> hitboxes;
  for (std::size_t index = 0; index < squares.size(); ++index) {
    constexpr std::array kParts{BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb};
    hitboxes.push_back(Hitbox{.target = static_cast<TargetId>(index / kParts.size()),
                              .part = kParts.at(index % kParts.size()),
                              .triangles = squares[index]});
  }

  const Vec3 origin(0.0F, 1.5F, 0.0F);
  const Vec3 direction(0.0F, 0.0F, -1.0F);
  const BulletConfig config{.gravity = 9.81F, .max_range = 1000.0F};
  augusta::ballistics::World world;
  auto bullet = world.Fire(origin, direction, 800.0F, config);
  for (auto _ : state) {
    // A bullet past its range is replaced, its Fire timed with the Steps: one
    // in about 75.
    if (world.Step(bullet, kTick, map, hitboxes).outcome != Outcome::kInFlight) {
      bullet = world.Fire(origin, direction, 800.0F, config);
    }
  }
}
BENCHMARK(BM_BallisticsStep);

}  // namespace
