#include "augusta/local_view.h"

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"

namespace augusta::presentation {

namespace {

// Below this fraction of the way from previous to latest, previous's stance is
// shown; at or beyond it, latest's - the same rule as a remote player's
// (interpolation.cpp).
constexpr float kMidpointFraction = 0.5F;

}  // namespace

prediction::State BlendTicks(const prediction::State& previous, const prediction::State& latest, float fraction) {
  prediction::State blended = latest;
  blended.local_body.position = math::Lerp(previous.local_body.position, latest.local_body.position, fraction);
  blended.local_body.velocity = math::Lerp(previous.local_body.velocity, latest.local_body.velocity, fraction);
  blended.local_body.stance = fraction < kMidpointFraction ? previous.local_body.stance : latest.local_body.stance;
  blended.total_correction = math::Lerp(previous.total_correction, latest.total_correction, fraction);
  return blended;
}

math::Vec3 EyeAt(const math::Vec3& standing_eye, physics::Stance stance) {
  const float scale = physics::StanceHeight(stance) / physics::StanceHeight(physics::Stance::kStanding);
  return {standing_eye.x, standing_eye.y * scale, standing_eye.z};
}

Camera LocalCamera(const math::Vec3& feet, physics::Stance stance, const math::Vec3& standing_eye,
                   const math::Quat& view) {
  return Camera{.position = feet + EyeAt(standing_eye, stance), .rotation = view};
}

}  // namespace augusta::presentation
