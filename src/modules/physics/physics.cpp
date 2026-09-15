#include "augusta/physics.h"

namespace augusta::physics {

// TODO(sergioffpc): every method below is a placeholder - none of them
// wrap PhysX yet (ADR-0002). Just enough is defined here for callers to
// construct/link against this module; nothing here is physically
// meaningful.

World::World([[maybe_unused]] const StaminaConfig& config) {
  // TODO(sergioffpc): create the PhysX scene/physics for this World.
}

BodyHandle World::CreateBody([[maybe_unused]] const math::Vec3& initial_position) {
  // TODO(sergioffpc): create a PhysX rigid body/capsule controller.
  return {};
}

void World::DestroyBody([[maybe_unused]] BodyHandle handle) {
  // TODO(sergioffpc): destroy the PhysX rigid body for handle.
}

BodyState World::Step([[maybe_unused]] BodyHandle handle, [[maybe_unused]] const MovementInput& input,
                      [[maybe_unused]] float delta_time) {
  // TODO(sergioffpc): resolve movement/stance/stamina via PhysX.
  return {};
}

void World::SetState([[maybe_unused]] BodyHandle handle, [[maybe_unused]] const BodyState& state) {
  // TODO(sergioffpc): overwrite the PhysX body's transform/velocity.
}

BodyState World::Reconcile([[maybe_unused]] BodyHandle handle, const BodyState& authoritative) {
  // TODO(sergioffpc): snap/blend correction (ADR-0004) - returns the
  // authoritative state unchanged for now, i.e. an immediate snap with
  // no blending.
  return authoritative;
}

RaycastHit World::Raycast([[maybe_unused]] const math::Vec3& origin, [[maybe_unused]] const math::Vec3& direction,
                          [[maybe_unused]] float max_distance) const {
  // TODO(sergioffpc): cast against PhysX bodies. No bodies to hit yet.
  return {};
}

}  // namespace augusta::physics
