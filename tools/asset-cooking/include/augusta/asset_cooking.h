#ifndef AUGUSTA_ASSET_COOKING_H_
#define AUGUSTA_ASSET_COOKING_H_

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>

#include "augusta/assets.h"

// augusta::asset_cooking is the offline asset cooker (ADR-0030/ADR-0031/
// ADR-0032): turns an authored OpenUSD stage into a runtime pack
// augusta::assets can load. Offline-only - never linked into the shipped
// client/server binaries (ARCHITECTURE.md "Tooling").
//
// Cook() walks every prim in the stage, emitting one mesh blob per
// UsdGeomMesh (as before) plus a single scene-graph blob (ADR-0032) tying
// the whole stage's hierarchy/transforms/references together. The
// authored stage's upAxis/metersPerUnit are normalized into the runtime's
// fixed Y-up/right-handed/1-meter convention by correcting each
// top-level node's transform - nothing downstream ever branches on how a
// given stage was authored. Collider/hitbox blob *content* (PhysX-
// authored collision geometry, ADR-0031's ADR-0007-reuse) is not
// implemented yet - only the spawn-point marker and each node's
// mesh/hitbox *reference fields* are populated (see cook.cpp's ReadNode
// for exactly what's read per prim); resolving a hitbox_path today
// yields ResolveError::kNotFound until that follow-up lands, which is
// expected fail-closed behavior, not a bug.
namespace augusta::asset_cooking {

enum class CookError {
  // stage_path could not be opened as a USD stage.
  kStageOpenFailed,
  // A UsdGeomMesh prim's faces are not already triangles. Cook() does not
  // triangulate (that's usd-optimize's job, ADR-0030) - it only reads
  // pre-triangulated mesh data.
  kUnsupportedTopology,
  // A UsdGeomMesh prim is missing points, faceVertexCounts, or
  // faceVertexIndices (e.g. a stub prim, or one whose geometry comes from
  // a layer that hasn't composed in). Cook() refuses to write an
  // artificially-empty mesh blob in its place.
  kMissingMeshData,
  // A UsdGeomMesh prim's faceVertexIndices contains a negative value -
  // USD represents these as signed ints, but the pack format's indices
  // are unsigned; casting a negative value would silently wrap to a huge
  // index rather than reporting the malformed source data.
  kNegativeIndex,
  // A UsdGeomMesh prim's faceVertexIndices contains a value that isn't a
  // valid index into that same prim's points.
  kIndexOutOfRange,
  // A UsdGeomMesh prim's faceVertexCounts don't sum to the length of its
  // faceVertexIndices - the two arrays disagree about the mesh's own
  // topology.
  kInconsistentTopology,
  // A mesh or the scene graph exceeded augusta::assets' pragmatic v1
  // size limits (EncodeError::kTooLarge).
  kContentTooLarge,
  // A UsdShadeShader prim with info:id "UsdUVTexture" has no inputs:file
  // attribute at all (an SdfAssetPath value that's merely unresolved -
  // e.g. naming a file that doesn't exist - still reads successfully and
  // surfaces later as kTextureLoadFailed instead, once DirectXTex
  // actually tries to open it).
  kMissingTextureFile,
  // DirectXTex could not load the texture's source image - unsupported/
  // corrupt image data, or inputs:file names a path that doesn't exist
  // (see CookErrorDetail::message).
  kTextureLoadFailed,
  // DirectXTex could not block-compress the loaded image (e.g. a
  // dimension not a multiple of 4, which BC7/BC5/BC4 require).
  kTextureCompressFailed,
  // The assembled pack could not be written to output_pack_path
  // (augusta::assets::WriteError - see CookErrorDetail::message).
  kPackWriteFailed,
};

// What Cook() did, for the caller (CLI or test) to report.
struct CookReport {
  std::size_t mesh_count = 0;
  std::size_t texture_count = 0;
  std::size_t node_count = 0;
};

// A failed Cook() call's detail: which stage of the pipeline failed, the
// USD prim path responsible (empty if the failure isn't tied to one
// specific prim, e.g. a pack-write failure), and a human-readable cause -
// replaces the CLI's old bare "cook failed" (main.cpp) with something a
// caller can actually act on.
struct CookErrorDetail {
  CookError code;
  std::string prim_path;
  std::string message;
};

// Reads every prim from the stage at stage_path, building a scene graph
// (ADR-0032) plus one mesh blob per UsdGeomMesh prim, and writes them
// into a new pack file at output_pack_path signed with signing_key.
// Overwrites output_pack_path if it already exists (atomically - see
// augusta::assets::WritePack).
std::expected<CookReport, CookErrorDetail> Cook(const std::filesystem::path& stage_path,
                                                const std::filesystem::path& output_pack_path,
                                                const assets::Ed25519PrivateKey& signing_key);

}  // namespace augusta::asset_cooking

#endif  // AUGUSTA_ASSET_COOKING_H_
