#include "augusta/hitbox.h"

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

namespace {

math::Vec3 PlacePoint(const math::Vec3& point, const Pose& pose) {
  return pose.position + (command::ViewRotation(pose.yaw, 0.0F) * physics::LowerToStance(point, pose.stance));
}

}  // namespace

std::vector<PlacedHitbox> Place(std::span<const CharacterHitbox> hitboxes, const Pose& pose) {
  std::vector<PlacedHitbox> placed;
  placed.reserve(hitboxes.size());
  for (const CharacterHitbox& hitbox : hitboxes) {
    PlacedHitbox& one = placed.emplace_back(PlacedHitbox{.part = hitbox.part, .triangles = {}});
    one.triangles.reserve(hitbox.triangles.size());
    for (const ballistics::Triangle& triangle : hitbox.triangles) {
      one.triangles.push_back(ballistics::Triangle{
          .a = PlacePoint(triangle.a, pose), .b = PlacePoint(triangle.b, pose), .c = PlacePoint(triangle.c, pose)});
    }
  }
  return placed;
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

}  // namespace augusta::hitbox
