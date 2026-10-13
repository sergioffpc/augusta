#ifndef AUGUSTA_RENDERER_VERTICES_H_
#define AUGUSTA_RENDERER_VERTICES_H_

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/math.h"
#include "augusta/renderer.h"

/// \file
/// The CPU-side geometry the Renderer draws, built from what its callers pass
/// it, and kept free of Falcor so it is built and tested on every platform:
/// renderer.cpp uploads and draws it.
namespace augusta::renderer {

/// One vertex of the flat-shaded scene geometry - see BuildFlatShadedVertices.
/// The effects and the overlay (Effects.3d.slang) use it too, unlit: their
/// normal is zero, and goes unused. Three tightly packed float triples, as the
/// GPU reads them.
struct Vertex {
  math::Vec3 position;
  math::Vec3 normal;
  math::Vec3 color;
};

/// Each character's flat-shaded vertices, in its own local space, by character
/// name - see BuildRemoteVertices.
using CharacterVertices = std::unordered_map<std::string, std::vector<Vertex>>;

/// Expands every mesh's indexed triangles into 3 unshared vertices each,
/// carrying the triangle's own face normal and its mesh's color: cooked meshes
/// have positions and indices only (no normals - see assets::MeshData), so flat
/// shading is the one lighting model the data supports. Throws
/// std::runtime_error on an index at or past its mesh's position count.
std::vector<Vertex> BuildFlatShadedVertices(const Scene& scene);

/// Every RemotePlayer as an instance of its character's local vertices
/// (SetCharacterMesh's own flat-shaded vertices, in the character's local space
/// - ADR-0040/ADR-0041), skipping one whose character has none: turned about
/// its origin by that instance's yaw, normals with it, then translated to its
/// own position and given its own color. remote.position is where the mesh's
/// own origin (y=0) lands - the same convention the character's mesh was cooked
/// around, so no further placement is needed.
std::vector<Vertex> BuildRemoteVertices(std::span<const RemotePlayer> remote_players,
                                        const CharacterVertices& character_vertices);

/// Every tracer, impact and muzzle flash of effects, in world space, facing
/// camera.
std::vector<Vertex> BuildEffectVertices(const CombatEffects& effects, const Camera& camera);

/// The crosshair and hit marker of overlay, in clip space for a frame of width
/// by height pixels, so they keep their size in pixels whatever the window's.
std::vector<Vertex> BuildOverlayVertices(const Overlay& overlay, std::uint32_t width, std::uint32_t height);

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_VERTICES_H_
