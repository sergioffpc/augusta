#include "augusta/encoder.h"

#include <cstdint>
#include <expected>
#include <vector>

#include "wire_format.h"

// The Encode* half of augusta_assets' blob (de)serialization (ADR-0031/
// ADR-0032/ADR-0007) - see decoder.cpp for the matching Decode* half, and
// wire_format.h for the byte-level primitives (and scene-node flag bits)
// both sides share.
namespace augusta::assets {

namespace {

std::expected<void, EncodeError> EncodeSceneNode(std::vector<std::byte>& blob, const SceneNode& node) {
  if (!AppendString(blob, node.name)) {
    return std::unexpected(EncodeError::kTooLarge);
  }
  AppendU32(blob, node.parent_index);
  AppendF32(blob, node.translation.x);
  AppendF32(blob, node.translation.y);
  AppendF32(blob, node.translation.z);
  AppendF32(blob, node.rotation.x);
  AppendF32(blob, node.rotation.y);
  AppendF32(blob, node.rotation.z);
  AppendF32(blob, node.rotation.w);
  AppendF32(blob, node.scale.x);
  AppendF32(blob, node.scale.y);
  AppendF32(blob, node.scale.z);

  std::uint8_t flags = 0;
  if (node.mesh_path) {
    flags |= kNodeHasMesh;
  }
  if (node.material_path) {
    flags |= kNodeHasMaterial;
  }
  if (node.collider_path) {
    flags |= kNodeHasCollider;
  }
  if (node.hitbox_path) {
    flags |= kNodeHasHitbox;
  }
  if (node.is_spawn_point) {
    flags |= kNodeIsSpawnPoint;
  }
  AppendU8(blob, flags);

  if (!AppendOptionalPath(blob, node.mesh_path) || !AppendOptionalPath(blob, node.material_path) ||
      !AppendOptionalPath(blob, node.collider_path) || !AppendOptionalPath(blob, node.hitbox_path)) {
    return std::unexpected(EncodeError::kTooLarge);
  }

  if (node.properties.size() > kMaxProperties) {
    return std::unexpected(EncodeError::kTooLarge);
  }
  AppendU32(blob, static_cast<std::uint32_t>(node.properties.size()));
  for (const auto& [key, value] : node.properties) {
    if (!AppendString(blob, key) || !AppendString(blob, value)) {
      return std::unexpected(EncodeError::kTooLarge);
    }
  }
  return {};
}

}  // namespace

std::expected<std::vector<std::byte>, EncodeError> EncodeMeshBlob(const MeshData& mesh) {
  if (mesh.points.size() > kMaxMeshPoints || mesh.indices.size() > kMaxMeshIndices) {
    return std::unexpected(EncodeError::kTooLarge);
  }

  std::vector<std::byte> blob;
  AppendU32(blob, static_cast<std::uint32_t>(mesh.points.size()));
  for (const auto& point : mesh.points) {
    AppendF32(blob, point.x);
    AppendF32(blob, point.y);
    AppendF32(blob, point.z);
  }
  AppendU32(blob, static_cast<std::uint32_t>(mesh.indices.size()));
  for (auto index : mesh.indices) {
    AppendU32(blob, index);
  }
  return blob;
}

std::expected<std::vector<std::byte>, EncodeError> EncodeSceneBlob(const SceneData& scene) {
  if (scene.nodes.size() > kMaxSceneNodes) {
    return std::unexpected(EncodeError::kTooLarge);
  }

  std::vector<std::byte> blob;
  AppendU32(blob, static_cast<std::uint32_t>(scene.nodes.size()));
  for (const auto& node : scene.nodes) {
    if (auto encoded = EncodeSceneNode(blob, node); !encoded) {
      return std::unexpected(encoded.error());
    }
  }
  return blob;
}

std::expected<std::vector<std::byte>, EncodeError> EncodeTextureBlob(const TextureData& texture) {
  if (texture.dds_bytes.size() > kMaxTextureBytes) {
    return std::unexpected(EncodeError::kTooLarge);
  }

  std::vector<std::byte> blob;
  AppendU8(blob, static_cast<std::uint8_t>(texture.format));
  AppendU32(blob, static_cast<std::uint32_t>(texture.dds_bytes.size()));
  AppendBytes(blob, texture.dds_bytes);
  return blob;
}

// Spawn-point blob wire format: translation (3x f32), then rotation as
// x/y/z/w (4x f32) - same field order as EncodeSceneNode's transform, for
// one consistent on-disk quaternion layout across this module.
std::expected<std::vector<std::byte>, EncodeError> EncodeSpawnPointBlob(const SpawnPointData& spawn_point) {
  std::vector<std::byte> blob;
  AppendF32(blob, spawn_point.translation.x);
  AppendF32(blob, spawn_point.translation.y);
  AppendF32(blob, spawn_point.translation.z);
  AppendF32(blob, spawn_point.rotation.x);
  AppendF32(blob, spawn_point.rotation.y);
  AppendF32(blob, spawn_point.rotation.z);
  AppendF32(blob, spawn_point.rotation.w);
  return blob;
}

}  // namespace augusta::assets
