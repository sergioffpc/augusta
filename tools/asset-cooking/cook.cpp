#include "augusta/asset_cooking.h"

#include <cstdint>
#include <string>
#include <vector>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

#include "augusta/assets.h"

namespace augusta::asset_cooking {

namespace {

// vcpkg's usd port doesn't wire up OpenUSD's plugin registry for a
// caller (see tools/asset-cooking/CMakeLists.txt's AUGUSTA_USD_PLUGIN_PATH
// comment) - without this, UsdStage::Open aborts the process the moment
// it needs a registered Sdf file format. Registering directly through
// PlugRegistry (rather than via the PXR_PLUGINPATH_NAME env var) is
// deliberate: vcpkg's usd DLLs cache their own copy of the process
// environment at DLL-load time, before this function - or any of our own
// code - ever runs, so an env var this process sets afterward is
// invisible to them; a direct C++ call is the only way in. Must run
// before any other pxr:: call, since that's what constructs the
// PlugRegistry singleton in the first place.
void EnsureUsdPluginPathConfigured() {
  static const bool kConfigured = [] {
    pxr::PlugRegistry::GetInstance().RegisterPlugins(std::vector<std::string>{AUGUSTA_USD_PLUGIN_PATH});
    return true;
  }();
  (void)kConfigured;
}

}  // namespace

std::expected<CookReport, CookError> Cook(const std::filesystem::path& stage_path,
                                           const std::filesystem::path& output_pack_path) {
  EnsureUsdPluginPathConfigured();

  const pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(stage_path.string());
  if (!stage) {
    return std::unexpected(CookError::kStageOpenFailed);
  }

  std::vector<assets::AssetEntry> entries;
  for (const pxr::UsdPrim& prim : stage->Traverse()) {
    if (!prim.IsA<pxr::UsdGeomMesh>()) {
      continue;
    }

    const pxr::UsdGeomMesh mesh(prim);

    // The walking skeleton reads mesh data as-authored - no
    // usd-optimize/triangulation pass exists yet (ADR-0030), so a
    // non-triangulated face would silently produce a wrong index buffer
    // if read as one.
    pxr::VtArray<int> face_vertex_counts;
    pxr::VtArray<pxr::GfVec3f> usd_points;
    pxr::VtArray<int> face_vertex_indices;
    if (!mesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts) || !mesh.GetPointsAttr().Get(&usd_points) ||
        !mesh.GetFaceVertexIndicesAttr().Get(&face_vertex_indices)) {
      return std::unexpected(CookError::kMissingMeshData);
    }

    for (int count : face_vertex_counts) {
      if (count != 3) {
        return std::unexpected(CookError::kUnsupportedTopology);
      }
    }

    assets::MeshData mesh_data;
    mesh_data.points.reserve(usd_points.size());
    for (const pxr::GfVec3f& point : usd_points) {
      mesh_data.points.emplace_back(point[0], point[1], point[2]);
    }
    mesh_data.indices.reserve(face_vertex_indices.size());
    for (int index : face_vertex_indices) {
      mesh_data.indices.push_back(static_cast<std::uint32_t>(index));
    }

    entries.push_back(assets::AssetEntry{
        assets::AssetType::kMesh,
        assets::SanitizePrimPath(prim.GetPath().GetString()),
        assets::EncodeMeshBlob(mesh_data),
    });
  }

  if (!assets::WritePack(output_pack_path, entries)) {
    return std::unexpected(CookError::kPackWriteFailed);
  }

  return CookReport{entries.size()};
}

}  // namespace augusta::asset_cooking
