#include "scene_loader.h"

#include <cstddef>
#include <format>
#include <vector>

#include "augusta/math.h"

namespace augusta::client {

namespace {

// How far above a spawn point's origin (its feet) the camera sits.
constexpr float kEyeHeight = 1.7F;

std::string_view DescribeResolveError(assets::ResolveError error) {
  switch (error) {
    case assets::ResolveError::kNotFound:
      return "not found";
    case assets::ResolveError::kTypeMismatch:
      return "is not a mesh";
    case assets::ResolveError::kCorruptBlob:
      return "is corrupt";
  }
  return "unknown error";
}

// World transform of every node. SceneData lists parents before children
// (ADR-0032), so one forward pass sees each parent's transform already done.
std::vector<math::Mat4> ComputeWorldTransforms(const assets::SceneData& scene) {
  std::vector<math::Mat4> world;
  world.reserve(scene.nodes.size());
  for (const assets::SceneNode& node : scene.nodes) {
    const math::Mat4 local = math::ToMat4(node.translation, node.rotation, node.scale);
    world.push_back(node.parent_index == assets::kSceneNodeNoParent ? local : world[node.parent_index] * local);
  }
  return world;
}

renderer::SceneMesh ToWorldSpace(const assets::MeshData& mesh, const math::Mat4& world) {
  renderer::SceneMesh result{.indices = mesh.indices};
  result.positions.reserve(mesh.points.size());
  for (const math::Vec3& point : mesh.points) {
    result.positions.push_back(math::TransformPoint(world, point));
  }
  return result;
}

renderer::Camera CameraAtSpawnPoint(const math::Mat4& spawn_world) {
  renderer::Camera camera;
  camera.position = math::TranslationOf(spawn_world) + math::Vec3(0.0F, kEyeHeight, 0.0F);
  camera.rotation = math::RotationOf(spawn_world);
  return camera;
}

}  // namespace

std::expected<renderer::Scene, std::string> BuildRenderScene(const assets::SceneData& scene,
                                                             const MeshResolver& resolve_mesh) {
  const std::vector<math::Mat4> world = ComputeWorldTransforms(scene);

  renderer::Scene result;
  bool has_spawn_point = false;
  for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
    const assets::SceneNode& node = scene.nodes[i];
    if (node.is_spawn_point && !has_spawn_point) {
      result.camera = CameraAtSpawnPoint(world[i]);
      has_spawn_point = true;
    }
    if (!node.mesh_path) {
      continue;
    }
    const auto mesh = resolve_mesh(*node.mesh_path);
    if (!mesh) {
      return std::unexpected(
          std::format("mesh {} of node {} {}", *node.mesh_path, node.name, DescribeResolveError(mesh.error())));
    }
    result.meshes.push_back(ToWorldSpace(*mesh, world[i]));
  }
  return result;
}

std::expected<renderer::Scene, std::string> LoadRenderScene(const assets::Pack& pack, std::string_view scene_path) {
  const auto scene = pack.ResolveScene(scene_path);
  if (!scene) {
    return std::unexpected(std::format("scene {} {}", scene_path, DescribeResolveError(scene.error())));
  }
  return BuildRenderScene(*scene, [&pack](std::string_view path) { return pack.ResolveMesh(path); });
}

}  // namespace augusta::client
