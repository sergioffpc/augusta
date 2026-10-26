#ifndef AUGUSTA_PHYSICS_MOVEMENT_RULES_H_
#define AUGUSTA_PHYSICS_MOVEMENT_RULES_H_

#include <cstdint>

#include "augusta/math.h"
#include "augusta/physics.h"

/// \file
/// The decision half of a World's movement, private to the module: a body's
/// capsule for its stance, its stamina and speed this tick, and how far its
/// Stride and change of stance advance. Pure - no PhysX; World, the mechanism
/// half, applies what it decides (ADR-0002).
namespace augusta::physics {

// Spike placeholders (ADR-0002 doesn't pin these down, and unlike
// StaminaConfig these aren't yet threaded through as data): move to
// external configuration once gameplay tuning needs it, same posture as
// StaminaConfig's own header comment.
inline constexpr float kCapsuleRadius = 0.3F;
inline constexpr float kStandingHeight = 1.5F;  // Capsule cylinder height, excludes hemispherical caps.

float HeightForStance(Stance stance);

// Decision half of Step's stamina rule (US-05): given this tick's sprint
// request, whether the body is standing and moving, and its stamina and
// exhaustion, resolves whether sprint is actually honored and the resulting
// stamina and exhaustion. Pure - no PxController calls - so it stays
// unit-testable independent of PhysX; Step (mechanism half) just applies the
// result.
struct StaminaResult {
  bool sprinting = false;
  float stamina = 1.0F;
  bool exhausted = false;
};

// Sprint is honored only standing, moving and not exhausted, and only then
// drains; otherwise stamina regenerates. Running stamina out exhausts the
// body, and it stays exhausted, walking, until stamina is back above
// forced_walk_below, so a body held at the threshold does not flicker between
// sprinting and walking.
StaminaResult ResolveStamina(bool sprint_requested, bool standing_and_moving, float current_stamina,
                             bool currently_exhausted, const StaminaConfig& config, float delta_time);

// Decision half of Step's move speed: this tick's horizontal speed given
// stance and the effective (post-ResolveStamina) sprint state. Pure, same
// testability rationale as ResolveStamina above.
float ResolveSpeed(Stance stance, bool sprinting);

// stride advanced by moved's distance along the ground over stride_length,
// rounded to a whole step and round the cycle; held without a stride length.
std::uint8_t AdvanceStride(std::uint8_t stride, const math::Vec3& moved, float stride_length);

// progress, the progress of a change of stance that takes duration seconds,
// advanced by delta_time, rounded to a whole step and held at the change's end;
// a change without a duration is over at once.
std::uint8_t AdvanceStanceProgress(std::uint8_t progress, float delta_time, float duration);

}  // namespace augusta::physics

#endif  // AUGUSTA_PHYSICS_MOVEMENT_RULES_H_
