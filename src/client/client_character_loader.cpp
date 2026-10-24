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
#include "augusta/animation.h"
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
    case CharacterErrorCode::kSkeletonUnresolved:
      return std::format("skeleton of character {} {}", error.character,
                         assets::DescribeResolveError(error.resolve_error, "skeleton"));
    case CharacterErrorCode::kJointUnresolved:
      return std::format("a hitbox of character {} names no joint of its skeleton: {}", error.character, error.subject);
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
    std::string_view character, const SkeletonResolver& resolve_skeleton, const HitboxesResolver& resolve_hitboxes) {
  const auto failed = [character](CharacterErrorCode code, std::string subject = {},
                                  assets::ResolveError resolve_error = {},
                                  physics::CollisionMeshError mesh_error = {}) {
    return std::unexpected(CharacterError{.code = code,
                                          .character = std::string(character),
                                          .subject = std::move(subject),
                                          .resolve_error = resolve_error,
                                          .mesh_error = mesh_error});
  };
  auto resolved = resolve_hitboxes(character);
  if (!resolved) {
    return failed(CharacterErrorCode::kHitboxesUnresolved, {}, resolved.error());
  }
  const auto skeleton = resolve_skeleton(character);
  if (!skeleton) {
    return failed(CharacterErrorCode::kSkeletonUnresolved, {}, skeleton.error());
  }
  auto bound = assets::BindHitboxes(*skeleton, *std::move(resolved));
  if (!bound) {
    return failed(CharacterErrorCode::kJointUnresolved, std::move(bound.error().joint));
  }
  std::vector<hitbox::CharacterHitbox> hitboxes;
  hitboxes.reserve(bound->size());
  for (const assets::JointHitbox& one : *bound) {
    auto converted = hitbox::FromPack(one);
    if (!converted) {
      return failed(CharacterErrorCode::kHitboxRejected, std::string(assets::BodyPartName(one.hitbox.part)), {},
                    converted.error());
    }
    hitboxes.push_back(*std::move(converted));
  }
  std::vector<hitbox::CharacterHitbox> in_root_space;
  in_root_space.reserve(hitboxes.size());
  for (hitbox::PlacedHitbox& placed : animation::PlaceAtRest(*skeleton, hitboxes, hitbox::Pose{})) {
    in_root_space.push_back(hitbox::CharacterHitbox{.part = placed.part, .triangles = std::move(placed.triangles)});
  }
  return in_root_space;
}
#endif

}  // namespace augusta::client
