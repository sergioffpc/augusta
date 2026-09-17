#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/shader.h>

// DirectXTex.h pulls in <windows.h> (via d3d11.h), whose macros corrupt
// OpenUSD's own template headers (e.g. GfVec4i/GfVec4h) if parsed while
// those macros are active - it must come after every pxr/ include above,
// never before.
#include <DirectXTex.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
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

// augusta:textureFormat: custom string attribute (same authoring
// convention as kSpawnPointAttr/kHitboxAttr above) selecting which BC
// format a UsdUVTexture prim compresses to (ADR-0017/issue #49). Driven
// directly by the authored prim rather than inferred from which
// UsdPreviewSurface input it feeds (diffuseColor vs. normal vs.
// roughness, say) - that would need walking the full shading-network
// connection graph via UsdShadeConnectableAPI, which no fixture here
// exercises yet. Defaults to BC7 (the common sRGB color-texture case)
// when absent or unrecognized.
constexpr auto kTextureFormatAttr = "augusta:textureFormat";
constexpr auto kUsdUVTextureShaderId = "UsdUVTexture";

assets::TextureFormat ReadTextureFormatAttr(const pxr::UsdPrim& prim) {
  const pxr::UsdAttribute attr = prim.GetAttribute(pxr::TfToken(kTextureFormatAttr));
  std::string value;
  if (attr && attr.Get(&value)) {
    if (value == "bc5") {
      return assets::TextureFormat::kBC5;
    }
    if (value == "bc4") {
      return assets::TextureFormat::kBC4;
    }
  }
  return assets::TextureFormat::kBC7;
}

DXGI_FORMAT ToDxgiFormat(assets::TextureFormat format) {
  switch (format) {
    case assets::TextureFormat::kBC5:
      return DXGI_FORMAT_BC5_UNORM;
    case assets::TextureFormat::kBC4:
      return DXGI_FORMAT_BC4_UNORM;
    case assets::TextureFormat::kBC7:
      return DXGI_FORMAT_BC7_UNORM;
  }
  return DXGI_FORMAT_BC7_UNORM;
}

// DirectXTex's WIC-backed loaders (LoadFromWICFile) need COM initialized
// on the calling thread - they don't do this themselves. thread_local
// (COM apartment state is per-thread, unlike EnsureUsdPluginPathConfigured's
// process-wide plugin registry) so a multi-threaded caller doesn't skip
// this on a thread that never ran it. Returns false if COM is unusable on
// this thread (e.g. RPC_E_CHANGED_MODE, because something else already
// initialized it with an incompatible apartment model) - the caller must
// check this rather than let every later LoadFromWICFile call fail with a
// generic, misleading "failed to load" error.
bool EnsureComInitialized() {
  thread_local const bool kInitialized = [] {
    // S_FALSE (already initialized on this thread, e.g. by USD's own COM
    // use) is success too - only a hard FAILED() return means WIC calls
    // on this thread won't work.
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return SUCCEEDED(init_result) || init_result == S_FALSE;
  }();
  return kInitialized;
}

// Resolves a UsdUVTexture prim's inputs:file into an absolute filesystem
// path. SdfAssetPath's own resolved path (populated by USD's asset
// resolver at attribute-read time) is preferred; a stage referencing a
// texture that exists on disk relative to the stage's own layer always
// has one. The stage_path fallback only matters for a value the resolver
// left unresolved (e.g. a bare relative string with no matching file at
// read time).
std::filesystem::path ResolveTextureFilePath(const pxr::SdfAssetPath& asset_path,
                                             const std::filesystem::path& stage_path) {
  std::filesystem::path resolved = asset_path.GetResolvedPath();
  if (resolved.empty()) {
    resolved = asset_path.GetAssetPath();
  }
  if (resolved.is_relative()) {
    resolved = stage_path.parent_path() / resolved;
  }
  return resolved;
}

// Loads a UsdUVTexture prim's referenced image via DirectXTex/WIC and
// block-compresses it (ADR-0017) to the format kTextureFormatAttr
// selects, returning the result as a pack-ready TextureData.
std::expected<assets::TextureData, CookErrorDetail> CookTexture(const pxr::UsdPrim& prim, const std::string& prim_path,
                                                                const std::filesystem::path& stage_path) {
  const pxr::UsdShadeShader shader(prim);
  const pxr::UsdShadeInput file_input = shader.GetInput(pxr::TfToken("file"));
  pxr::SdfAssetPath asset_path;
  if (!file_input || !file_input.Get(&asset_path)) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kMissingTextureFile,
        .prim_path = prim_path,
        .message = "UsdUVTexture prim has no inputs:file",
    });
  }

  const std::filesystem::path texture_path = ResolveTextureFilePath(asset_path, stage_path);

  if (!EnsureComInitialized()) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kTextureLoadFailed,
        .prim_path = prim_path,
        .message = "COM could not be initialized on this thread (CoInitializeEx failed) - WIC texture loading "
                   "is unavailable",
    });
  }

  DirectX::TexMetadata metadata;
  DirectX::ScratchImage image;
  if (FAILED(DirectX::LoadFromWICFile(texture_path.wstring().c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image))) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kTextureLoadFailed,
        .prim_path = prim_path,
        .message = std::format("failed to load texture image {}", texture_path.string()),
    });
  }

  const assets::TextureFormat format = ReadTextureFormatAttr(prim);
  DirectX::ScratchImage compressed;
  if (FAILED(DirectX::Compress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), ToDxgiFormat(format),
                               DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed))) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kTextureCompressFailed,
        .prim_path = prim_path,
        .message = std::format("failed to BC-compress texture image {}", texture_path.string()),
    });
  }

  DirectX::Blob dds_blob;
  if (FAILED(DirectX::SaveToDDSMemory(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
                                      DirectX::DDS_FLAGS_NONE, dds_blob))) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kTextureCompressFailed,
        .prim_path = prim_path,
        .message = std::format("failed to encode DDS for texture image {}", texture_path.string()),
    });
  }

  const auto* dds_bytes = reinterpret_cast<const std::byte*>(dds_blob.GetBufferPointer());
  return assets::TextureData{
      .dds_bytes = std::vector<std::byte>(dds_bytes, dds_bytes + dds_blob.GetBufferSize()),
      .format = format,
  };
}

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

std::size_t CountEntriesOfType(const std::vector<assets::AssetEntry>& entries, assets::AssetType type) {
  return static_cast<std::size_t>(std::count_if(
      entries.begin(), entries.end(), [type](const assets::AssetEntry& entry) { return entry.type == type; }));
}

// UsdShadeShader prims aren't part of the scene graph's spatial hierarchy
// (they still get the generic node BuildNode produces, same as any other
// non-mesh prim, via Cook()'s own call to BuildNode) - a UsdUVTexture one
// is additionally cooked into its own texture blob (ADR-0017/issue #49)
// and appended to entries. A no-op for any other prim. prim_path is
// BuildNode's already-sanitized node.name for the same prim (Cook() reuses
// it rather than sanitizing prim's path a second time).
std::expected<void, CookErrorDetail> MaybeCookTexturePrim(const pxr::UsdPrim& prim, const std::string& prim_path,
                                                          const std::filesystem::path& stage_path,
                                                          std::vector<assets::AssetEntry>& entries) {
  pxr::TfToken shader_id;
  if (!prim.IsA<pxr::UsdShadeShader>() || !pxr::UsdShadeShader(prim).GetShaderId(&shader_id) ||
      shader_id != kUsdUVTextureShaderId) {
    return {};
  }

  auto texture_data = CookTexture(prim, prim_path, stage_path);
  if (!texture_data) {
    return std::unexpected(texture_data.error());
  }
  auto texture_blob = assets::EncodeTextureBlob(*texture_data);
  if (!texture_blob) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kContentTooLarge,
        .prim_path = prim_path,
        .message = "texture exceeds pack size limits",
    });
  }
  entries.push_back(
      assets::AssetEntry{.type = assets::AssetType::kTexture, .path = prim_path, .data = std::move(*texture_blob)});
  return {};
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
    // node->name is prim's sanitized path (BuildNode) - reused below
    // instead of sanitizing it again, captured before the move.
    const std::string prim_path = node->name;
    node_index_of.emplace(prim.GetPath(), static_cast<std::uint32_t>(scene.nodes.size()));
    scene.nodes.push_back(std::move(*node));

    if (auto texture = MaybeCookTexturePrim(prim, prim_path, stage_path, entries); !texture) {
      return std::unexpected(texture.error());
    }
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

  const std::size_t mesh_count = CountEntriesOfType(entries, assets::AssetType::kMesh);
  const std::size_t texture_count = CountEntriesOfType(entries, assets::AssetType::kTexture);

  if (auto written = assets::WritePack(output_pack_path, entries, signing_key); !written) {
    return std::unexpected(CookErrorDetail{
        .code = CookError::kPackWriteFailed,
        .prim_path = "",
        .message = std::format("WriteError code {}", static_cast<int>(written.error())),
    });
  }

  return CookReport{.mesh_count = mesh_count, .texture_count = texture_count, .node_count = scene.nodes.size()};
}

}  // namespace augusta::asset_cooking
