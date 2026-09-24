#ifndef AUGUSTA_CLIENT_SCENE_LOADER_H_
#define AUGUSTA_CLIENT_SCENE_LOADER_H_

#include <cstdint>
#include <expected>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/renderer.h"

// Turns a cooked scene graph (ADR-0032) into what augusta::renderer draws:
// every mesh node's geometry in world space, plus a camera placed at the local
// player's eye as if it stood at the scene's first spawn point (or the
// renderer's default camera if it has none); and resolves the mesh each
// character is drawn as and the eye the local player's camera sits at.
namespace augusta::client {

/// Key of the node property (ADR-0032) holding a mesh's base color: three
/// space-separated linear RGB floats, e.g. "0.35 0.38 0.35".
inline constexpr std::string_view kBaseColorProperty = "base_color";

/// Looks up a mesh by its pack-relative path, e.g. Pack::ResolveMesh.
using MeshResolver = std::function<std::expected<assets::MeshData, assets::ResolveError>(std::string_view)>;

/// Looks up a character's eye by its pack-relative path, e.g. Pack::ResolveEye.
using EyeResolver = std::function<std::expected<assets::EyeData, assets::ResolveError>(std::string_view)>;

/// Why a render scene could not be built.
enum class SceneErrorCode {
  /// The scene graph could not be resolved from the pack; subject is its path
  /// and resolve_error says why.
  kSceneUnresolved,
  /// A node's mesh could not be resolved; node is the node's name, subject the
  /// mesh's path and resolve_error says why.
  kMeshUnresolved,
  /// A node's base color is not three floats; node is the node's name and
  /// subject the malformed value.
  kMalformedBaseColor,
  /// A character index names no character of the pack's list; subject is the index.
  kUnknownCharacter,
  /// A character's visual mesh could not be resolved; node is the character's
  /// path, subject the mesh's and resolve_error says why.
  kCharacterMeshUnresolved,
  /// A character's eye could not be resolved; node is the character's path,
  /// subject the eye's and resolve_error says why.
  kCharacterEyeUnresolved,
};

/// A failure to build a render scene: what went wrong (code) and what it is
/// about (the other fields, empty or default when the code has none).
struct SceneError {
  SceneErrorCode code;
  std::string node;
  std::string subject;
  assets::ResolveError resolve_error{};
};

/// A message for error fit to print to whoever runs the process.
std::string DescribeSceneError(const SceneError& error);

/// Builds the render scene for scene, resolving each node's mesh through
/// resolve_mesh; eye is the local player's (LoadCharacterEye), where its camera
/// sits above the first spawn point. The error names the node or mesh that
/// could not be used.
std::expected<renderer::Scene, SceneError> BuildRenderScene(const assets::SceneData& scene,
                                                            const MeshResolver& resolve_mesh, const math::Vec3& eye);

/// Resolves the scene graph at scene_path in pack and builds its render scene,
/// with eye as in BuildRenderScene.
std::expected<renderer::Scene, SceneError> LoadRenderScene(const assets::Pack& pack, const math::Vec3& eye,
                                                           std::string_view scene_path = assets::kScenePath);

/// The characters among others (every other Lobby player's, ADR-0043) whose
/// meshes are not in loaded: each once, in the order they first appear.
std::vector<std::uint8_t> CharactersToLoad(std::span<const std::uint8_t> others, const std::set<std::uint8_t>& loaded);

/// Pack-relative path of character's visual mesh: its `Character` root prim's
/// `Visual` child (ADR-0040), e.g. "characters/player/Character/Visual".
std::string CharacterMeshPath(std::string_view character);

/// Resolves through resolve_mesh the visual mesh of the character whose index
/// is character_index in characters, the pack's character list (index N is
/// element N-1, ADR-0042), for Renderer::SetCharacterMesh. Unlike
/// LoadRenderScene's meshes, no world transform is applied: the points are
/// already in the character's own root space (baked in at cook time, ADR-0041),
/// and the renderer places them per player. The error names the character.
std::expected<renderer::SceneMesh, SceneError> LoadCharacterMesh(std::span<const std::string> characters,
                                                                 std::uint8_t character_index,
                                                                 const MeshResolver& resolve_mesh);

/// Pack-relative path of character's eye: its `Character` root prim's `Eye`
/// child (ADR-0040), e.g. "characters/player/Character/Eye".
std::string CharacterEyePath(std::string_view character);

/// Resolves through resolve_eye the eye of character, by its path relative to
/// `authoring/` (e.g. "characters/player"): where the camera sits, in the
/// character's own root space, its feet at the origin. The error names the
/// character.
std::expected<math::Vec3, SceneError> LoadCharacterEye(std::string_view character, const EyeResolver& resolve_eye);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_SCENE_LOADER_H_
