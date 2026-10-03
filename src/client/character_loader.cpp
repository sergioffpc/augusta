#include "character_loader.h"

#include <algorithm>
#include <expected>
#include <format>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/renderer.h"
#include "scene_loader.h"

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
  }
  return "unknown character error";
}

std::vector<std::string> CharactersToLoad(std::span<const std::string> others,
                                          const std::set<std::string, std::less<>>& loaded) {
  std::vector<std::string> to_load;
  for (const std::string& character : others) {
    if (!loaded.contains(character) && std::ranges::find(to_load, character) == to_load.end()) {
      to_load.push_back(character);
    }
  }
  return to_load;
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

}  // namespace augusta::client
