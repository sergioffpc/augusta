#ifndef AUGUSTA_CLIENT_CHARACTER_LOADER_H_
#define AUGUSTA_CLIENT_CHARACTER_LOADER_H_

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/renderer.h"
#include "scene_loader.h"

/// \file
/// Resolves what the client needs of a character (ADR-0040, ADR-0042): the mesh
/// it is drawn as and the eye its camera sits at.
namespace augusta::client {

/// Looks up a character's eye by its pack-relative path, e.g. Pack::ResolveEye.
using EyeResolver = std::function<std::expected<assets::EyeData, assets::ResolveError>(std::string_view)>;

/// Why a character could not be loaded.
enum class CharacterErrorCode {
  /// The character is not in the pack's character list.
  kUnknownCharacter,
  /// The character's visual mesh could not be resolved; subject is the mesh's
  /// path and resolve_error says why.
  kMeshUnresolved,
  /// The character's eye could not be resolved; subject is the eye's path and
  /// resolve_error says why.
  kEyeUnresolved,
};

/// A failure to load a character: what went wrong (code), the character's name
/// and what it is about (the other fields, empty or default when the code has
/// none).
struct CharacterError {
  CharacterErrorCode code;
  std::string character;
  std::string subject;
  assets::ResolveError resolve_error{};
};

/// A message for error fit to print to whoever runs the process.
std::string DescribeCharacterError(const CharacterError& error);

/// Resolves through resolve_mesh the visual mesh of character, which must be one
/// of characters, the pack's character list (ADR-0042), for
/// Renderer::SetCharacterMesh. Unlike LoadRenderScene's meshes, no world
/// transform is applied: the points are already in the character's own root
/// space (baked in at cook time, ADR-0041), and the renderer places them per
/// player.
std::expected<renderer::SceneMesh, CharacterError> LoadCharacterMesh(std::span<const std::string> characters,
                                                                     std::string_view character,
                                                                     const MeshResolver& resolve_mesh);

/// Resolves through resolve_eye the eye of character, by its name in the
/// scenario's manifest (e.g. "soldier"), at assets::CharacterEyePath: where
/// the camera sits, in the character's own root space, its feet at the origin.
std::expected<math::Vec3, CharacterError> LoadCharacterEye(std::string_view character, const EyeResolver& resolve_eye);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_CHARACTER_LOADER_H_
