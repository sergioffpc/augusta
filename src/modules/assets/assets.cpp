#include "augusta/assets.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>

#include <blake3.h>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <sodium.h>

#include "decoder.h"
#include "encoder.h"
#include "wire_format.h"

namespace augusta::assets {

namespace {

constexpr std::array<char, 4> kMagic = {'A', 'U', 'G', 'P'};
constexpr std::uint32_t kFormatVersion = 1;
// magic + version(u32) + data_offset(u64) + index_offset(u64) + index_count(u32),
// per ADR-0031's header field list.
constexpr std::uint64_t kHeaderSize =
    kMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t);
constexpr std::size_t kEd25519SignatureSize = 64;
// BLAKE3 hash + Ed25519 signature of that hash, ADR-0031's trailer.
constexpr std::uint64_t kTrailerSize = kPackHashSize + kEd25519SignatureSize;

// Pragmatic v1 sanity limits - see wire_format.h's own comment on
// kMaxPathLength for the rest of this module's limits (shared with
// encoder.cpp/decoder.cpp); these two are pack-container-only.
constexpr std::uint32_t kMaxEntries = 1U << 20;
constexpr std::uint64_t kMaxPackSize = 8ULL * 1024 * 1024 * 1024;

// A read-only view of a whole pack file, kept mapped for a Pack's lifetime.
using Mapping = boost::interprocess::mapped_region;

std::span<const std::byte> MappedBytes(const Mapping& mapping) {
  return {static_cast<const std::byte*>(mapping.get_address()), mapping.get_size()};
}

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
std::span<const std::byte> BlobBytes(const Mapping& mapping, const IndexEntry& entry) {
  return MappedBytes(mapping).subspan(entry.offset, entry.size);
}

// The path-lookup-plus-type-check every Pack::Resolve* method starts
// with: no entry at path is kNotFound, an entry of a different AssetType
// is kTypeMismatch - decoding the matched entry's blob bytes is each
// caller's own next step.
std::expected<const IndexEntry*, ResolveError> FindIndexEntry(const std::vector<IndexEntry>& index,
                                                              std::string_view path, AssetType expected_type) {
  const auto match = std::ranges::find_if(index, [&](const IndexEntry& entry) { return entry.path == path; });
  if (match == index.end()) {
    return std::unexpected(ResolveError::kNotFound);
  }
  if (match->type != expected_type) {
    return std::unexpected(ResolveError::kTypeMismatch);
  }
  return &*match;
}

// Shared shape behind every Pack::Resolve* method: look up path's index
// entry (rejecting a type mismatch), decode its blob bytes via decode, and
// map a decode failure to kCorruptBlob. Factored out once all six
// Resolve* methods turned out to be this same sequence with nothing but
// the target type/AssetType/decoder differing.
template <typename T>
std::expected<T, ResolveError> ResolveAsset(const std::vector<IndexEntry>& index, const Mapping& mapping,
                                            std::string_view path, AssetType expected_type,
                                            std::optional<T> (*decode)(std::span<const std::byte>)) {
  const auto match = FindIndexEntry(index, path, expected_type);
  if (!match) {
    return std::unexpected(match.error());
  }
  auto value = decode(BlobBytes(mapping, **match));
  if (!value) {
    return std::unexpected(ResolveError::kCorruptBlob);
  }
  return std::move(*value);
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
  ByteWriter data_writer(sections.data_section);
  ByteWriter index_writer(sections.index_section);
  ByteWriter header_writer(sections.header);

  std::vector<std::uint64_t> offsets(entries.size());
  std::vector<std::uint64_t> sizes(entries.size());
  std::uint64_t cursor = kHeaderSize;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    offsets[i] = cursor;
    sizes[i] = entries[i].data.size();
    data_writer.WriteBytes(entries[i].data);
    cursor += entries[i].data.size();
  }

  for (std::size_t i = 0; i < entries.size(); ++i) {
    index_writer.WriteU8(static_cast<std::uint8_t>(entries[i].type));
    if (!index_writer.WriteString(entries[i].path)) {
      return std::unexpected(WriteError::kTooLarge);
    }
    index_writer.WriteU64(offsets[i]);
    index_writer.WriteU64(sizes[i]);
  }
  const std::uint64_t index_offset = cursor;

  header_writer.WriteChars(std::string_view(kMagic.data(), kMagic.size()));
  header_writer.WriteU32(kFormatVersion);
  header_writer.WriteU64(kHeaderSize);
  header_writer.WriteU64(index_offset);
  header_writer.WriteU32(static_cast<std::uint32_t>(entries.size()));

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
  PackHash hash;
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
// computes is relative to the returned mapping's own size, not any
// size queried before mapping.
std::expected<Mapping, LoadError> OpenValidatedMapping(const std::filesystem::path& path) {
  // Checked before mapping anything: keeps Boost.Interprocess from ever
  // being asked to map an empty/absent file (its own behavior for that
  // case isn't relied upon). This is deliberately not the source of truth
  // for the bounds re-check below - path could be replaced between this
  // check and the mapping (e.g. a concurrent redeploy), so the real bounds
  // check is against the mapping's own size, the size actually mapped.
  std::error_code size_error;
  const auto file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return std::unexpected(LoadError::kIoError);
  }
  if (file_size < kHeaderSize + kTrailerSize || file_size > kMaxPackSize) {
    return std::unexpected(LoadError::kTruncated);
  }

  try {
    // path.c_str() (const wchar_t* on Windows), not path.string(): the
    // narrow overload takes the system ANSI codepage there, so a pack path
    // with non-ASCII characters would silently fail to open. The wide
    // overload goes straight to CreateFileW. The file_mapping can go
    // out of scope once the region exists: the mapping stays valid without it.
    const boost::interprocess::file_mapping file(path.c_str(), boost::interprocess::read_only);
    Mapping mapping(file, boost::interprocess::read_only);
    if (mapping.get_size() < kHeaderSize + kTrailerSize || mapping.get_size() > kMaxPackSize) {
      return std::unexpected(LoadError::kTruncated);
    }
    return mapping;
  } catch (const boost::interprocess::interprocess_exception&) {
    return std::unexpected(LoadError::kIoError);
  }
}

// The BLAKE3 hash of mapped's [0, hashed_length) range, and the trailer
// (also BLAKE3 hash + Ed25519 signature, see PackTrailer) immediately
// following it. Unlike the old chunked-ifstream-read version this
// replaced, this can't fail: mapped is already the whole file resident
// (memory-mapped, backed by the OS page cache) and hashed_length/kTrailerSize are
// already validated to fit within it (see Pack::Load), so there's no I/O
// left to go wrong here - just pointer arithmetic and a hash.
struct PackVerificationData {
  PackHash hash;
  PackTrailer trailer;
};

PackVerificationData ComputePackHashAndReadTrailer(std::span<const std::byte> mapped, std::uint64_t hashed_length) {
  PackVerificationData result;
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, mapped.data(), hashed_length);
  blake3_hasher_finalize(&hasher, reinterpret_cast<std::uint8_t*>(result.hash.data()), result.hash.size());

  std::memcpy(result.trailer.hash.data(), mapped.data() + hashed_length, kPackHashSize);
  std::memcpy(result.trailer.signature.data(), mapped.data() + hashed_length + kPackHashSize, kEd25519SignatureSize);
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
    case AssetType::kScript:
    case AssetType::kCharacters:
    case AssetType::kClientPack:
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

Ed25519KeyPair GenerateEd25519KeyPair() {
  EnsureSodiumInitialized();
  Ed25519KeyPair pair;
  crypto_sign_keypair(reinterpret_cast<unsigned char*>(pair.public_key.data()),
                      reinterpret_cast<unsigned char*>(pair.private_key.data()));
  return pair;
}

std::string DescribeResolveError(ResolveError error, std::string_view expected_type) {
  switch (error) {
    case ResolveError::kNotFound:
      return "not found";
    case ResolveError::kTypeMismatch:
      return std::format("is not a {}", expected_type);
    case ResolveError::kCorruptBlob:
      return "is corrupt";
  }
  return "unknown error";
}

// SceneData lists parents before children (ADR-0032), so one forward pass sees
// each parent's transform already done.
std::vector<math::Mat4> ComputeWorldTransforms(const SceneData& scene) {
  std::vector<math::Mat4> world;
  world.reserve(scene.nodes.size());
  for (const SceneNode& node : scene.nodes) {
    const math::Mat4 local = math::ToMat4(node.translation, node.rotation, node.scale);
    world.push_back(node.parent_index == kSceneNodeNoParent ? local : world[node.parent_index] * local);
  }
  return world;
}

std::expected<Ed25519PublicKey, ReadKeyFileError> ReadEd25519PublicKeyFile(const std::filesystem::path& path) {
  std::ifstream key_file(path, std::ios::binary);
  if (!key_file) {
    return std::unexpected(ReadKeyFileError::kIoError);
  }
  Ed25519PublicKey key;
  key_file.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
  if (!key_file || key_file.gcount() != static_cast<std::streamsize>(key.size())) {
    return std::unexpected(ReadKeyFileError::kIoError);
  }
  // A short read (file smaller than 32 bytes) is already caught above by
  // gcount(); this catches the opposite - a file with trailing bytes past
  // the key, which would otherwise go silently unnoticed - by checking
  // there's nothing left to read.
  key_file.peek();
  if (!key_file.eof()) {
    return std::unexpected(ReadKeyFileError::kIoError);
  }
  return key;
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
  Mapping mapping;
  std::vector<IndexEntry> index;
  PackHash hash;
};

std::string_view DescribeLoadError(LoadError error) {
  switch (error) {
    case LoadError::kIoError:
      return "could not be opened or read";
    case LoadError::kBadMagic:
      return "is not an augusta pack file (bad magic)";
    case LoadError::kUnsupportedVersion:
      return "was written by an incompatible pack format version";
    case LoadError::kTruncated:
      return "is truncated, or exceeds this build's pack size limits";
    case LoadError::kInvalidIndex:
      return "has a corrupt index";
    case LoadError::kHashMismatch:
      return "failed its integrity check (content hash mismatch)";
    case LoadError::kSignatureInvalid:
      return "failed signature verification";
  }
  return "failed to load for an unknown reason";
}

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
  const std::span<const std::byte> mapped = MappedBytes(*mapping);
  const std::uint64_t hashed_length = mapped.size() - kTrailerSize;

  auto header = ParsePackHeader(mapped.first(kHeaderSize), hashed_length);
  if (!header) {
    return std::unexpected(header.error());
  }

  const PackVerificationData hashed = ComputePackHashAndReadTrailer(mapped, hashed_length);

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
  pack.impl_ = std::make_unique<Impl>(
      Impl{.mapping = std::move(*mapping), .index = std::move(*index), .hash = hashed.trailer.hash});
  return pack;
}

std::expected<MeshData, ResolveError> Pack::ResolveMesh(std::string_view path) const {
  return ResolveAsset<MeshData>(impl_->index, impl_->mapping, path, AssetType::kMesh, DecodeMeshBlob);
}

std::expected<SceneData, ResolveError> Pack::ResolveScene(std::string_view path) const {
  return ResolveAsset<SceneData>(impl_->index, impl_->mapping, path, AssetType::kScene, DecodeSceneBlob);
}

std::expected<TextureData, ResolveError> Pack::ResolveTexture(std::string_view path) const {
  return ResolveAsset<TextureData>(impl_->index, impl_->mapping, path, AssetType::kTexture, DecodeTextureBlob);
}

std::expected<MeshData, ResolveError> Pack::ResolveCollision(std::string_view path) const {
  return ResolveAsset<MeshData>(impl_->index, impl_->mapping, path, AssetType::kCollision, DecodeMeshBlob);
}

std::expected<MeshData, ResolveError> Pack::ResolveHitbox(std::string_view path) const {
  return ResolveAsset<MeshData>(impl_->index, impl_->mapping, path, AssetType::kHitbox, DecodeMeshBlob);
}

std::expected<SpawnPointData, ResolveError> Pack::ResolveSpawnPoint(std::string_view path) const {
  return ResolveAsset<SpawnPointData>(impl_->index, impl_->mapping, path, AssetType::kSpawnPoint, DecodeSpawnPointBlob);
}

std::expected<std::string, ResolveError> Pack::ResolveScript(std::string_view path) const {
  return ResolveAsset<std::string>(impl_->index, impl_->mapping, path, AssetType::kScript, DecodeScriptBlob);
}

std::expected<std::vector<std::string>, ResolveError> Pack::ResolveCharacters() const {
  return ResolveAsset<std::vector<std::string>>(impl_->index, impl_->mapping, kCharactersPath, AssetType::kCharacters,
                                                DecodeCharactersBlob);
}

std::expected<PackHash, ResolveError> Pack::ResolveClientPackHash() const {
  return ResolveAsset<PackHash>(impl_->index, impl_->mapping, kClientPackPath, AssetType::kClientPack,
                                DecodeClientPackBlob);
}

const PackHash& Pack::Hash() const { return impl_->hash; }

}  // namespace augusta::assets
