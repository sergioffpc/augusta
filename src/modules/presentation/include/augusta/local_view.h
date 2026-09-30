#ifndef AUGUSTA_LOCAL_VIEW_H_
#define AUGUSTA_LOCAL_VIEW_H_

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"

// The local player's own view (ADR-0024's Interpolation and Camera phases):
// where its predicted body is shown between two ticks, and where its camera
// sits and looks. The prediction thread ticks at the server's tick rate and
// the Main/Render thread draws at its own; showing only the newest Prediction
// State would step the view whenever the two differ. Instead each frame shows
// the body the fraction of the tick elapsed at render time of the way from the
// previous tick's state to the newest, and turns the camera by the mouse-look
// accumulated up to the frame rather than up to the tick.
//
// Pure - no clock, no ECS - so it is tested on its own; PresentationWorld feeds
// it each frame (see presentation.cpp).
namespace augusta::presentation {

/// The local player's two newest Prediction States and how far between them a
/// render frame is - World::RunFrame's input.
struct PredictedTicks {
  prediction::State previous{};
  prediction::State latest{};
  /// How far through the tick from previous to latest the frame is: 0 shows
  /// previous, 1 latest.
  float fraction = 1.0F;
};

// The local player's view camera for one frame - Phase::kCamera's output.
// A plain position/rotation pair, not augusta::renderer::Camera itself: this
// module stays decoupled from augusta_renderer the same way
// presentation::RemotePlayer does (see renderer::RemotePlayer's doc comment)
// - ClientRuntime's ToRenderer maps both into their renderer-side
// equivalents.
struct Camera {
  math::Vec3 position{};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
};

/// The Prediction State fraction of the way from previous to latest: position,
/// velocity and total_correction linearly interpolated, the stance (discrete)
/// of whichever of the two is nearer, and the rest as of latest. Blending
/// total_correction with the position keeps a reconciliation jump inside the
/// tick hidden by Correction as it is being blended in.
[[nodiscard]] prediction::State BlendTicks(const prediction::State& previous, const prediction::State& latest,
                                           float fraction);

/// The view camera of a body shown at feet, in stance, of a character whose eye
/// standing is standing_eye (ADR-0040, in its root space: feet at the origin),
/// looking where view says: at the eye lowered for that stance
/// (physics::LowerToStance) - added as authored, since the character is drawn
/// unrotated - and turned by view.
[[nodiscard]] Camera LocalCamera(const math::Vec3& feet, physics::Stance stance, const math::Vec3& standing_eye,
                                 const math::Quat& view);

}  // namespace augusta::presentation

#endif  // AUGUSTA_LOCAL_VIEW_H_
