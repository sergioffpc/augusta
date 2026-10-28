#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <blake3.h>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <sodium.h>

#include "augusta/assets.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/pack_decoder.h"
#include "ed25519.h"
#include "pack_format.h"
#include "wire_format.h"

namespace augusta::assets {

namespace {

// A read-only view of a whole pack file, kept mapped for a Pack's lifetime.
using Mapping = boost::interprocess::mapped_region;

std::span<const std::byte> MappedBytes(const Mapping& mapping) {
  return {static_cast<const std::byte*>(mapping.get_address()), mapping.get_size()};
}

struct PackHeader {
  std::uint32_t version;
  std::uint64_t data_offset;
  std::uint64_t index_offset;
  std::uint32_t index_count;
  std::optional<PackHash> client_pack;
};

// The header's flags byte and the client pack hash after it: the hash, if the
// flags name one.
std::expected<std::optional<PackHash>, LoadErrorCode> ParseClientPack(ByteReader& header_reader) {
  const auto flags = header_reader.ReadU8();
  const auto client_pack_bytes = header_reader.ReadBytes(kPackHashSize);
  if (!flags || !client_pack_bytes) {
    return std::unexpected(LoadErrorCode::kTruncated);
  }
  // No flag but kHeaderHasClientPack, and no hash without it: either would be
  // a header WritePack never writes.
  const bool has_client_pack = (*flags & kHeaderHasClientPack) != 0;
  const bool stray_hash =
      !has_client_pack && std::ranges::any_of(*client_pack_bytes, [](std::byte b) { return b != std::byte{0}; });
  if ((*flags & ~kHeaderHasClientPack) != 0 || stray_hash) {
    return std::unexpected(LoadErrorCode::kTruncated);
  }
  std::optional<PackHash> client_pack;
  if (has_client_pack) {
    client_pack.emplace();
    std::ranges::copy(*client_pack_bytes, client_pack->begin());
  }
  return client_pack;
}

// pack_version is the one the header names, for kUnsupportedVersion alone.
std::unexpected<LoadError> Refuse(LoadErrorCode code, std::uint32_t pack_version = 0) {
  return std::unexpected(LoadError{.code = code, .pack_version = pack_version});
}

// hashed_length is the file's length minus the trailer (kTrailerSize) -
// everything the trailer's hash/signature actually covers, and therefore
// the upper bound both the index and every blob it points into must fit
// within.
std::expected<PackHeader, LoadError> ParsePackHeader(std::span<const std::byte> header_bytes,
                                                     std::uint64_t hashed_length) {
  ByteReader header_reader(header_bytes);
  const auto magic = header_reader.ReadBytes(kPackMagic.size());
  if (!magic || std::memcmp(magic->data(), kPackMagic.data(), kPackMagic.size()) != 0) {
    return Refuse(LoadErrorCode::kBadMagic);
  }

  const auto version = header_reader.ReadU32();
  if (!version) {
    return Refuse(LoadErrorCode::kTruncated);
  }
  if (*version != kFormatVersion) {
    return Refuse(LoadErrorCode::kUnsupportedVersion, *version);
  }

  const auto data_offset = header_reader.ReadU64();
  const auto index_offset = header_reader.ReadU64();
  const auto index_count = header_reader.ReadU32();
  if (!data_offset || !index_offset || !index_count) {
    return Refuse(LoadErrorCode::kTruncated);
  }
  // The data section always starts immediately after the header
  // (WritePack never writes it anywhere else) - a header claiming
  // otherwise is internally inconsistent, not just short.
  if (*data_offset != kHeaderSize) {
    return Refuse(LoadErrorCode::kTruncated);
  }
  // The index must start at or after the data section and end at or
  // before hashed_length (i.e. entirely within the hashed/signed
  // content, never reaching into the trailer).
  if (*index_offset < kHeaderSize || *index_offset > hashed_length) {
    return Refuse(LoadErrorCode::kTruncated);
  }
  if (*index_count > kMaxEntries) {
    return Refuse(LoadErrorCode::kTruncated);
  }

  auto client_pack = ParseClientPack(header_reader);
  if (!client_pack) {
    return Refuse(client_pack.error());
  }

  return PackHeader{.version = *version,
                    .data_offset = *data_offset,
                    .index_offset = *index_offset,
                    .index_count = *index_count,
                    .client_pack = *client_pack};
}

// index_bytes is exactly the file's [index_offset, hashed_length) range.
// data_section_end (== index_offset) is the exclusive upper bound every
// blob's own [offset, offset+size) must fall within - a blob can
// therefore never alias into the header, the index itself, or the
// trailer, only into the data section that precedes the index.
std::expected<std::vector<PackEntry>, LoadErrorCode> ParsePackIndex(std::span<const std::byte> index_bytes,
                                                                    std::uint64_t data_section_end,
                                                                    std::uint32_t index_count) {
  ByteReader index_reader(index_bytes);
  std::vector<PackEntry> index;
  index.reserve(index_count);
  for (std::uint32_t i = 0; i < index_count; ++i) {
    const auto type = index_reader.ReadU8();
    if (!type || !IsValidAssetType(*type)) {
      return std::unexpected(LoadErrorCode::kInvalidIndex);
    }
    auto path = index_reader.ReadString();
    if (!path) {
      return std::unexpected(LoadErrorCode::kTruncated);
    }
    const auto offset = index_reader.ReadU64();
    const auto size = index_reader.ReadU64();
    if (!offset || !size) {
      return std::unexpected(LoadErrorCode::kTruncated);
    }
    if (*offset < kHeaderSize || *offset > data_section_end || *size > data_section_end - *offset) {
      return std::unexpected(LoadErrorCode::kTruncated);
    }

    if (std::ranges::any_of(index, [&](const PackEntry& existing) { return existing.path == *path; })) {
      return std::unexpected(LoadErrorCode::kInvalidIndex);
    }

    index.push_back(
        PackEntry{.type = static_cast<AssetType>(*type), .offset = *offset, .size = *size, .path = std::move(*path)});
  }
  return index;
}

// A zero-copy view into entry's raw bytes within mapping. Safe by
// construction: ParsePackIndex already validated entry's own
// [offset, offset+size) falls entirely within the pack's data section
// (see its own comment), so this can never read outside mapping.
std::span<const std::byte> BlobBytes(const Mapping& mapping, const PackEntry& entry) {
  return MappedBytes(mapping).subspan(entry.offset, entry.size);
}

// The path-lookup-plus-type-check every Pack::Resolve* method starts
// with: no entry at path is kNotFound, an entry of a different AssetType
// is kTypeMismatch - decoding the matched entry's blob bytes is each
// caller's own next step.
std::expected<const PackEntry*, ResolveError> FindIndexEntry(const std::vector<PackEntry>& index, std::string_view path,
                                                             AssetType expected_type) {
  const auto match = std::ranges::find_if(index, [&](const PackEntry& entry) { return entry.path == path; });
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
std::expected<T, ResolveError> ResolveAsset(const std::vector<PackEntry>& index, const Mapping& mapping,
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

// Memory-maps path and validates its size is within [kHeaderSize +
// kTrailerSize, kMaxPackSize] - every subsequent offset/length Pack::Load
// computes is relative to the returned mapping's own size, not any
// size queried before mapping.
std::expected<Mapping, LoadErrorCode> OpenValidatedMapping(const std::filesystem::path& path) {
  // Checked before mapping anything: keeps Boost.Interprocess from ever
  // being asked to map an empty/absent file (its own behavior for that
  // case isn't relied upon). This is deliberately not the source of truth
  // for the bounds re-check below - path could be replaced between this
  // check and the mapping (e.g. a concurrent redeploy), so the real bounds
  // check is against the mapping's own size, the size actually mapped.
  std::error_code size_error;
  const auto file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return std::unexpected(LoadErrorCode::kIoError);
  }
  if (file_size < kHeaderSize + kTrailerSize || file_size > kMaxPackSize) {
    return std::unexpected(LoadErrorCode::kTruncated);
  }

  try {
    // path.c_str() (const wchar_t* on Windows), not its narrow string: the
    // narrow overload takes the system ANSI codepage there, so a pack path
    // with non-ASCII characters would silently fail to open. The wide
    // overload goes straight to CreateFileW. The file_mapping can go
    // out of scope once the region exists: the mapping stays valid without it.
    const boost::interprocess::file_mapping file(path.c_str(), boost::interprocess::read_only);
    Mapping mapping(file, boost::interprocess::read_only);
    if (mapping.get_size() < kHeaderSize + kTrailerSize || mapping.get_size() > kMaxPackSize) {
      return std::unexpected(LoadErrorCode::kTruncated);
    }
    return mapping;
  } catch (const boost::interprocess::interprocess_exception&) {
    return std::unexpected(LoadErrorCode::kIoError);
  }
}

// The BLAKE3 hash of mapped's [0, hashed_length) range, and the trailer that
// follows it. Neither can fail: mapped is already the whole file resident
// (memory-mapped, backed by the OS page cache) and hashed_length/kTrailerSize
// are already validated to fit within it (OpenValidatedMapping), so there's no
// I/O left to go wrong here - just pointer arithmetic and a hash.
PackHash HashContent(std::span<const std::byte> mapped, std::uint64_t hashed_length) {
  PackHash hash;
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, mapped.data(), hashed_length);
  blake3_hasher_finalize(&hasher, reinterpret_cast<std::uint8_t*>(hash.data()), hash.size());
  return hash;
}

PackTrailer ReadTrailer(std::span<const std::byte> mapped, std::uint64_t hashed_length) {
  PackTrailer trailer;
  std::memcpy(trailer.hash.data(), mapped.data() + hashed_length, kPackHashSize);
  std::memcpy(trailer.signature.data(), mapped.data() + hashed_length + kPackHashSize, kEd25519SignatureSize);
  return trailer;
}

// A pack file mapped, its size checked and its header parsed, none of it
// verified: where Pack::Load and ReadPackLayout both start.
struct OpenedPack {
  Mapping mapping;
  PackHeader header;
  // The file's length minus the trailer: what the trailer's hash covers.
  std::uint64_t hashed_length = 0;
};

std::expected<OpenedPack, LoadError> OpenPack(const std::filesystem::path& path) {
  auto mapping = OpenValidatedMapping(path);
  if (!mapping) {
    return Refuse(mapping.error());
  }
  const std::span<const std::byte> mapped = MappedBytes(*mapping);
  const std::uint64_t hashed_length = mapped.size() - kTrailerSize;
  auto header = ParsePackHeader(mapped.first(kHeaderSize), hashed_length);
  if (!header) {
    return std::unexpected(header.error());
  }
  return OpenedPack{.mapping = std::move(*mapping), .header = *header, .hashed_length = hashed_length};
}

// The index of pack: the one index parse both Pack::Load and ReadPackLayout go
// through.
std::expected<std::vector<PackEntry>, LoadErrorCode> ParsePackIndexOf(const OpenedPack& pack) {
  const std::span<const std::byte> mapped = MappedBytes(pack.mapping);
  return ParsePackIndex(mapped.subspan(pack.header.index_offset, pack.hashed_length - pack.header.index_offset),
                        pack.header.index_offset, pack.header.index_count);
}

}  // namespace

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

struct Pack::Impl {
  Mapping mapping;
  std::vector<PackEntry> index;
  PackHash hash;
  std::optional<PackHash> client_pack;
  std::uint32_t version;
  std::filesystem::path path;
};

std::string DescribeLoadError(const LoadError& error) {
  switch (error.code) {
    case LoadErrorCode::kIoError:
      return "could not be opened or read";
    case LoadErrorCode::kBadMagic:
      return "is not an augusta pack file (bad magic)";
    case LoadErrorCode::kUnsupportedVersion:
      return std::format("is pack format {}; this build reads format {}", error.pack_version, kFormatVersion);
    case LoadErrorCode::kTruncated:
      return "is truncated, or exceeds this build's pack size limits";
    case LoadErrorCode::kInvalidIndex:
      return "has a corrupt index";
    case LoadErrorCode::kHashMismatch:
      return "failed its integrity check (content hash mismatch)";
    case LoadErrorCode::kSignatureInvalid:
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

  auto opened = OpenPack(path);
  if (!opened) {
    return std::unexpected(opened.error());
  }
  const std::span<const std::byte> mapped = MappedBytes(opened->mapping);
  const PackTrailer trailer = ReadTrailer(mapped, opened->hashed_length);

  // Verified in this order (integrity, then authenticity) purely for a
  // clearer error to the caller - both checks are on untrusted data
  // either way, and neither is trusted until both pass.
  if (HashContent(mapped, opened->hashed_length) != trailer.hash) {
    return Refuse(LoadErrorCode::kHashMismatch);
  }
  if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char*>(trailer.signature.data()),
                                  reinterpret_cast<const unsigned char*>(trailer.hash.data()), trailer.hash.size(),
                                  reinterpret_cast<const unsigned char*>(public_key.data())) != 0) {
    return Refuse(LoadErrorCode::kSignatureInvalid);
  }

  auto index = ParsePackIndexOf(*opened);
  if (!index) {
    return Refuse(index.error());
  }

  Pack pack;
  pack.impl_ = std::make_unique<Impl>(Impl{.mapping = std::move(opened->mapping),
                                           .index = std::move(*index),
                                           .hash = trailer.hash,
                                           .client_pack = opened->header.client_pack,
                                           .version = opened->header.version,
                                           .path = path});
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

std::expected<HitboxData, ResolveError> Pack::ResolveHitbox(std::string_view path) const {
  return ResolveAsset<HitboxData>(impl_->index, impl_->mapping, path, AssetType::kHitbox, DecodeHitboxBlob);
}

std::expected<std::vector<HitboxData>, ResolveError> Pack::ResolveHitboxes(std::string_view character) const {
  const std::string prefix = std::string(character) + "/";
  std::vector<const PackEntry*> entries;
  for (const PackEntry& entry : impl_->index) {
    if (entry.type == AssetType::kHitbox && entry.path.starts_with(prefix)) {
      entries.push_back(&entry);
    }
  }
  std::ranges::sort(entries, {}, &PackEntry::path);
  std::vector<HitboxData> hitboxes;
  hitboxes.reserve(entries.size());
  for (const PackEntry* entry : entries) {
    auto hitbox = DecodeHitboxBlob(BlobBytes(impl_->mapping, *entry));
    if (!hitbox) {
      return std::unexpected(ResolveError::kCorruptBlob);
    }
    hitboxes.push_back(*std::move(hitbox));
  }
  return hitboxes;
}

std::expected<Skeleton, ResolveError> Pack::ResolveSkeleton(std::string_view character) const {
  auto stored = ResolveAsset<SkeletonData>(impl_->index, impl_->mapping, CharacterSkeletonPath(character),
                                           AssetType::kSkeleton, DecodeSkeletonBlob);
  if (!stored) {
    return std::unexpected(stored.error());
  }
  Skeleton skeleton{.joints = std::move(stored->joints), .rest_transforms = {}};
  skeleton.rest_transforms.reserve(skeleton.joints.size());
  // DecodeSkeletonBlob guarantees each parent is listed before its children.
  for (const JointData& joint : skeleton.joints) {
    const math::Mat4 local = math::ToMat4(joint.translation, joint.rotation, joint.scale);
    skeleton.rest_transforms.push_back(joint.parent == kJointNoParent ? local
                                                                      : skeleton.rest_transforms[joint.parent] * local);
  }
  return skeleton;
}

std::expected<std::vector<std::byte>, ResolveError> Pack::ResolveAnimationSkeleton(std::string_view character) const {
  return ResolveAsset<std::vector<std::byte>>(impl_->index, impl_->mapping, CharacterAnimationSkeletonPath(character),
                                              AssetType::kAnimationSkeleton, DecodeAnimationSkeletonBlob);
}

std::expected<ClipData, ResolveError> Pack::ResolveClip(std::string_view character, std::string_view role) const {
  return ResolveAsset<ClipData>(impl_->index, impl_->mapping, CharacterClipPath(character, role), AssetType::kClip,
                                DecodeClipBlob);
}

std::expected<StanceTransitionsData, ResolveError> Pack::ResolveStanceTransitions(std::string_view character) const {
  return ResolveAsset<StanceTransitionsData>(impl_->index, impl_->mapping, CharacterStanceTransitionsPath(character),
                                             AssetType::kStanceTransitions, DecodeStanceTransitionsBlob);
}

std::expected<SpawnPointData, ResolveError> Pack::ResolveSpawnPoint(std::string_view path) const {
  return ResolveAsset<SpawnPointData>(impl_->index, impl_->mapping, path, AssetType::kSpawnPoint, DecodeSpawnPointBlob);
}

std::expected<EyeData, ResolveError> Pack::ResolveEye(std::string_view path) const {
  return ResolveAsset<EyeData>(impl_->index, impl_->mapping, path, AssetType::kEye, DecodeEyeBlob);
}

std::expected<RifleData, ResolveError> Pack::ResolveRifle() const {
  return ResolveAsset<RifleData>(impl_->index, impl_->mapping, kRiflePath, AssetType::kRifle, DecodeRifleBlob);
}

std::expected<WeaponSocketData, ResolveError> Pack::ResolveWeaponSocket(std::string_view character) const {
  return ResolveAsset<WeaponSocketData>(impl_->index, impl_->mapping, CharacterWeaponSocketPath(character),
                                        AssetType::kWeaponSocket, DecodeWeaponSocketBlob);
}

std::expected<AudioData, ResolveError> Pack::ResolveAudio(std::string_view path) const {
  return ResolveAsset<AudioData>(impl_->index, impl_->mapping, path, AssetType::kAudio, DecodeAudioBlob);
}

std::expected<CueSoundData, ResolveError> Pack::ResolveCueSound(std::string_view path) const {
  return ResolveAsset<CueSoundData>(impl_->index, impl_->mapping, path, AssetType::kCueSound, DecodeCueSoundBlob);
}

std::expected<std::string, ResolveError> Pack::ResolveScript(std::string_view path) const {
  return ResolveAsset<std::string>(impl_->index, impl_->mapping, path, AssetType::kScript, DecodeScriptBlob);
}

std::expected<std::vector<std::string>, ResolveError> Pack::ResolveCharacters() const {
  return ResolveAsset<std::vector<std::string>>(impl_->index, impl_->mapping, kCharactersPath, AssetType::kCharacters,
                                                DecodeCharactersBlob);
}

const std::optional<PackHash>& Pack::ClientPackHash() const { return impl_->client_pack; }

const PackHash& Pack::Hash() const { return impl_->hash; }

const std::filesystem::path& Pack::Path() const { return impl_->path; }

std::span<const PackEntry> Pack::Entries() const { return impl_->index; }

std::uint32_t Pack::Version() const { return impl_->version; }

std::uint64_t Pack::FileSize() const { return impl_->mapping.get_size(); }

std::expected<PackLayout, LoadError> ReadPackLayout(const std::filesystem::path& path) {
  auto opened = OpenPack(path);
  if (!opened) {
    return std::unexpected(opened.error());
  }
  auto index = ParsePackIndexOf(*opened);
  if (!index) {
    return Refuse(index.error());
  }
  const std::span<const std::byte> mapped = MappedBytes(opened->mapping);
  const PackTrailer trailer = ReadTrailer(mapped, opened->hashed_length);
  const PackHeader& header = opened->header;
  return PackLayout{.version = header.version,
                    .file_size = mapped.size(),
                    .data_offset = header.data_offset,
                    .index_offset = header.index_offset,
                    .client_pack = header.client_pack,
                    .entries = std::move(*index),
                    .hash = trailer.hash,
                    .signature = trailer.signature};
}

std::expected<Pack, VerifiedPackError> LoadVerifiedPack(const std::filesystem::path& pack_path,
                                                        const std::filesystem::path& public_key_path) {
  const auto public_key = ReadEd25519PublicKeyFile(public_key_path);
  if (!public_key) {
    return std::unexpected(VerifiedPackError{.failure = VerifiedPackFailure::kPublicKeyUnreadable});
  }
  return Pack::Load(pack_path, *public_key).transform_error([](const LoadError& error) {
    return VerifiedPackError{.failure = VerifiedPackFailure::kPackRejected, .load_error = error};
  });
}

std::string DescribeVerifiedPackError(const VerifiedPackError& error, const std::filesystem::path& pack_path,
                                      const std::filesystem::path& public_key_path) {
  switch (error.failure) {
    case VerifiedPackFailure::kPublicKeyUnreadable:
      return std::format("could not read Ed25519 public key from {}", logging::Utf8(public_key_path));
    case VerifiedPackFailure::kPackRejected:
      return std::format("pack {} {}", logging::Utf8(pack_path), DescribeLoadError(error.load_error));
  }
  return "unknown pack error";
}

}  // namespace augusta::assets
