#ifndef AUGUSTA_POSE_H_
#define AUGUSTA_POSE_H_

#include <memory>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/physics.h"

/// \file
/// A player's Hitbox pose evaluated from its Character's Clips (ADR-0066), with
/// ozz-animation's sampling and blending: its stance's blend space at its
/// velocity in its own space (augusta/locomotion.h), each Clip sampled where the
/// Stride puts it between the Clip's two Foot contacts, so two blended Clips
/// always land the same foot together. While a change of stance is under way,
/// the stance it left is posed the same way and blended into the new one by how
/// far the change has gone (physics::BodyState::stance_progress). The root is
/// where the body's position and yaw put it (hitbox::RootOf); how low the body
/// is, is its stances' Clips'.
///
/// A pure function of replicated state: the Poser holds ozz-animation's
/// working buffers alone, so posing the same state twice gives the same pose,
/// whatever was posed between. SimulationWorld's Posing phase poses every player
/// with one; a client will pose players the same way (ADR-0061).
namespace augusta::animation {

/// What a player's Hitbox pose follows from: all of it in its Authoritative
/// State.
struct PoseInput {
  /// Its body, as Movement left it this tick: its position, velocity, stance,
  /// Stride and change of stance.
  physics::BodyState body{};
  /// Where it faces, in radians, as a Command's yaw (command::Command).
  float yaw = 0.0F;
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

}  // namespace augusta::animation

#endif  // AUGUSTA_POSE_H_
