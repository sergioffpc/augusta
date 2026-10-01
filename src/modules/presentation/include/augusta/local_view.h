#ifndef AUGUSTA_LOCAL_VIEW_H_
#define AUGUSTA_LOCAL_VIEW_H_

#include <cstdint>
#include <optional>
#include <span>

#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/weapon.h"

// The local player's own view (ADR-0024's Interpolation and Camera phases):
// where its predicted body is shown between two ticks, where its camera sits
// and looks, how far it zooms, and what shows over it. The prediction thread ticks at the server's tick rate and
// the Main/Render thread draws at its own; showing only the newest Prediction
// State would step the view whenever the two differ. Instead each frame shows
// the body the fraction of the tick elapsed at render time of the way from the
// previous tick's state to the newest, and turns the camera by the mouse-look
// accumulated up to the frame rather than up to the tick. Once the local player
// is dead, a spectator (US-13), its view is a living player's instead: whom it
// watches (Spectator) and from where (WatchedCamera).
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

/// The vertical field of view from the hip, in radians: what AdsZoom zooms
/// from and back to.
inline constexpr float kHipFieldOfView = 0.9F;

// The local player's view camera for one frame - Phase::kCamera's output.
// Not augusta::renderer::Camera itself: this module stays decoupled from
// augusta_renderer the same way presentation::RemotePlayer does (see
// renderer::RemotePlayer's doc comment) - ClientRuntime's ToRenderer maps both
// into their renderer-side equivalents.
struct Camera {
  math::Vec3 position{};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  /// Vertical field of view, in radians (AdsZoom).
  float vertical_fov = kHipFieldOfView;
};

/// Where the local player aims as of a render frame: its view's yaw and pitch
/// (command::Command's), and whether it holds ADS - newer than its latest
/// Command's, which is only as new as the last tick.
struct Aim {
  float yaw = 0.0F;
  float pitch = 0.0F;
  bool ads = false;
};

/// The Prediction State fraction of the way from previous to latest: position,
/// velocity, total_correction and the rifle's Recoil offset linearly
/// interpolated, the stance (discrete) of whichever of the two is nearer, and
/// the rest as of latest. Blending
/// total_correction with the position keeps a reconciliation jump inside the
/// tick hidden by Correction as it is being blended in.
[[nodiscard]] prediction::State BlendTicks(const prediction::State& previous, const prediction::State& latest,
                                           float fraction);

/// The view camera of a body shown at feet, in stance, of a character whose eye
/// standing is standing_eye (ADR-0040, in its root space: feet at the origin),
/// looking where aim says turned by recoil: at the eye lowered for that stance
/// (physics::LowerToStance) - added as authored, since the character is drawn
/// unrotated - and turned as a Shot's view is (command::ViewRotation of aim's
/// yaw and pitch each plus recoil's), so the view shows where the next round
/// leaves. From the hip: AdsZoom sets its field of view.
[[nodiscard]] Camera LocalCamera(const math::Vec3& feet, physics::Stance stance, const math::Vec3& standing_eye,
                                 const Aim& aim, const weapon::RecoilOffset& recoil);

/// A spectator's camera watching body, a living player as shown this frame, of
/// a character whose eye standing is standing_eye: at that eye lowered for the
/// body's stance, as LocalCamera puts it, facing where the body faces and
/// looking level - remote pitch is not replicated - from the hip.
[[nodiscard]] Camera WatchedCamera(const RemoteBody& body, const math::Vec3& standing_eye);

/// The camera's field of view, zooming smoothly between the hip's and ADS's
/// (US-06): holding ADS moves it to the ADS field of view over
/// kTransitionSeconds, and letting go moves it back out over as long. Let go
/// part way, it turns back from where it is.
class AdsZoom {
 public:
  /// How long, in seconds, the zoom takes from the hip all the way in or out.
  static constexpr float kTransitionSeconds = 0.15F;

  /// The vertical field of view, in radians, delta_time seconds after the last
  /// call, with ads held or not, zooming to ads_field_of_view (the Parameters').
  [[nodiscard]] float Update(bool ads, float ads_field_of_view, float delta_time);

 private:
  // How far in the zoom is, 0 at the hip to 1 fully in ADS.
  float progress_ = 0.0F;
};

/// Whether the hit marker shows (CONTEXT.md's Hit confirmation): for
/// kShownSeconds after each Hit confirmation, and never otherwise - a hit is
/// never predicted (ADR-0044).
class HitMarker {
 public:
  /// How long, in seconds, the marker shows after a Hit confirmation.
  static constexpr float kShownSeconds = 0.25F;

  /// Whether the marker shows delta_time seconds after the last call, given
  /// how many Hit confirmations have arrived since.
  [[nodiscard]] bool Update(std::uint32_t confirmations, float delta_time);

 private:
  // How long the marker still shows for; 0 or less when it does not.
  float remaining_ = 0.0F;
};

/// Whom a dead local player, a spectator (US-13), watches: at first the first
/// living player in Session order; each press of fire, the next living one
/// after it, wrapping around; and once the watched player is no longer living,
/// the next living one after it. One instance per spectated match.
class Spectator {
 public:
  /// The player watched this frame, given players, the match's players in
  /// Session order, living, those of them still alive and shown, and whether
  /// fire is held (a press is it held after a frame it was not). nullopt with
  /// no one living.
  [[nodiscard]] std::optional<EntityId> Update(std::span<const EntityId> players, std::span<const EntityId> living,
                                               bool fire_held);

 private:
  std::optional<EntityId> watched_;
  bool fire_held_ = false;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_LOCAL_VIEW_H_
