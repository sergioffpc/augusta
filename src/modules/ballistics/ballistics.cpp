#include "augusta/ballistics.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::ballistics {

namespace {

// Where along from + t * segment, t in [0, 1], the segment crosses triangle, from
// either side (Moller-Trumbore); nullopt if it does not.
std::optional<float> CrossingFraction(const math::Vec3& from, const math::Vec3& segment, const Triangle& triangle) {
  const math::Vec3 edge1 = triangle.b - triangle.a;
  const math::Vec3 edge2 = triangle.c - triangle.a;
  const math::Vec3 p = math::Cross(segment, edge2);
  const float determinant = math::Dot(edge1, p);
  // Parallel to the triangle's plane, or no segment at all.
  if (std::abs(determinant) < std::numeric_limits<float>::min()) {
    return std::nullopt;
  }
  const float inverse = 1.0F / determinant;
  const math::Vec3 s = from - triangle.a;
  const float u = math::Dot(s, p) * inverse;
  if (u < 0.0F || u > 1.0F) {
    return std::nullopt;
  }
  const math::Vec3 q = math::Cross(s, edge1);
  const float v = math::Dot(segment, q) * inverse;
  if (v < 0.0F || u + v > 1.0F) {
    return std::nullopt;
  }
  const float t = math::Dot(edge2, q) * inverse;
  if (t < 0.0F || t > 1.0F) {
    return std::nullopt;
  }
  return t;
}

}  // namespace

std::uint32_t MaxFlightSteps(float step_seconds) {
  // A thousandth of a step off, so a step of 1/rate seconds a float rounds down
  // does not count one more step than kMaxFlightTime holds.
  constexpr double kRoundingSlack = 0.001;
  const double steps = std::chrono::duration<double>(kMaxFlightTime).count() / step_seconds;
  return static_cast<std::uint32_t>(std::ceil(steps - kRoundingSlack));
}

World::World() = default;

BulletHandle World::Fire(const math::Vec3& origin, const math::Vec3& direction, float initial_speed,
                         const BulletConfig& config) {
  const auto handle = static_cast<BulletHandle>(next_handle_++);
  bullets_.emplace(handle, Bullet{
                               .origin = origin,
                               .state = {.position = origin, .velocity = math::Normalize(direction) * initial_speed},
                               .config = config,
                           });
  return handle;
}

StepResult World::Step(BulletHandle handle, float delta_time, const physics::World& map,
                       std::span<const Hitbox> hitboxes) {
  Bullet& bullet = bullets_.at(handle);
  BulletState& state = bullet.state;
  const math::Vec3 from = state.position;
  state = Advanced(bullet, delta_time);

  StepResult result;
  result.state = state;
  const math::Vec3 segment = state.position - from;

  // The nearest of the Map and every hitbox along the segment, as a fraction of it.
  std::optional<float> nearest;
  const physics::RaycastHit map_hit = map.RaycastMap(from, segment, math::Length(segment));
  if (map_hit.has_hit) {
    nearest = map_hit.distance / math::Length(segment);
    result.outcome = Outcome::kHitMap;
    result.impact_point = map_hit.point;
  }
  for (const Hitbox& hitbox : hitboxes) {
    for (const Triangle& triangle : hitbox.triangles) {
      const std::optional<float> crossing = CrossingFraction(from, segment, triangle);
      if (crossing && (!nearest || *crossing < *nearest)) {
        nearest = crossing;
        result.outcome = Outcome::kHitPlayer;
        result.target = hitbox.target;
        result.part = hitbox.part;
        result.impact_point = from + segment * *crossing;
      }
    }
  }

  ++bullet.steps;
  if (!nearest && (math::Length(state.position - bullet.origin) > bullet.config.max_range ||
                   bullet.steps >= MaxFlightSteps(delta_time))) {
    result.outcome = Outcome::kExpired;
  }
  if (result.outcome != Outcome::kInFlight) {
    bullets_.erase(handle);
  }
  return result;
}

Segment World::NextSegment(BulletHandle handle, float delta_time) const {
  const Bullet& bullet = bullets_.at(handle);
  return Segment{.from = bullet.state.position, .to = Advanced(bullet, delta_time).position};
}

BulletState World::Advanced(const Bullet& bullet, float delta_time) {
  // Semi-implicit Euler: the new velocity moves the bullet, so the drop
  // over a flight does not depend on how its ticks are split.
  BulletState state = bullet.state;
  state.velocity.y -= bullet.config.gravity * delta_time;
  state.position += state.velocity * delta_time;
  return state;
}

}  // namespace augusta::ballistics
