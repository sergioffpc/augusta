#include "augusta/ballistics.h"

namespace augusta::ballistics {

// TODO(sergioffpc): every method below is a placeholder - none of them
// integrate gravity or test physics_world::Raycast yet. Just enough is
// defined here for callers to construct/link against this module.

World::World() {
  // Nothing to own yet - no bullets exist until Fire is called.
}

BulletHandle World::Fire([[maybe_unused]] const math::Vec3& origin, [[maybe_unused]] const math::Vec3& direction,
                         [[maybe_unused]] float initial_speed, [[maybe_unused]] const BulletConfig& config) {
  // TODO(sergioffpc): track a new in-flight bullet.
  return {};
}

StepResult World::Step([[maybe_unused]] BulletHandle handle, [[maybe_unused]] float delta_time,
                       [[maybe_unused]] const physics::World& physics_world) {
  // TODO(sergioffpc): integrate gravity and raycast against
  // physics_world for a player hit (US-10, US-11).
  return {};
}

}  // namespace augusta::ballistics
