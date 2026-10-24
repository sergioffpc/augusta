#ifndef AUGUSTA_HITBOX_H_
#define AUGUSTA_HITBOX_H_

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/math.h"
#include "augusta/physics.h"

/// \file
/// augusta::hitbox places a player's Hitboxes (GLOSSARY.md) by their Joints'
/// transforms, and puts a Character's root where its Hitbox pose has it: the
/// functions the server's Lag compensation tests a round against and a client's
/// Debug overlay draws with, so both put a Hitbox in the same place (ADR-0044,
/// ADR-0061). It also converts a pack's Hitbox data into a Character's Hitboxes,
/// the same for the server's content loading and the client's (ADR-0040 ships
/// them in both packs). Pure: evaluating a Skeleton's Joints is animation's;
/// which players are placed, at which moment, and what a round crossing one
/// means are SimulationWorld's (the Hitbox history, culling by a Character's
/// reach, judging), and drawing them is presentation's.
namespace augusta::hitbox {

/// Where a player's Hitbox pose puts its root: what places a Hitbox bound to
/// the root, and what its Joints' pose is composed onto.
struct Pose {
  /// Where its feet are.
  math::Vec3 position{};
  /// Where it faces, in radians, as a Command's yaw (command::Command).
  float yaw = 0.0F;
  /// The stance it was posed in: it does not move the root (RootOf).
  physics::Stance stance = physics::Stance::kStanding;
};

/// One Hitbox of a Character (ADR-0040, ADR-0061): the Joint of its Skeleton it
/// moves with, and its geometry in that Joint's space. Joint 0 is the root, so a
/// Hitbox bound to it is in the Character's root space: standing, its feet at
/// the origin, facing where a view of yaw 0 looks.
struct CharacterHitbox {
  /// What a round that crosses it hits.
  body_part::BodyPart part = body_part::BodyPart::kTorso;
  /// Its surface: a round is tested against each, from either side.
  std::vector<ballistics::Triangle> triangles;
  /// Its Joint's index in its Character's Skeleton (assets::Skeleton).
  std::uint32_t joint = 0;
};

/// One Hitbox placed at a Pose: its triangles in world space.
struct PlacedHitbox {
  body_part::BodyPart part = body_part::BodyPart::kTorso;
  std::vector<ballistics::Triangle> triangles;
};

/// Where pose puts a Character's root, and with it the root Joint of its
/// Skeleton (ADR-0061): turned about the vertical by the yaw as the view is
/// (command::ViewRotation), then moved to the position. The stance does not
/// scale it: how low a body is, is its stance's Clips' (ADR-0066).
[[nodiscard]] math::Mat4 RootOf(const Pose& pose);

/// hitboxes placed by joints, a Hitbox pose's world transform of each Joint
/// (animation::PoseAtRest), one per Hitbox and in their order: each Hitbox's
/// triangles transformed by its Joint's. Each Hitbox's Joint must be one joints
/// has: bindings are checked when a Character loads, so one past joints is a
/// broken invariant, asserted (ADR-0033), never thrown on the tick.
[[nodiscard]] std::vector<PlacedHitbox> Place(std::span<const CharacterHitbox> hitboxes,
                                              std::span<const math::Mat4> joints);

/// hitboxes, each bound to the root (joint 0) and so in the Character's root
/// space, placed at pose, by RootOf(pose): how a client's Debug overlay places
/// a Character's Hitboxes once its Rest pose has placed them in that space.
[[nodiscard]] std::vector<PlacedHitbox> Place(std::span<const CharacterHitbox> hitboxes, const Pose& pose);

/// part, in the pack blob's numbering, as the Body part every module names.
[[nodiscard]] body_part::BodyPart FromPack(assets::BodyPart part);

/// hitbox, as a pack holds it, as the triangles its mesh indexes, bound to the
/// root. Fails, saying why, if the mesh is not a whole, in-range triangle list:
/// a pack's mesh blob is not checked for that when it is decoded.
[[nodiscard]] std::expected<CharacterHitbox, physics::CollisionMeshError> FromPack(const assets::HitboxData& hitbox);

/// hitbox as FromPack(hitbox.hitbox) converts it, bound to the Joint
/// assets::BindHitboxes resolved for it.
[[nodiscard]] std::expected<CharacterHitbox, physics::CollisionMeshError> FromPack(const assets::JointHitbox& hitbox);

}  // namespace augusta::hitbox

#endif  // AUGUSTA_HITBOX_H_
