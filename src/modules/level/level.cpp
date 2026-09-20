#include "augusta/level.h"

#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "augusta/math.h"

namespace augusta::level {

std::string DescribeLevelError(const LevelError& error) {
  switch (error.code) {
    case LevelErrorCode::kSceneUnresolved:
      return std::format("scene {} {}", error.subject, assets::DescribeResolveError(error.resolve_error, "scene"));
    case LevelErrorCode::kColliderUnresolved:
      return std::format("collider {} of node {} {}", error.subject, error.node,
                         assets::DescribeResolveError(error.resolve_error, "collision geometry"));
    case LevelErrorCode::kInvalidCollider:
      return std::format("collider {} of node {}: {}", error.subject, error.node,
                         physics::DescribeStaticMeshError(error.static_mesh_error));
    case LevelErrorCode::kNoCollision:
      return "the scene has no collider, so the level would have no floor or walls";
  }
  return "unknown level error";
}

std::expected<std::vector<physics::StaticMesh>, LevelError> LoadCollision(const assets::Pack& pack,
                                                                          std::string_view scene_path) {
  const auto scene = pack.ResolveScene(scene_path);
  if (!scene) {
    return std::unexpected(LevelError{
        .code = LevelErrorCode::kSceneUnresolved, .subject = std::string(scene_path), .resolve_error = scene.error()});
  }

  const std::vector<math::Mat4> world = assets::ComputeWorldTransforms(*scene);
  std::vector<physics::StaticMesh> meshes;
  for (std::size_t i = 0; i < scene->nodes.size(); ++i) {
    const assets::SceneNode& node = scene->nodes[i];
    if (!node.collider_path) {
      continue;
    }
    auto collision = pack.ResolveCollision(*node.collider_path);
    if (!collision) {
      return std::unexpected(LevelError{.code = LevelErrorCode::kColliderUnresolved,
                                        .node = node.name,
                                        .subject = *node.collider_path,
                                        .resolve_error = collision.error()});
    }
    physics::StaticMesh mesh{.indices = std::move(collision->indices)};
    mesh.points.reserve(collision->points.size());
    for (const math::Vec3& point : collision->points) {
      mesh.points.push_back(math::TransformPoint(world[i], point));
    }
    if (const auto valid = physics::ValidateStaticMesh(mesh); !valid) {
      return std::unexpected(LevelError{.code = LevelErrorCode::kInvalidCollider,
                                        .node = node.name,
                                        .subject = *node.collider_path,
                                        .static_mesh_error = valid.error()});
    }
    meshes.push_back(std::move(mesh));
  }
  if (meshes.empty()) {
    return std::unexpected(LevelError{.code = LevelErrorCode::kNoCollision});
  }
  return meshes;
}

}  // namespace augusta::level
