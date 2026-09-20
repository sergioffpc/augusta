#include "augusta/map.h"

#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "augusta/math.h"

namespace augusta::map {

std::string DescribeMapError(const MapError& error) {
  switch (error.code) {
    case MapErrorCode::kSceneUnresolved:
      return std::format("scene {} {}", error.subject, assets::DescribeResolveError(error.resolve_error, "scene"));
    case MapErrorCode::kColliderUnresolved:
      return std::format("collider {} of node {} {}", error.subject, error.node,
                         assets::DescribeResolveError(error.resolve_error, "collision geometry"));
    case MapErrorCode::kInvalidCollider:
      return std::format("collider {} of node {}: {}", error.subject, error.node,
                         physics::DescribeStaticMeshError(error.static_mesh_error));
    case MapErrorCode::kNoCollision:
      return "the scene has no collider, so the map would have no floor or walls";
  }
  return "unknown map error";
}

std::expected<std::vector<physics::StaticMesh>, MapError> LoadCollision(const assets::Pack& pack,
                                                                        std::string_view scene_path) {
  const auto scene = pack.ResolveScene(scene_path);
  if (!scene) {
    return std::unexpected(MapError{
        .code = MapErrorCode::kSceneUnresolved, .subject = std::string(scene_path), .resolve_error = scene.error()});
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
      return std::unexpected(MapError{.code = MapErrorCode::kColliderUnresolved,
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
      return std::unexpected(MapError{.code = MapErrorCode::kInvalidCollider,
                                      .node = node.name,
                                      .subject = *node.collider_path,
                                      .static_mesh_error = valid.error()});
    }
    meshes.push_back(std::move(mesh));
  }
  if (meshes.empty()) {
    return std::unexpected(MapError{.code = MapErrorCode::kNoCollision});
  }
  return meshes;
}

}  // namespace augusta::map
