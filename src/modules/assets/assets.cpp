#include "augusta/assets.h"

#include <blake3.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <mio/mmap.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>

namespace augusta::assets {

namespace {

constexpr std::array<char, 4> kMagic = {'A', 'U', 'G', 'P'};
constexpr std::uint32_t kFormatVersion = 1;
// magic + version(u32) + data_offset(u64) + index_offset(u64) + index_count(u32),
// per ADR-0031's header field list.
constexpr std::uint64_t kHeaderSize =
    kMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t);
constexpr std::size_t kBlake3HashSize = 32;
constexpr std::size_t kEd25519SignatureSize = 64;
// BLAKE3 hash + Ed25519 signature of that hash, ADR-0031's trailer.
constexpr std::uint64_t kTrailerSize = kBlake3HashSize + kEd25519SignatureSize;

constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xFFU;

// Pragmatic v1 sanity limits (review: "Não existem limites razoáveis para
// tamanho de pack, quantidade de entries ou path length"). Chosen to be
// far above any real v1 content while still rejecting a hostile or
// corrupt file long before it could cause a multi-gigabyte allocation.
// Every place that narrows a size_t count into a u32 wire field checks
// the relevant limit below first, which doubles as the narrowing guard:
// all limits are comfortably under UINT32_MAX.
constexpr std::uint32_t kMaxEntries = 1U << 20;
constexpr std::uint32_t kMaxPathLength = 4096;
constexpr std::uint64_t kMaxPackSize = 8ULL * 1024 * 1024 * 1024;
constexpr std::uint32_t kMaxMeshPoints = 16'000'000;
constexpr std::uint32_t kMaxMeshIndices = 48'000'000;
constexpr std::uint32_t kMaxSceneNodes = 1'000'000;
constexpr std::uint32_t kMaxProperties = 256;
// A single BC7-compressed 8K DDS is well under this; comfortably above
// any real v1 texture while still rejecting a hostile/corrupt blob long
// before an oversized allocation.
constexpr std::uint32_t kMaxTextureBytes = 256U * 1024 * 1024;

void EnsureSodiumInitialized() {
  static const bool kInitialized = [] {
    if (sodium_init() < 0) {
      // Unrecoverable startup error (ENGINEERING.md Design Philosophy):
      // the crypto library failed to initialize, so nothing downstream
      // (signing or verification) can be trusted to run at all.
      throw std::runtime_error("augusta::assets: sodium_init() failed");
    }
    return true;
  }();
  (void)kInitialized;
}

void AppendU8(std::vector<std::byte>& buf, std::uint8_t value) { buf.push_back(static_cast<std::byte>(value)); }

void AppendU32(std::vector<std::byte>& buf, std::uint32_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    buf.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

void AppendU64(std::vector<std::byte>& buf, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    buf.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

void AppendF32(std::vector<std::byte>& buf, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(buf, bits);
}

void AppendChars(std::vector<std::byte>& buf, std::string_view chars) {
  for (char chr : chars) {
    buf.push_back(static_cast<std::byte>(static_cast<unsigned char>(chr)));
  }
}

void AppendBytes(std::vector<std::byte>& buf, std::span<const std::byte> data) {
  buf.insert(buf.end(), data.begin(), data.end());
}

// Appends a length-prefixed string, failing if it exceeds kMaxPathLength
// (used for names/paths/property keys/values alike - all the same kind of
// short authored string).
[[nodiscard]] bool AppendString(std::vector<std::byte>& buf, std::string_view str) {
  if (str.size() > kMaxPathLength) {
    return false;
  }
  AppendU32(buf, static_cast<std::uint32_t>(str.size()));
  AppendChars(buf, str);
  return true;
}

[[nodiscard]] bool AppendOptionalPath(std::vector<std::byte>& blob, const std::optional<std::string>& path) {
  return !path || AppendString(blob, *path);
}

// Reads primitive values out of a byte span left-to-right, failing (via
// std::nullopt) the moment a read would run past the end - the one place
// both header and index parsing route every bounds check through.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

  std::optional<std::uint8_t> ReadU8() {
    const auto bytes = ReadBytes(sizeof(std::uint8_t));
    if (!bytes) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>((*bytes)[0]);
  }

  std::optional<std::uint32_t> ReadU32() {
    const auto bytes = ReadBytes(sizeof(std::uint32_t));
    if (!bytes) {
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
      value |= static_cast<std::uint32_t>((*bytes)[i]) << (kBitsPerByte * i);
    }
    return value;
  }

  std::optional<std::uint64_t> ReadU64() {
    const auto bytes = ReadBytes(sizeof(std::uint64_t));
    if (!bytes) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
      value |= static_cast<std::uint64_t>((*bytes)[i]) << (kBitsPerByte * i);
    }
    return value;
  }

  std::optional<float> ReadF32() {
    const auto bits = ReadU32();
    if (!bits) {
      return std::nullopt;
    }
    float value = 0.0F;
    const std::uint32_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  std::optional<std::span<const std::byte>> ReadBytes(std::size_t count) {
    if (count > data_.size() - pos_) {
      return std::nullopt;
    }
    const auto result = data_.subspan(pos_, count);
    pos_ += count;
    return result;
  }

  // Reads a length-prefixed string no longer than kMaxPathLength - the
  // read-side counterpart of AppendString.
  std::optional<std::string> ReadString() {
    const auto length = ReadU32();
    if (!length || *length > kMaxPathLength) {
      return std::nullopt;
    }
    const auto bytes = ReadBytes(*length);
    if (!bytes) {
      return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
};

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
    const auto pos_x = reader.ReadF32();
    const auto pos_y = reader.ReadF32();
    const auto pos_z = reader.ReadF32();
    if (!pos_x || !pos_y || !pos_z) {
      return std::nullopt;
    }
    mesh.points.emplace_back(*pos_x, *pos_y, *pos_z);
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

// Bit flags for a scene node's optional references (EncodeSceneBlob/
// DecodeSceneBlob's wire format - see ADR-0032).
constexpr std::uint8_t kNodeHasMesh = 1U << 0;
constexpr std::uint8_t kNodeHasMaterial = 1U << 1;
constexpr std::uint8_t kNodeHasCollider = 1U << 2;
constexpr std::uint8_t kNodeHasHitbox = 1U << 3;
constexpr std::uint8_t kNodeIsSpawnPoint = 1U << 4;

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

// A node's translation/rotation/scale, read as ten consecutive f32
// fields - factored out of DecodeSceneNode purely to keep that function
// under this codebase's function-size guideline (ADR-0012).
struct DecodedTransform {
  math::Vec3 translation;
  math::Quat rotation;
  math::Vec3 scale;
};

std::optional<DecodedTransform> ReadTransform(ByteReader& reader) {
  const auto translation_x = reader.ReadF32();
  const auto translation_y = reader.ReadF32();
  const auto translation_z = reader.ReadF32();
  const auto rotation_x = reader.ReadF32();
  const auto rotation_y = reader.ReadF32();
  const auto rotation_z = reader.ReadF32();
  const auto rotation_w = reader.ReadF32();
  const auto scale_x = reader.ReadF32();
  const auto scale_y = reader.ReadF32();
  const auto scale_z = reader.ReadF32();
  if (!translation_x || !translation_y || !translation_z || !rotation_x || !rotation_y || !rotation_z || !rotation_w ||
      !scale_x || !scale_y || !scale_z) {
    return std::nullopt;
  }
  return DecodedTransform{
      .translation = math::Vec3(*translation_x, *translation_y, *translation_z),
      .rotation = math::Quat(*rotation_w, *rotation_x, *rotation_y, *rotation_z),
      .scale = math::Vec3(*scale_x, *scale_y, *scale_z),
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

struct IndexEntry {
  AssetType type;
  std::string path;
  std::uint64_t offset;
  std::uint64_t size;
};

struct PackHeader {
  std::uint64_t index_offset;
  std::uint32_t index_count;
};

// hashed_length is the file's length minus the trailer (kTrailerSize) -
// everything the trailer's hash/signature actually covers, and therefore
// the upper bound both the index and every blob it points into must fit
// within.
std::expected<PackHeader, LoadError> ParsePackHeader(std::span<const std::byte> header_bytes,
                                                     std::uint64_t hashed_length) {
  ByteReader header_reader(header_bytes);
  const auto magic = header_reader.ReadBytes(kMagic.size());
  if (!magic || std::memcmp(magic->data(), kMagic.data(), kMagic.size()) != 0) {
    return std::unexpected(LoadError::kBadMagic);
  }

  const auto version = header_reader.ReadU32();
  if (!version) {
    return std::unexpected(LoadError::kTruncated);
  }
  if (*version != kFormatVersion) {
    return std::unexpected(LoadError::kUnsupportedVersion);
  }

  const auto data_offset = header_reader.ReadU64();
  const auto index_offset = header_reader.ReadU64();
  const auto index_count = header_reader.ReadU32();
  if (!data_offset || !index_offset || !index_count) {
    return std::unexpected(LoadError::kTruncated);
  }
  // The data section always starts immediately after the header
  // (WritePack never writes it anywhere else) - a header claiming
  // otherwise is internally inconsistent, not just short.
  if (*data_offset != kHeaderSize) {
    return std::unexpected(LoadError::kTruncated);
  }
  // The index must start at or after the data section and end at or
  // before hashed_length (i.e. entirely within the hashed/signed
  // content, never reaching into the trailer).
  if (*index_offset < kHeaderSize || *index_offset > hashed_length) {
    return std::unexpected(LoadError::kTruncated);
  }
  if (*index_count > kMaxEntries) {
    return std::unexpected(LoadError::kTruncated);
  }

  return PackHeader{.index_offset = *index_offset, .index_count = *index_count};
}

// index_bytes is exactly the file's [index_offset, hashed_length) range.
// data_section_end (== index_offset) is the exclusive upper bound every
// blob's own [offset, offset+size) must fall within - a blob can
// therefore never alias into the header, the index itself, or the
// trailer, only into the data section that precedes the index.
std::expected<std::vector<IndexEntry>, LoadError> ParsePackIndex(std::span<const std::byte> index_bytes,
                                                                 std::uint64_t data_section_end,
                                                                 std::uint32_t index_count) {
  ByteReader index_reader(index_bytes);
  std::vector<IndexEntry> index;
  index.reserve(index_count);
  for (std::uint32_t i = 0; i < index_count; ++i) {
    const auto type = index_reader.ReadU8();
    if (!type || !IsValidAssetType(*type)) {
      return std::unexpected(LoadError::kInvalidIndex);
    }
    auto path = index_reader.ReadString();
    if (!path) {
      return std::unexpected(LoadError::kTruncated);
    }
    const auto offset = index_reader.ReadU64();
    const auto size = index_reader.ReadU64();
    if (!offset || !size) {
      return std::unexpected(LoadError::kTruncated);
    }
    if (*offset < kHeaderSize || *offset > data_section_end || *size > data_section_end - *offset) {
      return std::unexpected(LoadError::kTruncated);
    }

    if (std::ranges::any_of(index, [&](const IndexEntry& existing) { return existing.path == *path; })) {
      return std::unexpected(LoadError::kInvalidIndex);
    }

    index.push_back(
        IndexEntry{.type = static_cast<AssetType>(*type), .path = std::move(*path), .offset = *offset, .size = *size});
  }
  return index;
}

// A zero-copy view into entry's raw bytes within mapping. Safe by
// construction: ParsePackIndex already validated entry's own
// [offset, offset+size) falls entirely within the pack's data section
// (see its own comment), so this can never read outside mapping.
std::span<const std::byte> BlobBytes(const mio::mmap_source& mapping, const IndexEntry& entry) {
  return {reinterpret_cast<const std::byte*>(mapping.data()) + entry.offset, entry.size};
}

// Rejects entries.size()/each path exceeding this module's pragmatic v1
// limits, and any two entries sharing a path (ResolveMesh/ResolveScene
// would be ambiguous about which one they name).
std::expected<void, WriteError> ValidateEntries(const std::vector<AssetEntry>& entries) {
  if (entries.size() > kMaxEntries) {
    return std::unexpected(WriteError::kTooLarge);
  }
  for (const auto& entry : entries) {
    if (entry.path.size() > kMaxPathLength) {
      return std::unexpected(WriteError::kTooLarge);
    }
  }

  std::vector<std::string_view> paths;
  paths.reserve(entries.size());
  for (const auto& entry : entries) {
    paths.push_back(entry.path);
  }
  std::ranges::sort(paths);
  if (std::ranges::adjacent_find(paths) != paths.end()) {
    return std::unexpected(WriteError::kDuplicatePath);
  }
  return {};
}

// The header/data/index sections of a pack (ADR-0031), assembled but not
// yet hashed/signed/written.
struct PackSections {
  std::vector<std::byte> header;
  std::vector<std::byte> data_section;
  std::vector<std::byte> index_section;
};

std::expected<PackSections, WriteError> BuildPackSections(const std::vector<AssetEntry>& entries) {
  PackSections sections;

  std::vector<std::uint64_t> offsets(entries.size());
  std::vector<std::uint64_t> sizes(entries.size());
  std::uint64_t cursor = kHeaderSize;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    offsets[i] = cursor;
    sizes[i] = entries[i].data.size();
    AppendBytes(sections.data_section, entries[i].data);
    cursor += entries[i].data.size();
  }

  for (std::size_t i = 0; i < entries.size(); ++i) {
    AppendU8(sections.index_section, static_cast<std::uint8_t>(entries[i].type));
    if (!AppendString(sections.index_section, entries[i].path)) {
      return std::unexpected(WriteError::kTooLarge);
    }
    AppendU64(sections.index_section, offsets[i]);
    AppendU64(sections.index_section, sizes[i]);
  }
  const std::uint64_t index_offset = cursor;

  AppendChars(sections.header, std::string_view(kMagic.data(), kMagic.size()));
  AppendU32(sections.header, kFormatVersion);
  AppendU64(sections.header, kHeaderSize);
  AppendU64(sections.header, index_offset);
  AppendU32(sections.header, static_cast<std::uint32_t>(entries.size()));

  const std::uint64_t total_size =
      sections.header.size() + sections.data_section.size() + sections.index_section.size() + kTrailerSize;
  if (total_size > kMaxPackSize) {
    return std::unexpected(WriteError::kTooLarge);
  }
  return sections;
}

// A pack's trailer (ADR-0031): BLAKE3 hash of the sections above,
// followed by the Ed25519 signature of that hash. Shared shape for both
// the write side (SignPack, freshly computed) and the read side
// (ReadTrailer, read back off disk to verify against).
struct PackTrailer {
  std::array<std::byte, kBlake3HashSize> hash;
  std::array<std::byte, kEd25519SignatureSize> signature;
};

// ADR-0030's pack -> hash -> sign order: BLAKE3 over everything through
// the end of the index, then Ed25519-sign that hash.
PackTrailer SignPack(const PackSections& sections, const Ed25519PrivateKey& signing_key) {
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, sections.header.data(), sections.header.size());
  blake3_hasher_update(&hasher, sections.data_section.data(), sections.data_section.size());
  blake3_hasher_update(&hasher, sections.index_section.data(), sections.index_section.size());

  PackTrailer trailer;
  blake3_hasher_finalize(&hasher, reinterpret_cast<std::uint8_t*>(trailer.hash.data()), trailer.hash.size());
  crypto_sign_detached(reinterpret_cast<unsigned char*>(trailer.signature.data()), nullptr,
                       reinterpret_cast<const unsigned char*>(trailer.hash.data()), trailer.hash.size(),
                       reinterpret_cast<const unsigned char*>(signing_key.data()));
  return trailer;
}

// Writes sections+trailer to a temporary file next to output_path, then
// renames it into place - a failed write only ever leaves a stray .tmp
// file behind, never a truncated file at output_path (review: "Fazer a
// escrita de packs atomicamente"), since the rename is the only step
// that touches output_path itself, and it's a single filesystem
// operation.
std::expected<void, WriteError> WritePackFile(const std::filesystem::path& output_path, const PackSections& sections,
                                              const PackTrailer& trailer) {
  const std::filesystem::path tmp_path = output_path.string() + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(WriteError::kIoError);
    }
    out.write(reinterpret_cast<const char*>(sections.header.data()),
              static_cast<std::streamsize>(sections.header.size()));
    out.write(reinterpret_cast<const char*>(sections.data_section.data()),
              static_cast<std::streamsize>(sections.data_section.size()));
    out.write(reinterpret_cast<const char*>(sections.index_section.data()),
              static_cast<std::streamsize>(sections.index_section.size()));
    out.write(reinterpret_cast<const char*>(trailer.hash.data()), static_cast<std::streamsize>(trailer.hash.size()));
    out.write(reinterpret_cast<const char*>(trailer.signature.data()),
              static_cast<std::streamsize>(trailer.signature.size()));
    if (!out) {
      std::error_code ignored;
      std::filesystem::remove(tmp_path, ignored);
      return std::unexpected(WriteError::kIoError);
    }
  }

  std::error_code rename_error;
  std::filesystem::rename(tmp_path, output_path, rename_error);
  if (rename_error) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return std::unexpected(WriteError::kIoError);
  }
  return {};
}

// Memory-maps path and validates its size is within [kHeaderSize +
// kTrailerSize, kMaxPackSize] - every subsequent offset/length Pack::Load
// computes is relative to the returned mapping's own size(), not any
// size queried before mapping.
std::expected<mio::mmap_source, LoadError> OpenValidatedMapping(const std::filesystem::path& path) {
  // Checked before mapping anything, same as before mio existed here:
  // keeps mio from ever being asked to map an empty/absent file (its own
  // behavior for that case isn't relied upon). This is deliberately not
  // the source of truth for the bounds re-check below - path could be
  // replaced between this check and make_mmap_source() (e.g. a
  // concurrent redeploy), so the real bounds check is against the
  // mapping's own size(), the size actually mapped.
  std::error_code size_error;
  const auto file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return std::unexpected(LoadError::kIoError);
  }
  if (file_size < kHeaderSize + kTrailerSize || file_size > kMaxPackSize) {
    return std::unexpected(LoadError::kTruncated);
  }

  std::error_code map_error;
  // path.native() (std::wstring on Windows), not path.string(): mio's
  // narrow-string overload assumes UTF-8 and converts via
  // MultiByteToWideChar(CP_UTF8, ...) before calling CreateFileW, but
  // path.string() re-encodes to the system ANSI codepage instead - a
  // pack path with non-ASCII characters would silently fail to open.
  // The wide overload passes straight to CreateFileW with no conversion.
  mio::mmap_source mapping = mio::make_mmap_source(path.native(), map_error);
  if (map_error) {
    return std::unexpected(LoadError::kIoError);
  }

  if (mapping.size() < kHeaderSize + kTrailerSize || mapping.size() > kMaxPackSize) {
    return std::unexpected(LoadError::kTruncated);
  }
  return mapping;
}

// The BLAKE3 hash of mapped's [0, hashed_length) range, and the trailer
// (also BLAKE3 hash + Ed25519 signature, see PackTrailer) immediately
// following it. Unlike the old chunked-ifstream-read version this
// replaced, this can't fail: mapped is already the whole file resident
// (mio, backed by the OS page cache) and hashed_length/kTrailerSize are
// already validated to fit within it (see Pack::Load), so there's no I/O
// left to go wrong here - just pointer arithmetic and a hash.
struct HashAndTrailer {
  std::array<std::byte, kBlake3HashSize> hash;
  PackTrailer trailer;
};

HashAndTrailer HashAndReadTrailer(std::span<const std::byte> mapped, std::uint64_t hashed_length) {
  HashAndTrailer result;
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, mapped.data(), hashed_length);
  blake3_hasher_finalize(&hasher, reinterpret_cast<std::uint8_t*>(result.hash.data()), result.hash.size());

  std::memcpy(result.trailer.hash.data(), mapped.data() + hashed_length, kBlake3HashSize);
  std::memcpy(result.trailer.signature.data(), mapped.data() + hashed_length + kBlake3HashSize, kEd25519SignatureSize);
  return result;
}

}  // namespace

bool IsValidAssetType(std::uint8_t value) {
  switch (static_cast<AssetType>(value)) {
    case AssetType::kMesh:
    case AssetType::kTexture:
    case AssetType::kAudio:
    case AssetType::kCollision:
    case AssetType::kSpawnPoint:
    case AssetType::kHitbox:
    case AssetType::kScene:
      return true;
  }
  return false;
}

bool IsValidTextureFormat(std::uint8_t value) {
  switch (static_cast<TextureFormat>(value)) {
    case TextureFormat::kBC7:
    case TextureFormat::kBC5:
    case TextureFormat::kBC4:
      return true;
  }
  return false;
}

std::string SanitizePrimPath(std::string_view usd_prim_path) {
  if (!usd_prim_path.empty() && usd_prim_path.front() == '/') {
    usd_prim_path.remove_prefix(1);
  }
  return std::string(usd_prim_path);
}

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

Ed25519KeyPair GenerateEd25519KeyPair() {
  EnsureSodiumInitialized();
  Ed25519KeyPair pair;
  crypto_sign_keypair(reinterpret_cast<unsigned char*>(pair.public_key.data()),
                      reinterpret_cast<unsigned char*>(pair.private_key.data()));
  return pair;
}

std::expected<void, WriteError> WritePack(const std::filesystem::path& output_path,
                                          const std::vector<AssetEntry>& entries,
                                          const Ed25519PrivateKey& signing_key) {
  EnsureSodiumInitialized();

  if (auto validated = ValidateEntries(entries); !validated) {
    return std::unexpected(validated.error());
  }

  auto sections = BuildPackSections(entries);
  if (!sections) {
    return std::unexpected(sections.error());
  }

  const PackTrailer trailer = SignPack(*sections, signing_key);
  return WritePackFile(output_path, *sections, trailer);
}

struct Pack::Impl {
  mio::mmap_source mapping;
  std::vector<IndexEntry> index;
};

Pack::Pack() = default;
Pack::Pack(Pack&&) noexcept = default;
Pack& Pack::operator=(Pack&&) noexcept = default;
Pack::~Pack() = default;

std::expected<Pack, LoadError> Pack::Load(const std::filesystem::path& path, const Ed25519PublicKey& public_key) {
  EnsureSodiumInitialized();

  auto mapping = OpenValidatedMapping(path);
  if (!mapping) {
    return std::unexpected(mapping.error());
  }
  const std::uint64_t hashed_length = mapping->size() - kTrailerSize;
  const std::span<const std::byte> mapped(reinterpret_cast<const std::byte*>(mapping->data()), mapping->size());

  auto header = ParsePackHeader(mapped.first(kHeaderSize), hashed_length);
  if (!header) {
    return std::unexpected(header.error());
  }

  const HashAndTrailer hashed = HashAndReadTrailer(mapped, hashed_length);

  // Verified in this order (integrity, then authenticity) purely for a
  // clearer error to the caller - both checks are on untrusted data
  // either way, and neither is trusted until both pass.
  if (hashed.hash != hashed.trailer.hash) {
    return std::unexpected(LoadError::kHashMismatch);
  }
  if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char*>(hashed.trailer.signature.data()),
                                  reinterpret_cast<const unsigned char*>(hashed.trailer.hash.data()),
                                  hashed.trailer.hash.size(),
                                  reinterpret_cast<const unsigned char*>(public_key.data())) != 0) {
    return std::unexpected(LoadError::kSignatureInvalid);
  }

  auto index = ParsePackIndex(mapped.subspan(header->index_offset, hashed_length - header->index_offset),
                              header->index_offset, header->index_count);
  if (!index) {
    return std::unexpected(index.error());
  }

  Pack pack;
  pack.impl_ = std::make_unique<Impl>(Impl{.mapping = std::move(*mapping), .index = std::move(*index)});
  return pack;
}

std::expected<MeshData, ResolveError> Pack::ResolveMesh(std::string_view path) const {
  const auto match = std::find_if(impl_->index.begin(), impl_->index.end(),
                                  [&](const IndexEntry& entry) { return entry.path == path; });
  if (match == impl_->index.end()) {
    return std::unexpected(ResolveError::kNotFound);
  }
  if (match->type != AssetType::kMesh) {
    return std::unexpected(ResolveError::kTypeMismatch);
  }

  auto mesh = DecodeMeshBlob(BlobBytes(impl_->mapping, *match));
  if (!mesh) {
    return std::unexpected(ResolveError::kCorruptBlob);
  }
  return std::move(*mesh);
}

std::expected<SceneData, ResolveError> Pack::ResolveScene(std::string_view path) const {
  const auto match = std::find_if(impl_->index.begin(), impl_->index.end(),
                                  [&](const IndexEntry& entry) { return entry.path == path; });
  if (match == impl_->index.end()) {
    return std::unexpected(ResolveError::kNotFound);
  }
  if (match->type != AssetType::kScene) {
    return std::unexpected(ResolveError::kTypeMismatch);
  }

  auto scene = DecodeSceneBlob(BlobBytes(impl_->mapping, *match));
  if (!scene) {
    return std::unexpected(ResolveError::kCorruptBlob);
  }
  return std::move(*scene);
}

std::expected<TextureData, ResolveError> Pack::ResolveTexture(std::string_view path) const {
  const auto match = std::find_if(impl_->index.begin(), impl_->index.end(),
                                  [&](const IndexEntry& entry) { return entry.path == path; });
  if (match == impl_->index.end()) {
    return std::unexpected(ResolveError::kNotFound);
  }
  if (match->type != AssetType::kTexture) {
    return std::unexpected(ResolveError::kTypeMismatch);
  }

  auto texture = DecodeTextureBlob(BlobBytes(impl_->mapping, *match));
  if (!texture) {
    return std::unexpected(ResolveError::kCorruptBlob);
  }
  return std::move(*texture);
}

}  // namespace augusta::assets
