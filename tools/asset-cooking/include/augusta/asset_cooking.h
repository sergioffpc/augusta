#ifndef AUGUSTA_ASSET_COOKING_H_
#define AUGUSTA_ASSET_COOKING_H_

#include <cstddef>
#include <expected>
#include <filesystem>

// augusta::asset_cooking is the offline asset cooker (ADR-0030/ADR-0031):
// turns an authored OpenUSD stage into a runtime pack augusta::assets can
// load. Offline-only - never linked into the shipped client/server
// binaries (ARCHITECTURE.md "Tooling").
//
// This is the walking-skeleton slice (ROADMAP.md M2, issue #46): Cook()
// reads UsdGeomMesh prims directly off the given stage and writes them
// into a pack matching ADR-0031's header/data/index layout. The full
// pipeline order (usd-optimize -> usd-validation-nvidia -> DirectXTex ->
// meshoptimizer -> pack -> hash -> sign, ADR-0030) and the hash/sign
// trailer are not implemented yet.
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
  // The assembled pack could not be written to output_pack_path.
  kPackWriteFailed,
};

// What Cook() did, for the caller (CLI or test) to report.
struct CookReport {
  std::size_t mesh_count = 0;
};

// Reads every UsdGeomMesh prim from the stage at stage_path and writes
// them into a new pack file at output_pack_path, addressed by their
// sanitized USD prim path (ADR-0031). Overwrites output_pack_path if it
// already exists.
std::expected<CookReport, CookError> Cook(const std::filesystem::path& stage_path,
                                           const std::filesystem::path& output_pack_path);

}  // namespace augusta::asset_cooking

#endif  // AUGUSTA_ASSET_COOKING_H_
