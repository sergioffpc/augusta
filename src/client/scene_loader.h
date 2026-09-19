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

/// Pack-relative path the cooker files a stage's scene graph under.
inline constexpr std::string_view kScenePath = "Scene";

/// Key of the node property (ADR-0032) holding a mesh's base color: three
/// space-separated linear RGB floats, e.g. "0.35 0.38 0.35".
inline constexpr std::string_view kBaseColorProperty = "base_color";

/// Looks up a mesh by its pack-relative path, e.g. Pack::ResolveMesh.
using MeshResolver = std::function<std::expected<assets::MeshData, assets::ResolveError>(std::string_view)>;

/// Builds the render scene for scene, resolving each node's mesh through
/// resolve_mesh. The error names the node or mesh that could not be used.
std::expected<renderer::Scene, std::string> BuildRenderScene(const assets::SceneData& scene,
                                                             const MeshResolver& resolve_mesh);

/// Resolves the scene graph at scene_path in pack and builds its render scene.
std::expected<renderer::Scene, std::string> LoadRenderScene(const assets::Pack& pack,
                                                            std::string_view scene_path = kScenePath);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_SCENE_LOADER_H_
