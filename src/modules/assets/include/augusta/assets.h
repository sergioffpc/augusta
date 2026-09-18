#ifndef AUGUSTA_ASSETS_H_
#define AUGUSTA_ASSETS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/math.h"

// augusta::assets loads runtime packs (ADR-0018/ADR-0031) and resolves
// their content by pack-relative path. Linked by both the client and
// server (ADR-0006). The offline cooker (tools/asset-pipeline, ADR-0030)
// is pure Python and does not link this module - it reimplements the same
// wire format independently (validated against this module's WritePack,
// kept private for exactly that reason - see encoder.h) rather than
// sharing code across the language boundary.
//
// Every pack is BLAKE3-hashed and Ed25519-signed (ADR-0030/ADR-0031's
// trailer step): Load() verifies the signature before trusting anything
// else in the file. Load() memory-maps the pack file once (mio) rather
// than copying it into a buffer; the BLAKE3 hash is computed directly off
// that mapping, and every Resolve* call decodes straight out of it too -
// the file's bytes are never read from disk more than once for a Pack's
// lifetime, and no blob is ever copied into a separate in-memory buffer
// before decoding.
namespace augusta::assets {

// The kind of a pack's index entry (ADR-0031's per-blob type tag).
enum class AssetType : std::uint8_t {
  kMesh,
  kTexture,
  kAudio,
  kCollision,
  kSpawnPoint,
  kHitbox,
  kScene,
};

// True if value is one of AssetType's defined enumerators - an index
// entry's type byte is untrusted wire data and must be checked against
// this before being treated as an AssetType.
bool IsValidAssetType(std::uint8_t value);

// A cooked mesh's render-relevant data: positions and a flat triangle
// index buffer. No normals/UVs yet (those land with meshoptimizer
// integration, per ADR-0031's fuller conversion mapping).
//
// Also reused, unchanged, for collision geometry and hitbox shapes
// (ResolveCollision/ResolveHitbox): both are PhysX-authored USD geometry
// read the same points-plus-triangle-index way as a render mesh, and
// ADR-0031 deliberately reuses this shape rather than defining a new one
// for them. Unlike a render mesh, collision/hitbox geometry is never run
// through meshoptimizer's simplify pass (a physics query silently missing
// geometry it should have hit is worse than an unsimplified triangle
// list).
struct MeshData {
  std::vector<math::Vec3> points;
  std::vector<std::uint32_t> indices;
};

// Sentinel parent_index for a SceneNode with no parent (a root node).
inline constexpr std::uint32_t kSceneNodeNoParent = 0xFFFFFFFFU;

// One node in a cooked scene graph (ADR-0032): a name, a transform local
// to its parent (world transform is recomputed by walking the parent
// chain, never stored), and optional references - by pack-relative path,
// same addressing every other blob type uses - to the node's mesh,
// material, collider, and hitbox. is_spawn_point and properties carry the
// rest of ADR-0032's per-node authoring data (the augusta:spawnPoint
// convention, and arbitrary string-keyed properties such as a `script`
// reference, ADR-0022).
struct SceneNode {
  std::string name;
  std::uint32_t parent_index = kSceneNodeNoParent;
  math::Vec3 translation{0.0F, 0.0F, 0.0F};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  math::Vec3 scale{1.0F, 1.0F, 1.0F};
  std::optional<std::string> mesh_path;
  std::optional<std::string> material_path;
  std::optional<std::string> collider_path;
  std::optional<std::string> hitbox_path;
  bool is_spawn_point = false;
  std::vector<std::pair<std::string, std::string>> properties;
};

// A cooked scene graph (ADR-0032): every node in parent-before-child
// order, so a node's parent_index is always less than its own index in
// this vector (or kSceneNodeNoParent for a root).
struct SceneData {
  std::vector<SceneNode> nodes;
};

// The DirectXTex block-compression format a texture blob was compressed
// to (ADR-0017), one-to-one with DXGI_FORMAT_BC7_UNORM/BC5_UNORM/
// BC4_UNORM. Its own enum rather than depending on DXGI_FORMAT directly:
// augusta_assets has no DirectXTex/D3D dependency of its own - only the
// offline cooker's native modules (tools/asset-pipeline/cpp) link DirectXTex.
enum class TextureFormat : std::uint8_t {
  kBC7,
  kBC5,
  kBC4,
};

// True if value is one of TextureFormat's defined enumerators - a texture
// blob's format byte is untrusted wire data and must be checked against
// this before being treated as a TextureFormat.
bool IsValidTextureFormat(std::uint8_t value);

// A cooked texture (ADR-0017/ADR-0031): DirectXTex's own SaveToDDSMemory
// output (DDS header included) plus the block format it was compressed
// to, so a caller can pick a shader/PSO without re-parsing the DDS
// header itself.
struct TextureData {
  std::vector<std::byte> dds_bytes;
  TextureFormat format = TextureFormat::kBC7;
};

// A cooked spawn-point marker (ADR-0032): the point's own local
// translation/rotation, as recorded on the authoring prim's SceneNode.
// Unlike MeshData-shaped blobs, a spawn point has no geometry - it's a
// bare transform, resolvable directly by path without walking the whole
// scene graph.
struct SpawnPointData {
  math::Vec3 translation{0.0F, 0.0F, 0.0F};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
};

// Sanitizes a USD prim path (e.g. "/Geom/Cube") into the pack-relative
// path ADR-0031 addresses its blob by: the leading '/' is stripped, '/'
// is kept as the path separator.
std::string SanitizePrimPath(std::string_view usd_prim_path);

}  // namespace augusta::assets

// The Encode*/WritePack functions (and matching Decode* half) live in
// their own, private header/source pairs (encoder.h/encoder.cpp,
// decoder.h/decoder.cpp - not under include/augusta/, not installed):
// the pack-cooking pipeline (tools/asset-pipeline, ADR-0030) is pure
// Python and reimplements this wire format independently rather than
// linking against it, so nothing outside this module calls Encode*/
// WritePack. They remain as the wire format's canonical reference and as
// tests/assets_test.cpp's round-trip fixture.
namespace augusta::assets {

// One raw blob to be written into a pack, already encoded (e.g. by
// EncodeMeshBlob) and addressed (e.g. by SanitizePrimPath).
struct AssetEntry {
  AssetType type;
  std::string path;
  std::vector<std::byte> data;
};

// A 32-byte Ed25519 public key.
using Ed25519PublicKey = std::array<std::byte, 32>;
// A 64-byte Ed25519 private (secret) key.
using Ed25519PrivateKey = std::array<std::byte, 64>;

struct Ed25519KeyPair {
  Ed25519PublicKey public_key;
  Ed25519PrivateKey private_key;
};

// Generates a new Ed25519 keypair (libsodium's CSPRNG), for tests that
// need a throwaway keypair for a single run (ENGINEERING.md's
// asset-pipeline CI check) without shelling out to a CLI.
Ed25519KeyPair GenerateEd25519KeyPair();

enum class ReadKeyFileError {
  // path could not be opened, or its size didn't match the key type
  // being read (a short/long read - see ReadEd25519PublicKeyFile).
  kIoError,
};

// Reads a raw 32-byte Ed25519 public key from path - the runtime-side
// counterpart to whatever key file a developer generated to sign packs
// (tools/asset-pipeline's keys.py). Every caller of Pack::Load outside a
// test (the client/server executables, ADR-0019) needs this same "read
// the public key I was handed, then load a pack against it" step, so it
// lives here rather than being duplicated per executable.
std::expected<Ed25519PublicKey, ReadKeyFileError> ReadEd25519PublicKeyFile(const std::filesystem::path& path);

enum class LoadError {
  // path could not be opened or read.
  kIoError,
  // The file's first 4 bytes are not "AUGP".
  kBadMagic,
  // The file's format version is not one this build understands.
  kUnsupportedVersion,
  // The file is shorter than its own header/index/trailer claims, or
  // exceeds this module's pragmatic v1 size limits (kMaxEntries,
  // kMaxPathLength, kMaxPackSize).
  kTruncated,
  // An index entry's type byte is not a defined AssetType, or two or more
  // entries share the same path.
  kInvalidIndex,
  // The BLAKE3 hash recomputed from the file's bytes does not match the
  // hash stored in the trailer - the file was corrupted or truncated
  // after signing.
  kHashMismatch,
  // The trailer's Ed25519 signature does not verify against public_key -
  // the file was tampered with, or was signed by a different key.
  kSignatureInvalid,
};

// A human-readable phrase for error (e.g. "failed signature verification")
// - every caller of Pack::Load outside a test needs to turn a LoadError
// into an actionable message for whoever's running the process (the
// client/server executables, ADR-0019), so it lives here rather than
// being duplicated per executable.
std::string_view DescribeLoadError(LoadError error);

enum class ResolveError {
  // No index entry has this path.
  kNotFound,
  // An index entry exists at this path, but not as the requested AssetType.
  kTypeMismatch,
  // The blob's bytes don't decode as the type its index entry claims, or
  // decode to semantically invalid data (e.g. a triangle index at or past
  // the mesh's own point count).
  kCorruptBlob,
};

// A loaded pack file (ADR-0031's header/index/trailer). Load() verifies
// the BLAKE3 hash and Ed25519 signature before parsing the index, and
// only the parsed index is kept in memory afterward - blob bytes
// (mesh/scene data) are read lazily, on demand, by ResolveMesh/
// ResolveScene, each a fresh seek-and-read against the pack file rather
// than a copy out of a permanently resident buffer.
class Pack {
 public:
  // Reads path, verifies its trailer against public_key, and parses its
  // header/index. Fails closed: any parse or verification problem (bad
  // magic, unsupported version, hash/signature mismatch, or offsets/sizes
  // reaching past the file's actual length) is reported here rather than
  // deferred to a later Resolve call.
  static std::expected<Pack, LoadError> Load(const std::filesystem::path& path, const Ed25519PublicKey& public_key);

  Pack(const Pack&) = delete;
  Pack& operator=(const Pack&) = delete;
  Pack(Pack&&) noexcept;
  Pack& operator=(Pack&&) noexcept;
  ~Pack();

  // Resolves a mesh by its pack-relative path (see SanitizePrimPath).
  [[nodiscard]] std::expected<MeshData, ResolveError> ResolveMesh(std::string_view path) const;

  // Resolves a scene graph by its pack-relative path (see
  // SanitizePrimPath).
  [[nodiscard]] std::expected<SceneData, ResolveError> ResolveScene(std::string_view path) const;

  // Resolves a texture by its pack-relative path (see SanitizePrimPath).
  [[nodiscard]] std::expected<TextureData, ResolveError> ResolveTexture(std::string_view path) const;

  // Resolves PhysX-authored collision geometry by its pack-relative path
  // (ADR-0019/ADR-0031). Present in both client and server packs.
  [[nodiscard]] std::expected<MeshData, ResolveError> ResolveCollision(std::string_view path) const;

  // Resolves a hitbox shape by its pack-relative path (ADR-0019/ADR-0031).
  // Present in both client and server packs.
  [[nodiscard]] std::expected<MeshData, ResolveError> ResolveHitbox(std::string_view path) const;

  // Resolves a spawn-point marker by its pack-relative path (ADR-0019/
  // ADR-0032). Present in both client and server packs.
  [[nodiscard]] std::expected<SpawnPointData, ResolveError> ResolveSpawnPoint(std::string_view path) const;

 private:
  Pack();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_H_
