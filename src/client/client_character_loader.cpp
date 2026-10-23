#include "augusta/client_character_loader.h"

#include <algorithm>
#include <expected>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/client_scene_loader.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/renderer.h"

#if AUGUSTA_DEBUG_TOOLS
#include "augusta/hitbox.h"
#endif

namespace augusta::client {

std::string DescribeCharacterError(const CharacterError& error) {
  switch (error.code) {
    case CharacterErrorCode::kUnknownCharacter:
      return std::format("character {} is not in the pack's character list", error.character);
    case CharacterErrorCode::kMeshUnresolved:
      return std::format("visual mesh {} of character {} {}", error.subject, error.character,
                         assets::DescribeResolveError(error.resolve_error, "mesh"));
    case CharacterErrorCode::kEyeUnresolved:
      return std::format("eye {} of character {} {}", error.subject, error.character,
                         assets::DescribeResolveError(error.resolve_error, "eye"));
    case CharacterErrorCode::kHitboxesUnresolved:
      return std::format("hitboxes of character {} {}", error.character,
                         assets::DescribeResolveError(error.resolve_error, "hitbox"));
    case CharacterErrorCode::kHitboxRejected:
      return std::format("{} hitbox of character {} rejected: {}", error.subject, error.character,
                         physics::DescribeCollisionMeshError(error.mesh_error));
  }
  return "unknown character error";
}

std::expected<renderer::SceneMesh, CharacterError> LoadCharacterMesh(std::span<const std::string> characters,
                                                                     std::string_view character,
                                                                     const MeshResolver& resolve_mesh) {
  if (std::ranges::find(characters, character) == characters.end()) {
    return std::unexpected(CharacterError{
        .code = CharacterErrorCode::kUnknownCharacter, .character = std::string(character), .subject = {}});
  }
  const std::string path = assets::CharacterMeshPath(character);
  const auto mesh = resolve_mesh(path);
  if (!mesh) {
    return std::unexpected(CharacterError{.code = CharacterErrorCode::kMeshUnresolved,
                                          .character = std::string(character),
                                          .subject = path,
                                          .resolve_error = mesh.error()});
  }
  // color is unused here - BuildRemoteVertices (renderer.cpp) replaces it
  // with each RemotePlayer's own color; left at SceneMesh's own default.
  return renderer::SceneMesh{.positions = mesh->points, .indices = mesh->indices};
}

std::expected<math::Vec3, CharacterError> LoadCharacterEye(std::string_view character, const EyeResolver& resolve_eye) {
  const std::string path = assets::CharacterEyePath(character);
  const auto eye = resolve_eye(path);
  if (!eye) {
    return std::unexpected(CharacterError{.code = CharacterErrorCode::kEyeUnresolved,
                                          .character = std::string(character),
                                          .subject = path,
                                          .resolve_error = eye.error()});
  }
  return eye->position;
}

#if AUGUSTA_DEBUG_TOOLS
std::expected<std::vector<hitbox::CharacterHitbox>, CharacterError> LoadCharacterHitboxes(
    std::string_view character, const HitboxesResolver& resolve_hitboxes) {
  const auto resolved = resolve_hitboxes(character);
  if (!resolved) {
    return std::unexpected(CharacterError{.code = CharacterErrorCode::kHitboxesUnresolved,
                                          .character = std::string(character),
                                          .subject = {},
                                          .resolve_error = resolved.error()});
  }
  std::vector<hitbox::CharacterHitbox> hitboxes;
  hitboxes.reserve(resolved->size());
  for (const assets::HitboxData& data : *resolved) {
    auto converted = hitbox::FromPack(data);
    if (!converted) {
      return std::unexpected(CharacterError{.code = CharacterErrorCode::kHitboxRejected,
                                            .character = std::string(character),
                                            .subject = std::string(assets::BodyPartName(data.part)),
                                            .resolve_error = {},
                                            .mesh_error = converted.error()});
    }
    hitboxes.push_back(*std::move(converted));
  }
  return hitboxes;
}
#endif

}  // namespace augusta::client
