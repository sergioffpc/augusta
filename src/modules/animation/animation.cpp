#include "augusta/animation.h"

#include <vector>

namespace augusta::animation {

// TODO(sergioffpc): every method below is a placeholder - there is no
// skeleton/rig format yet (see this module's header comment). Just
// enough is defined here for callers to construct/link against this
// module.

// Nothing to own yet - no animators exist until CreateAnimator is called.
Engine::Engine() = default;

AnimatorHandle Engine::CreateAnimator() {
  // TODO(sergioffpc): allocate per-character blend state.
  return {};
}

void Engine::DestroyAnimator([[maybe_unused]] AnimatorHandle handle) {
  // TODO(sergioffpc): free handle's blend state.
}

Pose Engine::Update([[maybe_unused]] AnimatorHandle handle, [[maybe_unused]] const LocomotionInput& input,
                    [[maybe_unused]] const std::vector<Action>& actions, [[maybe_unused]] float delta_time) {
  // TODO(sergioffpc): blend locomotion/aim state and play actions, once
  // there's a skeleton/rig format to evaluate a Pose against.
  return {};
}

}  // namespace augusta::animation
