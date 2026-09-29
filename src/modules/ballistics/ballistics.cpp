#include "augusta/ballistics.h"

#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::ballistics {

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

StepResult World::Step(BulletHandle handle, float delta_time, [[maybe_unused]] const physics::World& physics_world) {
  Bullet& bullet = bullets_.at(handle);
  BulletState& state = bullet.state;

  // Semi-implicit Euler: the new velocity moves the bullet, so the drop
  // over a flight does not depend on how its ticks are split.
  state.velocity.y -= bullet.config.gravity * delta_time;
  state.position += state.velocity * delta_time;

  // TODO(sergioffpc): raycast this tick's segment against physics_world for
  // a player hit (US-11), once Raycast sees a body where it is (M4).

  StepResult result;
  result.state = state;
  if (math::Length(state.position - bullet.origin) > bullet.config.max_range) {
    result.outcome = Outcome::kExpired;
    bullets_.erase(handle);
  }
  return result;
}

}  // namespace augusta::ballistics
