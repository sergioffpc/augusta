#ifndef AUGUSTA_ANIMATION_H_
#define AUGUSTA_ANIMATION_H_

#include <cstdint>
#include <vector>

#include "augusta/physics.h"

// augusta::animation drives PresentationWorld's Animation phase
// (ADR-0024): turning interpolated movement and weapon state into
// animation blend inputs for every player character the client can see
// - not just the local player. Unlike augusta::prediction (local player
// only), this runs client-side in PresentationWorld where remote
// players are visible too, so Engine is a many-characters, handle-based
// interface, the same shape as augusta::physics::World's many bodies.
//
// Scope, for now: locomotion/aim blend *parameters* and discrete action
// triggers only - not skeletal rig evaluation or mesh skinning. This
// project has no character/skeleton data format yet (augusta::
// ballistics's own header comment notes the same gap for per-body-part
// hitboxes), so there is nothing yet to evaluate a pose against.
// Engine::Update's return type (Pose) is a deliberately empty
// placeholder - same deferred-design posture as
// augusta::presentation::State - revisit once a skeleton/rig format
// exists (mesh import is ADR-0016; skinning/rigging isn't decided there
// yet).
namespace augusta::animation {

// Discrete, one-shot animation actions triggered by a gameplay event
// this frame (e.g. WeaponHandling firing or reloading). Not a full
// action/state-machine catalogue - extend as new gameplay needs a new
// action, same restraint as physics::Stance or input::Key.
enum class Action {
  kFire,
  kReload,
};

// This frame's locomotion/aim blend inputs for one character, derived
// from interpolated PredictionWorld movement (presentation::Phase::
// kInterpolation) and WeaponHandling state.
struct LocomotionInput {
  // Normalized movement speed, [0, 1] of max sprint speed - drives an
  // idle/walk/run blend.
  float speed = 0.0F;
  // Movement direction relative to facing, in radians (0 = forward,
  // positive = strafing right) - drives directional blending (e.g.
  // forward vs. strafe walk cycles).
  float direction = 0.0F;
  // Same stance vocabulary as physics::Stance (US-04) - selects
  // standing/crouching/prone blend trees.
  physics::Stance stance = physics::Stance::kStanding;
  // Aim pitch, in radians, for upper-body aim-offset blending -
  // independent of the lower body's locomotion blend.
  float aim_pitch = 0.0F;
};

// Opaque handle to one character's animation state, created by
// Engine::CreateAnimator. Valid only for the Engine instance that
// created it.
enum class AnimatorHandle : std::uint32_t {};

// One character's evaluated animation output for this frame.
// Deliberately empty for now - see header comment.
struct Pose {};

// Owns per-character animation blend state for every player character
// PresentationWorld drives this frame. The client constructs exactly
// one, on the Main/Render thread (ADR-0005) - same thread as
// presentation::World, which is expected to own this Engine and call
// Update once per visible character from its Animation phase.
class Engine {
 public:
  Engine();

  // Creates a new animator, one per visible player character (local or
  // remote). Returns a handle valid for the lifetime of this Engine or
  // until DestroyAnimator is called with it.
  [[nodiscard]] AnimatorHandle CreateAnimator();

  // Removes an animator and invalidates its handle. Calling any other
  // method with a handle after it has been destroyed is undefined
  // behavior.
  void DestroyAnimator(AnimatorHandle handle);

  // Advances handle's animator by one render frame of duration
  // delta_time seconds: blends locomotion/aim state toward input and
  // plays any actions triggered this frame. Returns the resulting Pose.
  Pose Update(AnimatorHandle handle, const LocomotionInput& input, const std::vector<Action>& actions,
              float delta_time);
};

}  // namespace augusta::animation

#endif  // AUGUSTA_ANIMATION_H_
