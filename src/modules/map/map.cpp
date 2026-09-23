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
                         physics::DescribeCollisionMeshError(error.collision_mesh_error));
    case MapErrorCode::kNoCollision:
      return "the scene has no collider, so the map would have no floor or walls";
    case MapErrorCode::kNoSpawnPoints:
      return "the scene has no spawn point, so there would be nowhere to put a player";
  }
  return "unknown map error";
}

namespace {

std::expected<assets::SceneData, MapError> ResolveScene(const assets::Pack& pack, std::string_view scene_path) {
  auto scene = pack.ResolveScene(scene_path);
  if (!scene) {
    return std::unexpected(MapError{.code = MapErrorCode::kSceneUnresolved,
                                    .node = {},
                                    .subject = std::string(scene_path),
                                    .resolve_error = scene.error()});
  }
  return *std::move(scene);
}

}  // namespace

std::expected<std::vector<physics::CollisionMesh>, MapError> LoadCollision(const assets::Pack& pack,
                                                                           std::string_view scene_path) {
  const auto scene = ResolveScene(pack, scene_path);
  if (!scene) {
    return std::unexpected(scene.error());
  }

  const std::vector<math::Mat4> world = assets::ComputeWorldTransforms(*scene);
  std::vector<physics::CollisionMesh> meshes;
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
    physics::CollisionMesh mesh{.points = {}, .indices = std::move(collision->indices)};
    mesh.points.reserve(collision->points.size());
    for (const math::Vec3& point : collision->points) {
      mesh.points.push_back(math::TransformPoint(world[i], point));
    }
    if (const auto valid = physics::ValidateCollisionMesh(mesh); !valid) {
      return std::unexpected(MapError{.code = MapErrorCode::kInvalidCollider,
                                      .node = node.name,
                                      .subject = *node.collider_path,
                                      .collision_mesh_error = valid.error()});
    }
    meshes.push_back(std::move(mesh));
  }
  if (meshes.empty()) {
    return std::unexpected(MapError{.code = MapErrorCode::kNoCollision, .node = {}, .subject = {}});
  }
  return meshes;
}

std::expected<std::vector<math::Vec3>, MapError> LoadSpawnPoints(const assets::Pack& pack,
                                                                 std::string_view scene_path) {
  const auto scene = ResolveScene(pack, scene_path);
  if (!scene) {
    return std::unexpected(scene.error());
  }

  const std::vector<math::Mat4> world = assets::ComputeWorldTransforms(*scene);
  std::vector<math::Vec3> spawn_points;
  for (std::size_t i = 0; i < scene->nodes.size(); ++i) {
    if (scene->nodes[i].is_spawn_point) {
      spawn_points.push_back(math::TranslationOf(world[i]));
    }
  }
  if (spawn_points.empty()) {
    return std::unexpected(MapError{.code = MapErrorCode::kNoSpawnPoints, .node = {}, .subject = {}});
  }
  return spawn_points;
}

}  // namespace augusta::map
