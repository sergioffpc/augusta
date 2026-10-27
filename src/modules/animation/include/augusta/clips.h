#ifndef AUGUSTA_CLIPS_H_
#define AUGUSTA_CLIPS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/physics.h"

/// \file
/// A Character's Clips (GLOSSARY.md, ADR-0066), loaded from its verified pack
/// with ozz-animation's runtime alone: no USD reaches a process. LoadClips reads
/// its ozz-animation Skeleton, each Clip it has by role, its stance transition
/// durations and its Weapon socket, and refuses a Character whose Clips are
/// missing or do not load, naming the Clip, so a server never admits a player it
/// cannot pose.
///
/// The ozz-animation types are held as they are. ozz's SIMD values must never be
/// held in a standard template instantiated on one (std::array<SimdFloat4, N>):
/// clang-cl aligns those to 8 bytes only, the #pragma pack(8) MSVC's standard
/// library declares them under, and an aligned SSE load of an element crashes.
namespace augusta::animation {

/// A Clip, ready for ozz-animation's sampling: its Animation, a track for each
/// Joint of its Character's ozz-animation Skeleton, and what the cooker
/// measured of it (assets::ClipData).
struct Clip {
  ozz::animation::Animation animation;
  /// How far its Stride advances one cycle by, in meters: 0 unless locomotion.
  float stride_length = 0.0F;
  /// One left and one right for locomotion, in time order; none for an idle.
  std::vector<assets::FootContact> foot_contacts;
};

/// A stance's Clips (ADR-0066): its blend space over the body's velocity, its
/// Aim offset and its ADS grip. An optional Clip a Character lacks is nullopt.
struct StanceClips {
  Clip idle;
  Clip forward;
  Clip backward;
  /// Required standing and crouching; optional prone.
  std::optional<Clip> left;
  std::optional<Clip> right;
  /// Optional in every stance.
  std::optional<Clip> forward_left;
  std::optional<Clip> forward_right;
  std::optional<Clip> backward_left;
  std::optional<Clip> backward_right;
  /// Standing's alone, and required there; nullopt in every other stance.
  std::optional<Clip> sprint;
  /// The Aim offset's poses, at the view's lowest, level and highest pitch.
  Clip aim_down;
  Clip aim_center;
  Clip aim_up;
  Clip ads;
};

/// A Character's Weapon socket (ADR-0066): where on its right hand the rifle's
/// Grip hangs, and the left hand that reaches the rifle's Fore-end by two-bone
/// IK, each Joint an index in the Character's assets::Skeleton.
struct WeaponSocket {
  /// The Joint of its right hand, which the socket moves with.
  std::uint32_t right_hand = 0;
  /// The socket in that Joint's space: where the rifle's Grip goes, pointing
  /// along its -Z as the Grip does.
  math::Mat4 frame{1.0F};
  /// The Joint of its left hand. Its parent, the elbow, and that Joint's
  /// parent, the shoulder, are the arm that bends to reach the Fore-end.
  std::uint32_t left_hand = 0;
};

/// The shoulder of the arm that holds the rifle on socket, a Joint of
/// skeleton: its right hand's grandparent, which LoadClips makes sure it has.
/// Where a Muzzle in a wall is found from (ADR-0044).
[[nodiscard]] std::uint32_t ShoulderOf(const assets::Skeleton& skeleton, const WeaponSocket& socket);

/// A Character's Clips, as the Hitbox pose uses them: its ozz-animation
/// Skeleton, each stance's Clips, its `air` Clip, how long each change of
/// stance takes and its Weapon socket. A Cosmetic layer's Clips are the
/// client's (ADR-0066) and not loaded here.
struct CharacterClips {
  /// The Character's Skeleton as ozz-animation keeps its Joints: depth first,
  /// which may differ from assets::Skeleton's order (see joints).
  ozz::animation::Skeleton skeleton;
  /// For each Joint of skeleton, in its order, the index of the same Joint in
  /// the Character's assets::Skeleton.
  std::vector<std::uint32_t> joints;
  /// Each stance's, indexed by physics::Stance.
  std::array<StanceClips, physics::kStanceCount> stances;
  Clip air;
  assets::StanceTransitionsData transitions;
  /// Where the Hitbox pose hangs the rifle and reaches it with the left hand.
  WeaponSocket weapon_socket;

  /// The Clips of stance.
  [[nodiscard]] const StanceClips& Of(physics::Stance stance) const;
};

/// The part of a Character's Clips that did not load.
enum class ClipPart {
  /// Its ozz-animation Skeleton.
  kSkeleton,
  /// One of its Clips, ClipLoadError::role.
  kClip,
  /// Its stance transition durations.
  kTransitions,
  /// Its Weapon socket.
  kWeaponSocket,
};

/// Why a part of a Character's Clips did not load.
enum class ClipLoadCause {
  /// The pack could not give it: ClipLoadError::resolve says why.
  kUnresolved,
  /// It is not an ozz-animation archive of its kind.
  kNotAnArchive,
  /// The ozz-animation Skeleton has no Joint.
  kNoJoint,
  /// The ozz-animation Skeleton has a Joint, ClipLoadError::joint, the
  /// Character's Skeleton lacks.
  kUnknownJoint,
  /// The ozz-animation Skeleton has ClipLoadError::count Joints, the
  /// Character's Skeleton ClipLoadError::expected.
  kJointCountMismatch,
  /// The Clip has ClipLoadError::count tracks, its ozz-animation Skeleton
  /// ClipLoadError::expected Joints.
  kTrackCountMismatch,
  /// The Weapon socket names a right hand, ClipLoadError::joint, the
  /// Character's Skeleton lacks.
  kUnknownRightHand,
  /// The Weapon socket names a left hand, ClipLoadError::joint, the
  /// Character's Skeleton lacks.
  kUnknownLeftHand,
  /// The Weapon socket's left hand, ClipLoadError::joint, has no arm: no parent
  /// or no grandparent.
  kLeftHandHasNoArm,
  /// The Weapon socket's right hand, ClipLoadError::joint, has no arm: no
  /// parent or no grandparent.
  kRightHandHasNoArm,
};

/// Why LoadClips refused a Character's Clips: the part and the cause, and what
/// the cause names.
struct ClipLoadError {
  ClipPart part = ClipPart::kClip;
  ClipLoadCause cause = ClipLoadCause::kUnresolved;
  /// The Clip's role, e.g. "standing/forward", for a kClip; empty otherwise.
  std::string role;
  /// Why the pack could not give the part, for a kUnresolved.
  assets::ResolveError resolve = assets::ResolveError::kNotFound;
  /// The Joint named, for a cause that names one.
  std::string joint;
  /// What was found and what was expected, for a count that does not match.
  std::size_t count = 0;
  std::size_t expected = 0;
};

/// A message for error fit to log or print, e.g. "clip standing/forward: not
/// found".
[[nodiscard]] std::string DescribeClipLoadError(const ClipLoadError& error);

/// Loads the Clips of the character named character (its name in the
/// scenario's manifest) out of pack, whose assets::Skeleton is skeleton: its
/// ozz-animation Skeleton, which must hold skeleton's Joints, every Clip the
/// Hitbox pose uses (each required one, and each optional one it has), each a
/// track for each Joint, its stance transitions, and its Weapon socket, whose
/// Joints must be skeleton's, each hand's with a parent and a grandparent.
/// Refuses the first part missing or not loading.
[[nodiscard]] std::expected<CharacterClips, ClipLoadError> LoadClips(const assets::Pack& pack,
                                                                     std::string_view character,
                                                                     const assets::Skeleton& skeleton);

}  // namespace augusta::animation

#endif  // AUGUSTA_CLIPS_H_
