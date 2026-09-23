#ifndef AUGUSTA_CLIENT_SCENE_LOADER_H_
#define AUGUSTA_CLIENT_SCENE_LOADER_H_

#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include "augusta/assets.h"
#include "augusta/renderer.h"

// Turns a cooked scene graph (ADR-0032) into what augusta::renderer draws:
// every mesh node's geometry in world space, plus a camera placed at the
// scene's first spawn point (or the renderer's default camera if it has none).
namespace augusta::client {

/// Key of the node property (ADR-0032) holding a mesh's base color: three
/// space-separated linear RGB floats, e.g. "0.35 0.38 0.35".
inline constexpr std::string_view kBaseColorProperty = "base_color";

/// Looks up a mesh by its pack-relative path, e.g. Pack::ResolveMesh.
using MeshResolver = std::function<std::expected<assets::MeshData, assets::ResolveError>(std::string_view)>;

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
};

/// A failure to build a render scene: what went wrong (code) and what it is
/// about (the other fields, empty or default when the code has none).
struct SceneError {
  SceneErrorCode code;
  std::string node{};
  std::string subject{};
  assets::ResolveError resolve_error{};
};

/// A message for error fit to print to whoever runs the process.
std::string DescribeSceneError(const SceneError& error);

/// Builds the render scene for scene, resolving each node's mesh through
/// resolve_mesh. The error names the node or mesh that could not be used.
std::expected<renderer::Scene, SceneError> BuildRenderScene(const assets::SceneData& scene,
                                                            const MeshResolver& resolve_mesh);

/// Resolves the scene graph at scene_path in pack and builds its render scene.
std::expected<renderer::Scene, SceneError> LoadRenderScene(const assets::Pack& pack,
                                                           std::string_view scene_path = assets::kScenePath);

/// Pack-relative path of the mesh the renderer draws every RemotePlayer as
/// (issue #82's placeholder box, replaced by the one example character -
/// ADR-0040/ADR-0041): no per-player character selection exists yet, so
/// every RemotePlayer is this same character's Visual mesh.
inline constexpr std::string_view kRemotePlayerMeshPath = "characters/player/Player/Visual";

/// Resolves the mesh at mesh_path in pack for Renderer::SetRemotePlayerMesh.
/// Unlike LoadRenderScene's meshes, no world transform is applied: the
/// points are already in the character's own root space (baked in at cook
/// time, ADR-0041) - Renderer translates them per RemotePlayer instance
/// instead.
std::expected<renderer::SceneMesh, SceneError> LoadRemotePlayerMesh(const assets::Pack& pack,
                                                                    std::string_view mesh_path = kRemotePlayerMeshPath);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_SCENE_LOADER_H_
