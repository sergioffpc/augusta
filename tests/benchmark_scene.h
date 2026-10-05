#ifndef AUGUSTA_TESTS_BENCHMARK_SCENE_H_
#define AUGUSTA_TESTS_BENCHMARK_SCENE_H_

#include <array>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// What the benchmarks' worlds are made of: a flat Map and players whose body
// parts are squares, sized like a standing body's, facing along Z.
namespace augusta::benchmarks {

// The example scenario's rifle (parameters.lua), in meters per second.
inline constexpr float kMuzzleVelocity = 800.0F;

// A floor 200 meters across, at height 0.
inline physics::CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return physics::CollisionMesh{.points = {math::Vec3(-kExtent, 0.0F, -kExtent), math::Vec3(-kExtent, 0.0F, kExtent),
                                           math::Vec3(kExtent, 0.0F, kExtent), math::Vec3(kExtent, 0.0F, -kExtent)},
                                .indices = {0, 1, 2, 0, 2, 3}};
}

// One body part: a square half_size wide each side of its center, height
// meters above the feet.
struct BodySquare {
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
  float height = 0.0F;
  float half_size = 0.0F;
};

inline constexpr std::array kBodySquares{
    BodySquare{.part = ballistics::BodyPart::kHead, .height = 1.6F, .half_size = 0.15F},
    BodySquare{.part = ballistics::BodyPart::kTorso, .height = 1.1F, .half_size = 0.3F},
    BodySquare{.part = ballistics::BodyPart::kLimb, .height = 0.4F, .half_size = 0.4F},
};

// The two triangles of square, for a body whose feet stand at feet.
inline std::vector<ballistics::Triangle> Triangles(const BodySquare& square, math::Vec3 feet) {
  const math::Vec3 center = feet + math::Vec3(0.0F, square.height, 0.0F);
  const math::Vec3 right(square.half_size, 0.0F, 0.0F);
  const math::Vec3 up(0.0F, square.half_size, 0.0F);
  return {ballistics::Triangle{.a = center - right - up, .b = center + right - up, .c = center + right + up},
          ballistics::Triangle{.a = center - right - up, .b = center + right + up, .c = center - right + up}};
}

}  // namespace augusta::benchmarks

#endif  // AUGUSTA_TESTS_BENCHMARK_SCENE_H_
