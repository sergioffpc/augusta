#include "movement_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::physics {

namespace {

// Spike placeholders, for the reason movement_rules.h gives for its own.
constexpr float kCrouchingHeight = 0.7F;
constexpr float kProneHeight = 0.1F;
// The capsule's two hemispherical caps together add its diameter to its height.
constexpr float kCapsuleDiameter = 2.0F * kCapsuleRadius;
constexpr float kWalkSpeed = 3.0F;  // m/s, standing baseline.
constexpr float kSprintMultiplier = 1.6F;
constexpr float kCrouchSpeedMultiplier = 0.6F;
constexpr float kProneSpeedMultiplier = 0.3F;

float SpeedMultiplierForStance(Stance stance) {
  switch (stance) {
    case Stance::kStanding:
      return 1.0F;
    case Stance::kCrouching:
      return kCrouchSpeedMultiplier;
    case Stance::kProne:
      return kProneSpeedMultiplier;
  }
  return 1.0F;
}

}  // namespace

float HeightForStance(Stance stance) {
  switch (stance) {
    case Stance::kStanding:
      return kStandingHeight;
    case Stance::kCrouching:
      return kCrouchingHeight;
    case Stance::kProne:
      return kProneHeight;
  }
  return kStandingHeight;
}

StaminaResult ResolveStamina(bool sprint_requested, bool standing_and_moving, float current_stamina,
                             bool currently_exhausted, const StaminaConfig& config, float delta_time) {
  StaminaResult result;
  result.sprinting = sprint_requested && standing_and_moving && !currently_exhausted;
  if (result.sprinting) {
    result.stamina = std::max(0.0F, current_stamina - (config.deplete_per_second * delta_time));
  } else {
    result.stamina = std::min(1.0F, current_stamina + (config.regen_per_second * delta_time));
  }
  if (currently_exhausted) {
    result.exhausted = result.stamina <= config.forced_walk_below;
  } else {
    result.exhausted = result.stamina <= 0.0F;
  }
  return result;
}

float ResolveSpeed(Stance stance, bool sprinting) {
  float speed = kWalkSpeed * SpeedMultiplierForStance(stance);
  if (sprinting && stance == Stance::kStanding) {
    speed *= kSprintMultiplier;
  }
  return speed;
}

std::uint8_t AdvanceStride(std::uint8_t stride, const math::Vec3& moved, float stride_length) {
  if (!(stride_length > 0.0F)) {
    return stride;
  }
  const float ground = math::Length(math::Vec3(moved.x, 0.0F, moved.z));
  const std::int64_t steps = std::llround(ground / stride_length * static_cast<float>(kStrideSteps));
  return static_cast<std::uint8_t>((stride + steps) % kStrideSteps);
}

std::uint8_t AdvanceStanceProgress(std::uint8_t progress, float delta_time, float duration) {
  if (!(duration > 0.0F)) {
    return kStanceProgressSteps;
  }
  const std::int64_t steps = std::llround(delta_time / duration * static_cast<float>(kStanceProgressSteps));
  return static_cast<std::uint8_t>(std::min<std::int64_t>(progress + steps, kStanceProgressSteps));
}

float TransitionDuration(const assets::StanceTransitionsData& transitions, Stance from, Stance to) {
  if (from == to) {
    return 0.0F;
  }
  if (from != Stance::kProne && to != Stance::kProne) {
    return transitions.standing_crouching;
  }
  if (from != Stance::kCrouching && to != Stance::kCrouching) {
    return transitions.standing_prone;
  }
  return transitions.crouching_prone;
}

float StanceHeight(Stance stance) { return HeightForStance(stance) + kCapsuleDiameter; }

math::Vec3 LowerToStance(const math::Vec3& standing_point, Stance stance) {
  const float scale = StanceHeight(stance) / StanceHeight(Stance::kStanding);
  return {standing_point.x, standing_point.y * scale, standing_point.z};
}

Capsule CapsuleOf(const BodyState& state) {
  const math::Vec3 bottom = state.position + math::Vec3(0.0F, kCapsuleRadius, 0.0F);
  return {.bottom = bottom,
          .top = bottom + math::Vec3(0.0F, HeightForStance(state.stance), 0.0F),
          .radius = kCapsuleRadius};
}

}  // namespace augusta::physics
