#include <meshoptimizer.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/asset_cooking.h"
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

// augusta:spawnPoint / augusta:hitbox: custom bool attributes (ADR-0032's
// authoring convention) rather than a native USD prim type - USD has no
// built-in notion of either. Hitbox *content* (actual collision geometry)
// isn't cooked yet (see asset_cooking.h's top comment); only this marker
// is read, so a resolved hitbox_path currently always misses.
constexpr auto kSpawnPointAttr = "augusta:spawnPoint";
constexpr auto kHitboxAttr = "augusta:hitbox";

// Z-up -> Y-up is a quarter turn about the X axis (see
// StageCorrectionMatrix's comment for the sign/direction reasoning).
constexpr double kZUpToYUpDegrees = -90.0;

bool ReadBoolAttr(const pxr::UsdPrim& prim, const char* attr_name) {
  const pxr::UsdAttribute attr = prim.GetAttribute(pxr::TfToken(attr_name));
  bool value = false;
  if (attr && attr.Get(&value)) {
    return value;
  }
  return false;
}

// Decomposes a USD local-to-parent transform matrix into the
// translation/rotation/scale triple augusta::assets::SceneNode stores
// (ADR-0032 stores local transforms only, never world).
struct DecomposedTransform {
  math::Vec3 translation;
  math::Quat rotation;
  math::Vec3 scale;
};

DecomposedTransform Decompose(const pxr::GfMatrix4d& matrix) {
  const pxr::GfTransform transform(matrix);
  const pxr::GfVec3d translation = transform.GetTranslation();
  const pxr::GfQuatd rotation = transform.GetRotation().GetQuat();
  const pxr::GfVec3d scale = transform.GetScale();
  const pxr::GfVec3d rotation_imaginary = rotation.GetImaginary();
  return DecomposedTransform{
      .translation = math::Vec3(static_cast<float>(translation[0]), static_cast<float>(translation[1]),
                                static_cast<float>(translation[2])),
      .rotation = math::Quat(static_cast<float>(rotation.GetReal()), static_cast<float>(rotation_imaginary[0]),
                             static_cast<float>(rotation_imaginary[1]), static_cast<float>(rotation_imaginary[2])),
      .scale = math::Vec3(static_cast<float>(scale[0]), static_cast<float>(scale[1]), static_cast<float>(scale[2])),
  };
}

DecomposedTransform LocalTransformOf(const pxr::UsdPrim& prim) {
  const pxr::UsdGeomXformable xformable(prim);
  if (!xformable) {
    return DecomposedTransform{
        .translation = math::Vec3(0.0F),
        .rotation = math::Quat(1.0F, 0.0F, 0.0F, 0.0F),
        .scale = math::Vec3(1.0F),
    };
  }
  pxr::GfMatrix4d matrix(1.0);
  bool resets_xform_stack = false;
  xformable.GetLocalTransformation(&matrix, &resets_xform_stack);
  return Decompose(matrix);
}

// Corrects a stage's own upAxis/metersPerUnit into the runtime's fixed
// Y-up/right-handed/1-meter convention (ADR-0032). USD stages are always
// right-handed by spec - only the up axis and unit scale vary - so this
// is a pure rotation (Z-up -> Y-up) composed with a uniform scale
// (metersPerUnit), never a winding-order-affecting reflection. Applied
// once, prepended to each top-level node's own local transform: every
// descendant's world transform is this correction times its USD-authored
// world transform, purely from chained matrix multiplication, without
// touching any other node's stored local transform or any mesh's point
// data.
pxr::GfMatrix4d StageCorrectionMatrix(const pxr::UsdStageRefPtr& stage) {
  const double meters_per_unit = pxr::UsdGeomGetStageMetersPerUnit(stage);
  pxr::GfMatrix4d correction = pxr::GfMatrix4d(1.0).SetScale(meters_per_unit);
  if (pxr::UsdGeomGetStageUpAxis(stage) == pxr::UsdGeomTokens->z) {
    pxr::GfMatrix4d rotate_z_to_y(1.0);
    rotate_z_to_y.SetRotate(pxr::GfRotation(pxr::GfVec3d(1.0, 0.0, 0.0), kZUpToYUpDegrees));
    correction = rotate_z_to_y * correction;
  }
  return correction;
}

// Validates that every face is a triangle and that faceVertexCounts sums
// to faceVertexIndices' own length, returning that sum (== the expected
// index count) on success.
std::expected<std::size_t, CookErrorDetail> ValidateTriangleTopology(const pxr::VtArray<int>& face_vertex_counts,
                                                                     std::size_t face_vertex_index_count,
                                                                     const std::string& prim_path) {
  // The walking skeleton reads mesh data as-authored - no
  // usd-optimize/triangulation pass exists yet (ADR-0030), so a
  // non-triangulated face would silently produce a wrong index buffer if
  // read as one.
  std::size_t expected_index_count = 0;
  for (int count : face_vertex_counts) {
    if (count != 3) {
      return std::unexpected(CookErrorDetail{
          .code = CookError::kUnsupportedTopology,
          .prim_path = prim_path,
          .message = std::format("face with {} vertices, only triangles (3) are supported", count),
      });
    }
    expected_index_count += static_cast<std::size_t>(count);
  }
  if (expected_index_count != face_vertex_index_count) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kInconsistentTopology,
        .prim_path = prim_path,
        .message = std::format("faceVertexCounts sums to {} but faceVertexIndices has {} entries", expected_index_count,
                               face_vertex_index_count),
    });
  }
  return expected_index_count;
}

// Converts face_vertex_indices to the pack format's unsigned indices,
// rejecting negative values (rather than silently wrapping them, see
// CookError::kNegativeIndex) and out-of-range ones (CookError::kIndexOutOfRange).
std::expected<std::vector<std::uint32_t>, CookErrorDetail> ReadIndices(const pxr::VtArray<int>& face_vertex_indices,
                                                                       std::size_t point_count,
                                                                       const std::string& prim_path) {
  std::vector<std::uint32_t> indices;
  indices.reserve(face_vertex_indices.size());
  for (int index : face_vertex_indices) {
    if (index < 0) {
      return std::unexpected(CookErrorDetail{
          .code = CookError::kNegativeIndex,
          .prim_path = prim_path,
          .message = std::format("negative index {}", index),
      });
    }
    const auto unsigned_index = static_cast<std::uint32_t>(index);
    if (unsigned_index >= point_count) {
      return std::unexpected(CookErrorDetail{
          .code = CookError::kIndexOutOfRange,
          .prim_path = prim_path,
          .message = std::format("index {} out of range for {} points", unsigned_index, point_count),
      });
    }
    indices.push_back(unsigned_index);
  }
  return indices;
}

// meshoptimizer pass (ADR-0016: vertex cache optimization, simplification,
// quantization) - turns the as-authored read into GPU-ready data rather
// than a passthrough of the source mesh. MeshData has no per-vertex
// attributes beyond position yet, so vertex identity here is position
// identity.
void OptimizeMesh(assets::MeshData& mesh) {
  const std::size_t vertex_count = mesh.points.size();
  const std::size_t index_count = mesh.indices.size();
  if (vertex_count == 0 || index_count == 0) {
    return;
  }

  // Weld vertices that share the exact same position first - every later
  // pass assumes the vertex buffer has no redundant entries, and authored
  // content routinely has coincident points from mirrored/merged geometry.
  std::vector<unsigned int> remap(vertex_count);
  const std::size_t unique_vertex_count = meshopt_generateVertexRemap(
      remap.data(), mesh.indices.data(), index_count, mesh.points.data(), vertex_count, sizeof(math::Vec3));

  std::vector<std::uint32_t> welded_indices(index_count);
  meshopt_remapIndexBuffer(welded_indices.data(), mesh.indices.data(), index_count, remap.data());

  std::vector<math::Vec3> welded_points(unique_vertex_count);
  meshopt_remapVertexBuffer(welded_points.data(), mesh.points.data(), vertex_count, sizeof(math::Vec3), remap.data());

  mesh.indices = std::move(welded_indices);
  mesh.points = std::move(welded_points);

  // Collapse degenerate/redundant triangles (e.g. the duplicate faces
  // welding above can expose) within a tight 1%-of-extents error budget -
  // conservative enough to leave legitimate detail alone, unlike a target
  // triangle-count ratio that would decimate every mesh uniformly.
  // target_index_count 0 means "simplify as far as target_error allows".
  std::vector<std::uint32_t> simplified_indices(mesh.indices.size());
  float simplify_error = 0.0F;
  const std::size_t simplified_index_count =
      meshopt_simplify(simplified_indices.data(), mesh.indices.data(), mesh.indices.size(),
                       reinterpret_cast<const float*>(mesh.points.data()), mesh.points.size(), sizeof(math::Vec3),
                       /*target_index_count=*/0, /*target_error=*/0.01F, /*options=*/0, &simplify_error);
  simplified_indices.resize(simplified_index_count);
  mesh.indices = std::move(simplified_indices);

  // Vertex cache optimization: reorder indices for GPU post-transform
  // cache locality, without touching the vertex buffer.
  meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), mesh.points.size());

  // Vertex fetch optimization: reorder the vertex buffer itself (indices
  // are remapped in place to match) for cache-friendly fetch order.
  std::vector<math::Vec3> fetch_optimized_points(mesh.points.size());
  const std::size_t fetch_vertex_count =
      meshopt_optimizeVertexFetch(fetch_optimized_points.data(), mesh.indices.data(), mesh.indices.size(),
                                  mesh.points.data(), mesh.points.size(), sizeof(math::Vec3));
  fetch_optimized_points.resize(fetch_vertex_count);
  mesh.points = std::move(fetch_optimized_points);

  // Quantization: snap each position component to a limited mantissa
  // precision (meshopt_quantizeFloat) - reduces stored-value entropy for
  // later compression without changing the pack's on-disk vertex format,
  // which stays plain float32 (ADR-0031, EncodeMeshBlob).
  constexpr int kQuantizationMantissaBits = 12;
  for (math::Vec3& point : mesh.points) {
    point.x = meshopt_quantizeFloat(point.x, kQuantizationMantissaBits);
    point.y = meshopt_quantizeFloat(point.y, kQuantizationMantissaBits);
    point.z = meshopt_quantizeFloat(point.z, kQuantizationMantissaBits);
  }
}

std::expected<assets::MeshData, CookErrorDetail> ReadMesh(const pxr::UsdGeomMesh& mesh, const std::string& prim_path) {
  pxr::VtArray<int> face_vertex_counts;
  pxr::VtArray<pxr::GfVec3f> usd_points;
  pxr::VtArray<int> face_vertex_indices;
  if (!mesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts) || !mesh.GetPointsAttr().Get(&usd_points) ||
      !mesh.GetFaceVertexIndicesAttr().Get(&face_vertex_indices)) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kMissingMeshData,
        .prim_path = prim_path,
        .message = "missing points, faceVertexCounts, or faceVertexIndices",
    });
  }

  if (auto topology = ValidateTriangleTopology(face_vertex_counts, face_vertex_indices.size(), prim_path); !topology) {
    return std::unexpected(topology.error());
  }

  assets::MeshData mesh_data;
  mesh_data.points.reserve(usd_points.size());
  for (const pxr::GfVec3f& point : usd_points) {
    mesh_data.points.emplace_back(point[0], point[1], point[2]);
  }

  auto indices = ReadIndices(face_vertex_indices, mesh_data.points.size(), prim_path);
  if (!indices) {
    return std::unexpected(indices.error());
  }
  mesh_data.indices = std::move(*indices);

  OptimizeMesh(mesh_data);

  return mesh_data;
}

using NodeIndexMap = std::unordered_map<pxr::SdfPath, std::uint32_t, pxr::SdfPath::Hash>;

// Builds prim's own SceneNode (name, parent link, corrected transform,
// spawn-point/hitbox markers), appending a mesh AssetEntry to entries if
// prim is a UsdGeomMesh. node_index_of must already contain every
// ancestor of prim - guaranteed by pre-order traversal (see Cook()'s own
// comment on its Traverse() call).
std::expected<assets::SceneNode, CookErrorDetail> BuildNode(const pxr::UsdPrim& prim, const pxr::GfMatrix4d& correction,
                                                            const NodeIndexMap& node_index_of,
                                                            std::vector<assets::AssetEntry>& entries) {
  const std::string prim_path = assets::SanitizePrimPath(prim.GetPath().GetString());

  assets::SceneNode node;
  node.name = prim_path;

  const pxr::UsdPrim parent = prim.GetParent();
  if (!parent.IsValid() || parent.IsPseudoRoot()) {
    node.parent_index = assets::kSceneNodeNoParent;
  } else {
    // Always succeeds under pre-order traversal - see this function's own
    // comment.
    node.parent_index = node_index_of.at(parent.GetPath());
  }

  DecomposedTransform local = LocalTransformOf(prim);
  if (node.parent_index == assets::kSceneNodeNoParent) {
    // Only the stage's own top-level (parentless) nodes get the
    // up-axis/unit correction prepended - see StageCorrectionMatrix's
    // comment for why that's sufficient for the whole hierarchy.
    pxr::GfMatrix4d local_matrix(1.0);
    bool resets_xform_stack = false;
    const pxr::UsdGeomXformable xformable(prim);
    if (xformable) {
      xformable.GetLocalTransformation(&local_matrix, &resets_xform_stack);
    }
    // GfMatrix4d is row-vector convention (v' = v * M; "apply A then B"
    // composes as A * B) - local_matrix must be applied first (it's the
    // prim's own authored transform), correction second (it maps that
    // result from the stage's own axis/units into the runtime's).
    local = Decompose(local_matrix * correction);
  }
  node.translation = local.translation;
  node.rotation = local.rotation;
  node.scale = local.scale;

  node.is_spawn_point = ReadBoolAttr(prim, kSpawnPointAttr);
  if (ReadBoolAttr(prim, kHitboxAttr)) {
    node.hitbox_path = prim_path;
  }

  if (prim.IsA<pxr::UsdGeomMesh>()) {
    auto mesh_data = ReadMesh(pxr::UsdGeomMesh(prim), prim_path);
    if (!mesh_data) {
      return std::unexpected(mesh_data.error());
    }
    auto blob = assets::EncodeMeshBlob(*mesh_data);
    if (!blob) {
      return std::unexpected(CookErrorDetail{
          .code = CookError::kContentTooLarge,
          .prim_path = prim_path,
          .message = "mesh exceeds pack size limits",
      });
    }
    entries.push_back(
        assets::AssetEntry{.type = assets::AssetType::kMesh, .path = prim_path, .data = std::move(*blob)});
    node.mesh_path = prim_path;
  }

  return node;
}

}  // namespace

std::expected<CookReport, CookErrorDetail> Cook(const std::filesystem::path& stage_path,
                                                const std::filesystem::path& output_pack_path,
                                                const assets::Ed25519PrivateKey& signing_key) {
  EnsureUsdPluginPathConfigured();

  const pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(stage_path.string());
  if (!stage) {
    return std::unexpected(
        CookErrorDetail{.code = CookError::kStageOpenFailed, .prim_path = "", .message = stage_path.string()});
  }

  const pxr::GfMatrix4d correction = StageCorrectionMatrix(stage);

  std::vector<assets::AssetEntry> entries;
  assets::SceneData scene;
  NodeIndexMap node_index_of;

  // UsdTraverseInstanceProxies: expands instanceable prototypes into
  // per-instance proxy prims instead of skipping them (Traverse()'s
  // default predicate does not descend into instances at all) - each
  // instance becomes its own node subtree, de-instanced at cook time
  // (ADR-0032's Considered Options). Traverse() visits prims pre-order
  // (a parent always before its children), which BuildNode relies on.
  for (const pxr::UsdPrim& prim : stage->Traverse(pxr::UsdTraverseInstanceProxies(pxr::UsdPrimDefaultPredicate))) {
    auto node = BuildNode(prim, correction, node_index_of, entries);
    if (!node) {
      return std::unexpected(node.error());
    }
    node_index_of.emplace(prim.GetPath(), static_cast<std::uint32_t>(scene.nodes.size()));
    scene.nodes.push_back(std::move(*node));
  }

  auto scene_blob = assets::EncodeSceneBlob(scene);
  if (!scene_blob) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kContentTooLarge,
        .prim_path = "",
        .message = "scene graph exceeds pack size limits",
    });
  }
  entries.push_back(
      assets::AssetEntry{.type = assets::AssetType::kScene, .path = "Scene", .data = std::move(*scene_blob)});

  const auto mesh_count =
      static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const assets::AssetEntry& entry) {
        return entry.type == assets::AssetType::kMesh;
      }));

  if (auto written = assets::WritePack(output_pack_path, entries, signing_key); !written) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kPackWriteFailed,
        .prim_path = "",
        .message = std::format("WriteError code {}", static_cast<int>(written.error())),
    });
  }

  return CookReport{.mesh_count = mesh_count, .node_count = scene.nodes.size()};
}

}  // namespace augusta::asset_cooking
