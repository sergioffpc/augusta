#include "augusta/hitbox.h"

#include <cassert>
#include <cstddef>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::hitbox {

// The stance's lowering is LowerToStance's, as a scale of the vertical alone.
math::Mat4 RootOf(const Pose& pose) {
  const math::Vec3 lowered = physics::LowerToStance(math::Vec3(1.0F), pose.stance);
  return math::ToMat4(pose.position, command::ViewRotation(pose.yaw, 0.0F), lowered);
}

std::vector<PlacedHitbox> Place(std::span<const CharacterHitbox> hitboxes, std::span<const math::Mat4> joints) {
  std::vector<PlacedHitbox> placed;
  placed.reserve(hitboxes.size());
  for (const CharacterHitbox& hitbox : hitboxes) {
    // Bound at load to a Joint of the Skeleton the pose is of (assets::BindHitboxes).
    assert(hitbox.joint < joints.size());
    const math::Mat4& joint = joints[hitbox.joint];
    PlacedHitbox& one = placed.emplace_back(PlacedHitbox{.part = hitbox.part, .triangles = {}});
    one.triangles.reserve(hitbox.triangles.size());
    for (const ballistics::Triangle& triangle : hitbox.triangles) {
      one.triangles.push_back(ballistics::Triangle{.a = math::TransformPoint(joint, triangle.a),
                                                   .b = math::TransformPoint(joint, triangle.b),
                                                   .c = math::TransformPoint(joint, triangle.c)});
    }
  }
  return placed;
}

std::vector<PlacedHitbox> Place(std::span<const CharacterHitbox> hitboxes, const Pose& pose) {
  const math::Mat4 root = RootOf(pose);
  return Place(hitboxes, std::span<const math::Mat4>(&root, 1));
}

body_part::BodyPart FromPack(assets::BodyPart part) {
  switch (part) {
    case assets::BodyPart::kHead:
      return body_part::BodyPart::kHead;
    case assets::BodyPart::kTorso:
      return body_part::BodyPart::kTorso;
    case assets::BodyPart::kLimb:
      return body_part::BodyPart::kLimb;
  }
  std::unreachable();
}

std::expected<CharacterHitbox, physics::CollisionMeshError> FromPack(const assets::HitboxData& hitbox) {
  const assets::MeshData& mesh = hitbox.mesh;
  if (const auto valid = physics::ValidateCollisionMesh({.points = mesh.points, .indices = mesh.indices}); !valid) {
    return std::unexpected(valid.error());
  }
  CharacterHitbox result{.part = FromPack(hitbox.part), .triangles = {}};
  result.triangles.reserve(mesh.indices.size() / 3);
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    result.triangles.push_back(ballistics::Triangle{.a = mesh.points[mesh.indices[i]],
                                                    .b = mesh.points[mesh.indices[i + 1]],
                                                    .c = mesh.points[mesh.indices[i + 2]]});
  }
  return result;
}

std::expected<CharacterHitbox, physics::CollisionMeshError> FromPack(const assets::JointHitbox& hitbox) {
  return FromPack(hitbox.hitbox).transform([&hitbox](CharacterHitbox converted) {
    converted.joint = hitbox.joint;
    return converted;
  });
}

}  // namespace augusta::hitbox
