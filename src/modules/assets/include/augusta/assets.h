#ifndef AUGUSTA_ASSETS_H_
#define AUGUSTA_ASSETS_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/math.h"

// augusta::assets loads runtime packs (ADR-0018/ADR-0031) and resolves
// their content by pack-relative path. Linked by both the client and
// server (ADR-0006), and by the offline cooker (augusta::asset_cooking,
// ADR-0030) so the pack's byte layout is defined in exactly one place
// rather than duplicated between writer and reader.
//
// This is the walking-skeleton slice (ROADMAP.md M2, issue #46): mesh
// blobs only, no BLAKE3 hash / Ed25519 signature verification yet
// (ADR-0031's trailer step). Pack::Load trusts the header/index as soon
// as they parse - nothing here is safe to expose to untrusted content
// until that trailer lands.
namespace augusta::assets {

// The kind of a pack's index entry (ADR-0031's per-blob type tag). Only
// kMesh is produced or resolved so far.
enum class AssetType : std::uint8_t {
  kMesh,
  kTexture,
  kAudio,
  kCollision,
  kSpawnPoint,
  kHitbox,
};

// A cooked mesh's render-relevant data: positions and a flat triangle
// index buffer. Matches what Cook() currently reads off a UsdGeomMesh -
// no normals/UVs yet (those land with meshoptimizer integration, per
// ADR-0031's fuller conversion mapping).
struct MeshData {
  std::vector<math::Vec3> points;
  std::vector<std::uint32_t> indices;
};

// Sanitizes a USD prim path (e.g. "/Geom/Cube") into the pack-relative
// path ADR-0031 addresses its blob by: the leading '/' is stripped, '/'
// is kept as the path separator.
std::string SanitizePrimPath(std::string_view usd_prim_path);

// Encodes mesh into the pack's mesh-blob byte layout (ADR-0031), for
// augusta::asset_cooking to embed as an AssetEntry's data. The exact
// on-disk layout is otherwise an implementation detail, shared only with
// Pack's own decode path.
std::vector<std::byte> EncodeMeshBlob(const MeshData& mesh);

// One raw blob to be written into a pack, already encoded (e.g. by
// EncodeMeshBlob) and addressed (e.g. by SanitizePrimPath).
struct AssetEntry {
  AssetType type;
  std::string path;
  std::vector<std::byte> data;
};

enum class WriteError {
  // output_path could not be created or written.
  kIoError,
};

// Writes entries into a new pack file at output_path, in ADR-0031's
// header/data/index write order. Overwrites output_path if it already
// exists. No hash/sign trailer yet (ADR-0031's trailer step, deferred).
std::expected<void, WriteError> WritePack(const std::filesystem::path& output_path,
                                          const std::vector<AssetEntry>& entries);

enum class LoadError {
  // path could not be opened or read.
  kIoError,
  // The file's first 4 bytes are not "AUGP".
  kBadMagic,
  // The file's format version is not one this build understands.
  kUnsupportedVersion,
  // The file is shorter than its own header/index claims.
  kTruncated,
};

enum class ResolveError {
  // No index entry has this path.
  kNotFound,
  // An index entry exists at this path, but not as the requested AssetType.
  kTypeMismatch,
  // The blob's bytes don't decode as the type its index entry claims.
  kCorruptBlob,
};

// A loaded pack file (ADR-0031's header/index), read fully into memory -
// packs are always loaded whole, never streamed (nothing can be trusted
// until the whole file has been hashed once that trailer exists).
// Verification (BLAKE3 hash, Ed25519 signature) is not implemented yet:
// Load() trusts the header/index as soon as they parse.
class Pack {
 public:
  // Reads path fully and parses its header/index. Fails closed: any parse
  // problem (bad magic, unsupported version, or offsets/sizes reaching
  // past the file's actual length) is reported here rather than deferred
  // to a later ResolveMesh call.
  static std::expected<Pack, LoadError> Load(const std::filesystem::path& path);

  Pack(const Pack&) = delete;
  Pack& operator=(const Pack&) = delete;
  Pack(Pack&&) noexcept;
  Pack& operator=(Pack&&) noexcept;
  ~Pack();

  // Resolves a mesh by its pack-relative path (see SanitizePrimPath).
  [[nodiscard]] std::expected<MeshData, ResolveError> ResolveMesh(std::string_view path) const;

 private:
  Pack();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_H_
