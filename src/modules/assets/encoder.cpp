#include "encoder.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wire_format.h"

// The Encode* half of augusta_assets' blob (de)serialization (ADR-0031/
// ADR-0032/ADR-0007) - see decoder.cpp for the matching Decode* half, and
// wire_format.h for the byte-level primitives (and scene-node flag bits)
// both sides share.
namespace augusta::assets {

namespace {

std::expected<void, EncodeError> EncodeSceneNode(ByteWriter& writer, const SceneNode& node) {
  if (!writer.WriteString(node.name)) {
    return std::unexpected(EncodeError::kTooLarge);
  }
  writer.WriteU32(node.parent_index);
  writer.WriteVec3(node.translation);
  writer.WriteQuat(node.rotation);
  writer.WriteVec3(node.scale);

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
  writer.WriteU8(flags);

  if (!writer.WriteOptionalPath(node.mesh_path) || !writer.WriteOptionalPath(node.material_path) ||
      !writer.WriteOptionalPath(node.collider_path) || !writer.WriteOptionalPath(node.hitbox_path)) {
    return std::unexpected(EncodeError::kTooLarge);
  }

  if (node.properties.size() > kMaxProperties) {
    return std::unexpected(EncodeError::kTooLarge);
  }
  writer.WriteU32(static_cast<std::uint32_t>(node.properties.size()));
  for (const auto& [key, value] : node.properties) {
    if (!writer.WriteString(key) || !writer.WriteString(value)) {
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
  ByteWriter writer(blob);
  writer.WriteU32(static_cast<std::uint32_t>(mesh.points.size()));
  for (const auto& point : mesh.points) {
    writer.WriteVec3(point);
  }
  writer.WriteU32(static_cast<std::uint32_t>(mesh.indices.size()));
  for (auto index : mesh.indices) {
    writer.WriteU32(index);
  }
  return blob;
}

std::expected<std::vector<std::byte>, EncodeError> EncodeSceneBlob(const SceneData& scene) {
  if (scene.nodes.size() > kMaxSceneNodes) {
    return std::unexpected(EncodeError::kTooLarge);
  }

  std::vector<std::byte> blob;
  ByteWriter writer(blob);
  writer.WriteU32(static_cast<std::uint32_t>(scene.nodes.size()));
  for (const auto& node : scene.nodes) {
    if (auto encoded = EncodeSceneNode(writer, node); !encoded) {
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
  ByteWriter writer(blob);
  writer.WriteU8(static_cast<std::uint8_t>(texture.format));
  writer.WriteU32(static_cast<std::uint32_t>(texture.dds_bytes.size()));
  writer.WriteBytes(texture.dds_bytes);
  return blob;
}

// Spawn-point blob wire format: translation (3x f32), then rotation as
// x/y/z/w (4x f32) - same field order as EncodeSceneNode's transform, for
// one consistent on-disk quaternion layout across this module.
std::expected<std::vector<std::byte>, EncodeError> EncodeSpawnPointBlob(const SpawnPointData& spawn_point) {
  std::vector<std::byte> blob;
  ByteWriter writer(blob);
  writer.WriteVec3(spawn_point.translation);
  writer.WriteQuat(spawn_point.rotation);
  return blob;
}

std::expected<std::vector<std::byte>, EncodeError> EncodeScriptBlob(std::string_view script) {
  if (script.size() > kMaxScriptBytes) {
    return std::unexpected(EncodeError::kTooLarge);
  }
  const auto* first = reinterpret_cast<const std::byte*>(script.data());
  return std::vector<std::byte>(first, first + script.size());
}

// Character-list blob wire format: a u32 count, then that many length-prefixed
// strings, in manifest order.
std::expected<std::vector<std::byte>, EncodeError> EncodeCharactersBlob(std::span<const std::string> characters) {
  if (characters.size() > kMaxCharacters) {
    return std::unexpected(EncodeError::kTooLarge);
  }
  std::vector<std::byte> blob;
  ByteWriter writer(blob);
  writer.WriteU32(static_cast<std::uint32_t>(characters.size()));
  for (const auto& character : characters) {
    if (!writer.WriteString(character)) {
      return std::unexpected(EncodeError::kTooLarge);
    }
  }
  return blob;
}

}  // namespace augusta::assets
