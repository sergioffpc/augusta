#include "scene_loader.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "augusta/math.h"

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
    case SceneErrorCode::kUnknownCharacter:
      return std::format("character index {} is not in the pack's character list", error.subject);
    case SceneErrorCode::kCharacterMeshUnresolved:
      return std::format("visual mesh {} of character {} {}", error.subject, error.node,
                         assets::DescribeResolveError(error.resolve_error, "mesh"));
    case SceneErrorCode::kCharacterEyeUnresolved:
      return std::format("eye {} of character {} {}", error.subject, error.node,
                         assets::DescribeResolveError(error.resolve_error, "eye"));
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

std::vector<std::uint8_t> CharactersToLoad(std::span<const std::uint8_t> others, const std::set<std::uint8_t>& loaded) {
  std::vector<std::uint8_t> to_load;
  for (const std::uint8_t character : others) {
    if (!loaded.contains(character) && std::ranges::find(to_load, character) == to_load.end()) {
      to_load.push_back(character);
    }
  }
  return to_load;
}

std::string CharacterMeshPath(std::string_view character) { return std::format("{}/Character/Visual", character); }

std::expected<renderer::SceneMesh, SceneError> LoadCharacterMesh(std::span<const std::string> characters,
                                                                 std::uint8_t character_index,
                                                                 const MeshResolver& resolve_mesh) {
  if (character_index == 0 || character_index > characters.size()) {
    return std::unexpected(
        SceneError{.code = SceneErrorCode::kUnknownCharacter, .subject = std::to_string(character_index)});
  }
  const std::string& character = characters[character_index - 1];
  const std::string path = CharacterMeshPath(character);
  const auto mesh = resolve_mesh(path);
  if (!mesh) {
    return std::unexpected(SceneError{.code = SceneErrorCode::kCharacterMeshUnresolved,
                                      .node = character,
                                      .subject = path,
                                      .resolve_error = mesh.error()});
  }
  // color is unused here - BuildRemoteVertices (renderer.cpp) replaces it
  // with each RemotePlayer's own color; left at SceneMesh's own default.
  return renderer::SceneMesh{.positions = mesh->points, .indices = mesh->indices};
}

std::string CharacterEyePath(std::string_view character) { return std::format("{}/Character/Eye", character); }

std::expected<math::Vec3, SceneError> LoadCharacterEye(std::string_view character, const EyeResolver& resolve_eye) {
  const std::string path = CharacterEyePath(character);
  const auto eye = resolve_eye(path);
  if (!eye) {
    return std::unexpected(SceneError{.code = SceneErrorCode::kCharacterEyeUnresolved,
                                      .node = std::string(character),
                                      .subject = path,
                                      .resolve_error = eye.error()});
  }
  return eye->position;
}

}  // namespace augusta::client
