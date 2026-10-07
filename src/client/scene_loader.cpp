#include "scene_loader.h"

#include <cstddef>
#include <expected>
#include <format>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/renderer.h"

namespace augusta::client {

namespace {

renderer::SceneMesh ToWorldSpace(const assets::MeshData& mesh, const math::Mat4& world) {
  renderer::SceneMesh result{.indices = mesh.indices};
  result.positions.reserve(mesh.points.size());
  for (const math::Vec3& point : mesh.points) {
    result.positions.push_back(math::TransformPoint(world, point));
  }
  return result;
}

// The node's base color, or nullopt if it has none. The error names the node
// whose value is malformed.
std::expected<std::optional<math::Vec3>, SceneError> ParseBaseColor(const assets::SceneNode& node) {
  for (const auto& [key, value] : node.properties) {
    if (key != kBaseColorProperty) {
      continue;
    }
    std::istringstream stream(value);
    math::Vec3 color;
    if (!(stream >> color.x >> color.y >> color.z) || !(stream >> std::ws).eof()) {
      return std::unexpected(
          SceneError{.code = SceneErrorCode::kMalformedBaseColor, .node = node.name, .subject = value});
    }
    return color;
  }
  return std::nullopt;
}

// The camera of a character standing at the spawn point: its feet at the
// point's origin, drawn unrotated like every character (renderer.h), so its eye
// is added as authored.
renderer::Camera CameraAtSpawnPoint(const math::Mat4& spawn_world, const math::Vec3& eye) {
  renderer::Camera camera;
  camera.position = math::TranslationOf(spawn_world) + eye;
  camera.rotation = math::RotationOf(spawn_world);
  return camera;
}

}  // namespace

std::string DescribeSceneError(const SceneError& error) {
  switch (error.code) {
    case SceneErrorCode::kSceneUnresolved:
      return std::format("scene {} {}", error.subject, assets::DescribeResolveError(error.resolve_error, "scene"));
    case SceneErrorCode::kMeshUnresolved:
      return std::format("mesh {} of node {} {}", error.subject, error.node,
                         assets::DescribeResolveError(error.resolve_error, "mesh"));
    case SceneErrorCode::kMalformedBaseColor:
      return std::format("node {} has a malformed {} \"{}\"", error.node, kBaseColorProperty, error.subject);
  }
  return "unknown scene error";
}

std::expected<renderer::Scene, SceneError> BuildRenderScene(const assets::SceneData& scene,
                                                            const MeshResolver& resolve_mesh, const math::Vec3& eye) {
  const std::vector<math::Mat4> world = assets::ComputeWorldTransforms(scene);

  renderer::Scene result;
  bool has_spawn_point = false;
  for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
    const assets::SceneNode& node = scene.nodes[i];
    if (node.is_spawn_point && !has_spawn_point) {
      result.camera = CameraAtSpawnPoint(world[i], eye);
      has_spawn_point = true;
    }
    if (!node.mesh_path) {
      continue;
    }
    const auto mesh = resolve_mesh(*node.mesh_path);
    if (!mesh) {
      return std::unexpected(SceneError{.code = SceneErrorCode::kMeshUnresolved,
                                        .node = node.name,
                                        .subject = *node.mesh_path,
                                        .resolve_error = mesh.error()});
    }
    const auto color = ParseBaseColor(node);
    if (!color) {
      return std::unexpected(color.error());
    }
    result.meshes.push_back(ToWorldSpace(*mesh, world[i]));
    if (*color) {
      result.meshes.back().color = **color;
    }
  }
  return result;
}

std::expected<renderer::Scene, SceneError> LoadRenderScene(const assets::Pack& pack, const math::Vec3& eye,
                                                           std::string_view scene_path) {
  const auto scene = pack.ResolveScene(scene_path);
  if (!scene) {
    return std::unexpected(SceneError{
        .code = SceneErrorCode::kSceneUnresolved, .subject = std::string(scene_path), .resolve_error = scene.error()});
  }
  return BuildRenderScene(*scene, [&pack](std::string_view path) { return pack.ResolveMesh(path); }, eye);
}

}  // namespace augusta::client
