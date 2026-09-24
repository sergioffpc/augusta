#include "decoder.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "wire_format.h"

// The Decode* half of augusta_assets' blob (de)serialization (ADR-0031/
// ADR-0032/ADR-0007) - see encoder.cpp for the matching Encode* half, and
// wire_format.h for the byte-level primitives (and scene-node flag bits)
// both sides share. Both halves are private (see encoder.h's own comment).
namespace augusta::assets {

namespace {

// Reads path-shaped optional reference fields (mesh/material/collider/
// hitbox) that are only present when their bit is set in flags.
bool ReadOptionalPath(ByteReader& reader, std::uint8_t flags, std::uint8_t bit, std::optional<std::string>& out) {
  if ((flags & bit) == 0) {
    return true;
  }
  auto path = reader.ReadString();
  if (!path) {
    return false;
  }
  out = std::move(*path);
  return true;
}

std::optional<math::Vec3> ReadVec3(ByteReader& reader) {
  const auto x = reader.ReadF32();
  const auto y = reader.ReadF32();
  const auto z = reader.ReadF32();
  if (!x || !y || !z) {
    return std::nullopt;
  }
  return math::Vec3(*x, *y, *z);
}

std::optional<math::Quat> ReadQuat(ByteReader& reader) {
  const auto x = reader.ReadF32();
  const auto y = reader.ReadF32();
  const auto z = reader.ReadF32();
  const auto w = reader.ReadF32();
  if (!x || !y || !z || !w) {
    return std::nullopt;
  }
  return math::Quat(*w, *x, *y, *z);
}

// A node's translation/rotation/scale, read as ten consecutive f32
// fields - factored out of DecodeSceneNode purely to keep that function
// under this codebase's function-size guideline (ADR-0012).
struct DecodedTransform {
  math::Vec3 translation;
  math::Quat rotation;
  math::Vec3 scale;
};

std::optional<DecodedTransform> ReadTransform(ByteReader& reader) {
  const auto translation = ReadVec3(reader);
  const auto rotation = ReadQuat(reader);
  const auto scale = ReadVec3(reader);
  if (!translation || !rotation || !scale) {
    return std::nullopt;
  }
  return DecodedTransform{
      .translation = *translation,
      .rotation = *rotation,
      .scale = *scale,
  };
}

// Reads a node's property_count-prefixed key/value string pairs into
// node.properties - factored out of DecodeSceneNode for the same reason
// as ReadTransform.
bool ReadProperties(ByteReader& reader, SceneNode& node) {
  const auto property_count = reader.ReadU32();
  if (!property_count || *property_count > kMaxProperties) {
    return false;
  }
  node.properties.reserve(*property_count);
  for (std::uint32_t prop_index = 0; prop_index < *property_count; ++prop_index) {
    auto key = reader.ReadString();
    auto value = reader.ReadString();
    if (!key || !value) {
      return false;
    }
    node.properties.emplace_back(std::move(*key), std::move(*value));
  }
  return true;
}

// Decodes one SceneNode, given node_index (this node's own position in
// the array being built) so parent_index can be validated against it -
// see the parent-before-child comment below.
std::optional<SceneNode> DecodeSceneNode(ByteReader& reader, std::uint32_t node_index) {
  SceneNode node;

  auto name = reader.ReadString();
  if (!name) {
    return std::nullopt;
  }
  node.name = std::move(*name);

  const auto parent_index = reader.ReadU32();
  if (!parent_index) {
    return std::nullopt;
  }
  // Nodes are written parent-before-child (ADR-0032), so a node's own
  // parent must already have a lower index - this simultaneously rules
  // out both forward references and cycles.
  if (*parent_index != kSceneNodeNoParent && *parent_index >= node_index) {
    return std::nullopt;
  }
  node.parent_index = *parent_index;

  const auto transform = ReadTransform(reader);
  if (!transform) {
    return std::nullopt;
  }
  node.translation = transform->translation;
  node.rotation = transform->rotation;
  node.scale = transform->scale;

  const auto flags = reader.ReadU8();
  if (!flags) {
    return std::nullopt;
  }
  if (!ReadOptionalPath(reader, *flags, kNodeHasMesh, node.mesh_path) ||
      !ReadOptionalPath(reader, *flags, kNodeHasMaterial, node.material_path) ||
      !ReadOptionalPath(reader, *flags, kNodeHasCollider, node.collider_path) ||
      !ReadOptionalPath(reader, *flags, kNodeHasHitbox, node.hitbox_path)) {
    return std::nullopt;
  }
  node.is_spawn_point = (*flags & kNodeIsSpawnPoint) != 0;

  if (!ReadProperties(reader, node)) {
    return std::nullopt;
  }

  return node;
}

}  // namespace

std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);

  const auto point_count = reader.ReadU32();
  if (!point_count || *point_count > kMaxMeshPoints) {
    return std::nullopt;
  }

  // No reserve() here: point_count/index_count come straight off
  // untrusted blob bytes. They're capped against kMaxMeshPoints/
  // kMaxMeshIndices above/below, but still not yet validated against the
  // blob's actual remaining size - reserving on their word before the
  // per-element bounds checks below get a chance to reject a corrupt or
  // truncated blob would still risk an oversized allocation within that
  // cap.
  MeshData mesh;
  for (std::uint32_t i = 0; i < *point_count; ++i) {
    const auto point = ReadVec3(reader);
    if (!point) {
      return std::nullopt;
    }
    mesh.points.push_back(*point);
  }

  const auto index_count = reader.ReadU32();
  if (!index_count || *index_count > kMaxMeshIndices) {
    return std::nullopt;
  }
  // A triangle list has exactly 3 indices per face (cook.cpp already
  // rejects non-triangular topology before writing, but the pack format
  // itself doesn't otherwise encode that constraint, so it's re-checked
  // here against untrusted bytes rather than assumed from how they were
  // produced).
  if (*index_count % 3 != 0) {
    return std::nullopt;
  }

  for (std::uint32_t i = 0; i < *index_count; ++i) {
    const auto index = reader.ReadU32();
    if (!index) {
      return std::nullopt;
    }
    // Every index must address one of the points just read above -
    // otherwise a downstream renderer would read past the vertex buffer.
    if (*index >= mesh.points.size()) {
      return std::nullopt;
    }
    mesh.indices.push_back(*index);
  }

  return mesh;
}

std::optional<SceneData> DecodeSceneBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);

  const auto node_count = reader.ReadU32();
  if (!node_count || *node_count > kMaxSceneNodes) {
    return std::nullopt;
  }

  // No reserve() here either, for the same reason as DecodeMeshBlob's:
  // node_count is capped against kMaxSceneNodes but not yet validated
  // against the blob's actual remaining size.
  SceneData scene;
  for (std::uint32_t node_index = 0; node_index < *node_count; ++node_index) {
    auto node = DecodeSceneNode(reader, node_index);
    if (!node) {
      return std::nullopt;
    }
    scene.nodes.push_back(std::move(*node));
  }

  return scene;
}

// Texture blob wire format: format byte, then a length-prefixed DDS byte
// string (DirectXTex's own SaveToDDSMemory output, opaque to this
// module beyond its outer length).
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);

  const auto format = reader.ReadU8();
  if (!format || !IsValidTextureFormat(*format)) {
    return std::nullopt;
  }

  const auto dds_size = reader.ReadU32();
  if (!dds_size || *dds_size > kMaxTextureBytes) {
    return std::nullopt;
  }
  const auto dds_bytes = reader.ReadBytes(*dds_size);
  if (!dds_bytes) {
    return std::nullopt;
  }

  return TextureData{
      .dds_bytes = std::vector<std::byte>(dds_bytes->begin(), dds_bytes->end()),
      .format = static_cast<TextureFormat>(*format),
  };
}

// Spawn-point blob wire format: see EncodeSpawnPointBlob.
std::optional<SpawnPointData> DecodeSpawnPointBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto translation = ReadVec3(reader);
  const auto rotation = ReadQuat(reader);
  if (!translation || !rotation) {
    return std::nullopt;
  }
  return SpawnPointData{
      .translation = *translation,
      .rotation = *rotation,
  };
}

// Script blob: the script's text as it is, with no framing and no terminator.
std::optional<std::string> DecodeScriptBlob(std::span<const std::byte> blob) {
  if (blob.size() > kMaxScriptBytes) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char*>(blob.data()), blob.size());
}

// Character-list blob wire format: see EncodeCharactersBlob.
std::optional<std::vector<std::string>> DecodeCharactersBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto count = reader.ReadU32();
  if (!count || *count > kMaxCharacters) {
    return std::nullopt;
  }
  // Unlike DecodeMeshBlob's counts, this one is already capped at a few hundred,
  // so reserving on its word can't cause an oversized allocation.
  std::vector<std::string> characters;
  characters.reserve(*count);
  for (std::uint32_t i = 0; i < *count; ++i) {
    auto character = reader.ReadString();
    if (!character) {
      return std::nullopt;
    }
    characters.push_back(std::move(*character));
  }
  return characters;
}

// Client-pack blob: the client pack's hash, its kPackHashSize bytes and nothing else.
std::optional<PackHash> DecodeClientPackBlob(std::span<const std::byte> blob) {
  if (blob.size() != kPackHashSize) {
    return std::nullopt;
  }
  PackHash hash;
  std::ranges::copy(blob, hash.begin());
  return hash;
}

}  // namespace augusta::assets
