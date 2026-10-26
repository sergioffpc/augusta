#ifndef AUGUSTA_POSE_H_
#define AUGUSTA_POSE_H_

#include <memory>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/weapon.h"

/// \file
/// A player's Hitbox pose evaluated from its Character's Clips (ADR-0066), with
/// ozz-animation's sampling and blending: its stance's blend space at its
/// velocity in its own space (augusta/locomotion.h), each Clip sampled where the
/// Stride puts it between the Clip's two Foot contacts, so two blended Clips
/// always land the same foot together. While a change of stance is under way,
/// the stance it left is posed the same way and blended into the new one by how
/// far the change has gone (physics::BodyState::stance_progress). Off the
/// ground, its air Clip by its vertical velocity takes the place of both. On
/// top of each stance posed, before the two blend, the stance's Aim offset bends
/// the body by the view's pitch and its ADS grip raises its arms, each an
/// additive layer of its difference from the level aim. The root is where the
/// body's position and yaw put it (hitbox::RootOf); how low the body is, is its
/// stances' Clips'. Then HoldRifle hangs the rifle from the weapon socket on its
/// right hand, wherever the pose put that hand, and bends its left arm by
/// ozz-animation's two-bone IK so the left hand reaches the rifle's Fore-end.
///
/// A pure function of replicated state: the Poser holds ozz-animation's
/// working buffers alone, so posing the same state twice gives the same pose,
/// whatever was posed between. SimulationWorld's Posing phase poses every player
/// with one. A client poses its local player's predicted body with one on a
/// frame it fires, to show its muzzle flash at the Muzzle it holds (ADR-0064),
/// and has what it would pose a remote player from, its interpolated state
/// (presentation::PoseInputOf, ADR-0061), and the two agree, but it draws no
/// posed player yet: drawing one comes with skinning.
namespace augusta::animation {

/// How fast a body rises at the start of its air Clip, and falls at its end, in
/// meters a second: the Clip is sampled by the body's vertical velocity between
/// them, faster held at either end.
inline constexpr float kAirVerticalSpeed = 5.0F;

/// What a player's Hitbox pose follows from: all of it in its Authoritative
/// State.
struct PoseInput {
  /// Its body, as Movement left it this tick: its position, velocity, stance,
  /// Stride, change of stance and whether it is on the ground.
  physics::BodyState body{};
  /// Where it faces, in radians, as a Command's yaw (command::Command).
  float yaw = 0.0F;
  /// Where its view looks up (+) or down (-), in radians, as a Command's pitch:
  /// its stance's Aim offset bends the body by it, fully at
  /// command::kMaxLookPitch either way.
  float pitch = 0.0F;
  /// Whether it aims down sights: its stance's ADS grip raises its arms.
  bool ads = false;
};

/// Evaluates Hitbox poses from Clips. Move-only: it owns ozz-animation's
/// sampling contexts, which do not move.
class Poser {
 public:
  Poser();
  ~Poser();
  Poser(Poser&&) noexcept;
  Poser& operator=(Poser&&) noexcept;
  Poser(const Poser&) = delete;
  Poser& operator=(const Poser&) = delete;

  /// Poses skeleton, whose Clips are clips, at input into posed: each Joint
  /// relative to its parent as clips put it, and the world transform that
  /// composes from where the input puts the root. posed is overwritten, so a
  /// caller posing every tick keeps its storage. clips must be skeleton's
  /// (LoadClips checked them against it).
  void Pose(const CharacterClips& clips, const assets::Skeleton& skeleton, const PoseInput& input, SkeletonPose& posed);

 private:
  struct Buffers;
  std::unique_ptr<Buffers> buffers_;
};

/// Holds the rifle whose markers are rifle in posed, a pose of skeleton as
/// Poser::Pose left it (ADR-0066): the rifle's Grip on socket,
/// where the pose put the Joint it moves with, and the left hand on the rifle's
/// Fore-end, its elbow and shoulder turned by ozz-animation's two-bone IK, each
/// Joint below them composed again. The elbow bends the way the pose bent it,
/// out of the line from the shoulder to the hand (down, for an arm the pose
/// holds straight); a Fore-end out of the arm's reach leaves it straight toward
/// it. Returns where the Grip is held, the hand weapon::PlaceMuzzle places the
/// rifle's Muzzle from. socket must be skeleton's (LoadClips checked it).
math::Mat4 HoldRifle(const assets::Skeleton& skeleton, const WeaponSocket& socket, const weapon::Markers& rifle,
                     SkeletonPose& posed);

}  // namespace augusta::animation

#endif  // AUGUSTA_POSE_H_
