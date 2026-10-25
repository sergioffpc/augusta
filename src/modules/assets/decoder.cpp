#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/pack_decoder.h"
#include "wire_format.h"

// The Decode* half of augusta_assets' blob (de)serialization (ADR-0031/
// ADR-0032/ADR-0007) - see encoder.cpp for the matching Encode* half, and
// wire_format.h for the byte-level primitives (and scene-node flag bits)
// both sides share.
namespace augusta::assets {

namespace {

// The twelve bytes every KTX2 file starts with (the KTX2 specification's file
// identifier).
constexpr std::array<std::byte, 12> kKtx2Identifier = {
    std::byte{0xAB}, std::byte{0x4B}, std::byte{0x54}, std::byte{0x58}, std::byte{0x20}, std::byte{0x32},
    std::byte{0x30}, std::byte{0xBB}, std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A},
};

// The VkFormat values of the three block formats the cooker writes (ADR-0017).
constexpr std::uint32_t kVkFormatBc4UnormBlock = 139;
constexpr std::uint32_t kVkFormatBc5UnormBlock = 141;
constexpr std::uint32_t kVkFormatBc7SrgbBlock = 146;

// The block format a KTX2's VkFormat names, or nothing for any other VkFormat.
std::optional<TextureFormat> ToTextureFormat(std::uint32_t vk_format) {
  switch (vk_format) {
    case kVkFormatBc7SrgbBlock:
      return TextureFormat::kBC7;
    case kVkFormatBc5UnormBlock:
      return TextureFormat::kBC5;
    case kVkFormatBc4UnormBlock:
      return TextureFormat::kBC4;
    default:
      return std::nullopt;
  }
}

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

// Texture blob wire format: see EncodeTextureBlob. Of the KTX2 file itself,
// only its identifier and the VkFormat after it are read.
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);

  const auto ktx2_size = reader.ReadU32();
  if (!ktx2_size || *ktx2_size > kMaxTextureBytes) {
    return std::nullopt;
  }
  const auto ktx2_bytes = reader.ReadBytes(*ktx2_size);
  if (!ktx2_bytes) {
    return std::nullopt;
  }

  ByteReader header(*ktx2_bytes);
  const auto identifier = header.ReadBytes(kKtx2Identifier.size());
  if (!identifier || !std::ranges::equal(*identifier, kKtx2Identifier)) {
    return std::nullopt;
  }
  const auto vk_format = header.ReadU32();
  if (!vk_format) {
    return std::nullopt;
  }
  const auto format = ToTextureFormat(*vk_format);
  if (!format) {
    return std::nullopt;
  }

  return TextureData{
      .ktx2_bytes = std::vector<std::byte>(ktx2_bytes->begin(), ktx2_bytes->end()),
      .format = *format,
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

// Hitbox blob wire format: see EncodeHitboxBlob.
std::optional<HitboxData> DecodeHitboxBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto part = reader.ReadU8();
  if (!part || *part > static_cast<std::uint8_t>(BodyPart::kLimb)) {
    return std::nullopt;
  }
  auto joint = reader.ReadString();
  if (!joint) {
    return std::nullopt;
  }
  auto mesh = DecodeMeshBlob(reader.ReadRest());
  if (!mesh) {
    return std::nullopt;
  }
  return HitboxData{.part = static_cast<BodyPart>(*part), .joint = *std::move(joint), .mesh = *std::move(mesh)};
}

// Skeleton blob wire format: see EncodeSkeletonBlob. Only joints listed parents
// before children are a skeleton, so a parent's index is below its child's.
std::optional<SkeletonData> DecodeSkeletonBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto count = reader.ReadU32();
  if (!count || *count > kMaxJoints) {
    return std::nullopt;
  }
  SkeletonData skeleton;
  // Capped at kMaxJoints, so reserving on its word can't over-allocate.
  skeleton.joints.reserve(*count);
  for (std::uint32_t index = 0; index < *count; ++index) {
    auto name = reader.ReadString();
    const auto parent = reader.ReadU32();
    const auto transform = ReadTransform(reader);
    if (!name || !parent || !transform || (*parent != kJointNoParent && *parent >= index)) {
      return std::nullopt;
    }
    skeleton.joints.push_back(JointData{.name = *std::move(name),
                                        .parent = *parent,
                                        .translation = transform->translation,
                                        .rotation = transform->rotation,
                                        .scale = transform->scale});
  }
  return skeleton;
}

// Eye blob wire format: see EncodeEyeBlob.
std::optional<EyeData> DecodeEyeBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto position = ReadVec3(reader);
  if (!position) {
    return std::nullopt;
  }
  return EyeData{.position = *position};
}

// Rifle blob wire format: see EncodeRifleBlob.
std::optional<RifleData> DecodeRifleBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  RifleData rifle;
  for (RifleMarker* marker : {&rifle.grip, &rifle.muzzle, &rifle.sight, &rifle.fore_end}) {
    const auto translation = ReadVec3(reader);
    const auto rotation = ReadQuat(reader);
    if (!translation || !rotation) {
      return std::nullopt;
    }
    *marker = {.translation = *translation, .rotation = *rotation};
  }
  return rifle;
}

// Weapon socket blob wire format: see EncodeWeaponSocketBlob.
std::optional<WeaponSocketData> DecodeWeaponSocketBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  auto joint = reader.ReadString();
  const auto translation = ReadVec3(reader);
  const auto rotation = ReadQuat(reader);
  auto left_hand = reader.ReadString();
  if (!joint || !translation || !rotation || !left_hand) {
    return std::nullopt;
  }
  return WeaponSocketData{.joint = *std::move(joint),
                          .translation = *translation,
                          .rotation = *rotation,
                          .left_hand = *std::move(left_hand)};
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

// Audio blob wire format: see EncodeAudioBlob. Only whole samples of a width a
// PCM WAV file can hold are a sound.
std::optional<AudioData> DecodeAudioBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto sample_rate = reader.ReadU32();
  const auto bits_per_sample = reader.ReadU8();
  const auto size = reader.ReadU32();
  if (!sample_rate || !bits_per_sample || !size || *size > kMaxAudioBytes) {
    return std::nullopt;
  }
  if (std::ranges::find(kPcmBitsPerSample, *bits_per_sample) == kPcmBitsPerSample.end()) {
    return std::nullopt;
  }
  if (*size % (*bits_per_sample / kBitsPerByte) != 0) {
    return std::nullopt;
  }
  const auto samples = reader.ReadBytes(*size);
  if (!samples) {
    return std::nullopt;
  }
  return AudioData{.sample_rate = *sample_rate,
                   .bits_per_sample = *bits_per_sample,
                   .samples = std::vector<std::byte>(samples->begin(), samples->end())};
}

// Cue-sound blob wire format: see EncodeCueSoundBlob. Only a finite gain and at
// least one variant are a cue's sound.
std::optional<CueSoundData> DecodeCueSoundBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto gain_db = reader.ReadF32();
  const auto count = reader.ReadU32();
  if (!gain_db || !std::isfinite(*gain_db) || !count || *count == 0 || *count > kMaxCueVariants) {
    return std::nullopt;
  }
  CueSoundData sound{.gain_db = *gain_db, .variants = {}};
  // Capped at kMaxCueVariants, so reserving on its word can't over-allocate.
  sound.variants.reserve(*count);
  for (std::uint32_t i = 0; i < *count; ++i) {
    auto variant = reader.ReadString();
    if (!variant) {
      return std::nullopt;
    }
    sound.variants.push_back(std::move(*variant));
  }
  return sound;
}

// Animation-skeleton blob wire format: see EncodeAnimationSkeletonBlob. The
// archive is ozz-animation's to read.
std::optional<std::vector<std::byte>> DecodeAnimationSkeletonBlob(std::span<const std::byte> blob) {
  if (blob.size() > kMaxArchiveBytes) {
    return std::nullopt;
  }
  return std::vector<std::byte>(blob.begin(), blob.end());
}

// Clip blob wire format: see EncodeClipBlob.
std::optional<ClipData> DecodeClipBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto stride_length = reader.ReadF32();
  const auto count = reader.ReadU8();
  if (!stride_length || !IsValidStrideLength(*stride_length) || !count) {
    return std::nullopt;
  }
  ClipData clip{.stride_length = *stride_length};
  clip.foot_contacts.reserve(*count);
  for (std::uint8_t i = 0; i < *count; ++i) {
    const auto foot = reader.ReadU8();
    const auto ratio = reader.ReadF32();
    if (!foot || *foot > static_cast<std::uint8_t>(Foot::kRight) || !ratio || !IsValidFootContactRatio(*ratio)) {
      return std::nullopt;
    }
    clip.foot_contacts.push_back({.foot = static_cast<Foot>(*foot), .ratio = *ratio});
  }
  const auto archive = reader.ReadRest();
  if (archive.size() > kMaxArchiveBytes) {
    return std::nullopt;
  }
  clip.archive.assign(archive.begin(), archive.end());
  return clip;
}

// Stance-transitions blob wire format: see EncodeStanceTransitionsBlob.
std::optional<StanceTransitionsData> DecodeStanceTransitionsBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);
  const auto standing_crouching = reader.ReadF32();
  const auto standing_prone = reader.ReadF32();
  const auto crouching_prone = reader.ReadF32();
  if (!standing_crouching || !standing_prone || !crouching_prone || !reader.ReadRest().empty() ||
      !IsValidTransitionDuration(*standing_crouching) || !IsValidTransitionDuration(*standing_prone) ||
      !IsValidTransitionDuration(*crouching_prone)) {
    return std::nullopt;
  }
  return StanceTransitionsData{
      .standing_crouching = *standing_crouching,
      .standing_prone = *standing_prone,
      .crouching_prone = *crouching_prone,
  };
}

}  // namespace augusta::assets
