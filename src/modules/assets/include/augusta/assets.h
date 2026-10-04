#ifndef AUGUSTA_ASSETS_H_
#define AUGUSTA_ASSETS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/math.h"

/// \file
/// augusta::assets loads runtime packs (ADR-0018/ADR-0031) and resolves
/// their content by pack-relative path. Linked by both the client and
/// server (ADR-0006). The offline cooker (tools/pack, ADR-0030)
/// is pure Python and does not link this module - it reimplements the same
/// wire format independently (validated against this module's WritePack,
/// kept private for exactly that reason - see encoder.h) rather than
/// sharing code across the language boundary.
///
/// Every pack is BLAKE3-hashed and Ed25519-signed (ADR-0030/ADR-0031's
/// trailer step): Load() verifies the signature before trusting anything
/// else in the file. Load() memory-maps the pack file once (Boost.Interprocess) rather
/// than copying it into a buffer; the BLAKE3 hash is computed directly off
/// that mapping, and every Resolve* call decodes straight out of it too -
/// the file's bytes are never read from disk more than once for a Pack's
/// lifetime, and no blob is ever copied into a separate in-memory buffer
/// before decoding.
namespace augusta::assets {

/// The kind of a pack's index entry (ADR-0031's per-blob type tag).
enum class AssetType : std::uint8_t {
  kMesh,
  kTexture,
  kAudio,
  kCollision,
  kSpawnPoint,
  kHitbox,
  kScene,
  kScript,
  kCharacters,
  kClientPack,
  kEye,
  kSounds,
};

/// True if value is one of AssetType's defined enumerators - an index
/// entry's type byte is untrusted wire data and must be checked against
/// this before being treated as an AssetType.
bool IsValidAssetType(std::uint8_t value);

/// A cooked mesh's render-relevant data: positions and a flat triangle
/// index buffer. No normals/UVs yet (those land with meshoptimizer
/// integration, per ADR-0031's fuller conversion mapping).
///
/// Also reused, unchanged, for collision geometry and hitbox shapes
/// (ResolveCollision/ResolveHitbox): both are PhysX-authored USD geometry
/// read the same points-plus-triangle-index way as a render mesh, and
/// ADR-0031 deliberately reuses this shape rather than defining a new one
/// for them. Unlike a render mesh, collision/hitbox geometry is never run
/// through meshoptimizer's simplify pass (a physics query silently missing
/// geometry it should have hit is worse than an unsimplified triangle
/// list).
struct MeshData {
  std::vector<math::Vec3> points;
  std::vector<std::uint32_t> indices;
};

/// Where on a player a hitbox is (US-11): what a bullet that crosses it hits,
/// which decides its damage (US-12). Numbered as the hitbox blob carries it.
enum class BodyPart : std::uint8_t {
  kHead = 0,
  kTorso = 1,
  kLimb = 2,
};

/// One hitbox (ADR-0040): its geometry and the body part it stands for. A
/// character's is in its own root space, its feet at the origin, standing.
struct HitboxData {
  BodyPart part = BodyPart::kTorso;
  MeshData mesh{};
};

/// part's name as a character stage spells it in augusta:bodyPart: "head",
/// "torso" or "limb".
[[nodiscard]] std::string_view BodyPartName(BodyPart part);

/// The first body part, in BodyPart's order, that none of hitboxes stands for,
/// or nullopt if each has one: a character must be hittable in every part.
[[nodiscard]] std::optional<BodyPart> FirstMissingBodyPart(std::span<const HitboxData> hitboxes);

/// Sentinel parent_index for a SceneNode with no parent (a root node).
inline constexpr std::uint32_t kSceneNodeNoParent = 0xFFFFFFFFU;

/// One node in a cooked scene graph (ADR-0032): a name, a transform local
/// to its parent (world transform is recomputed by walking the parent
/// chain, never stored), and optional references - by pack-relative path,
/// same addressing every other blob type uses - to the node's mesh,
/// material, collider, and hitbox. is_spawn_point and properties carry the
/// rest of ADR-0032's per-node authoring data (the augusta:spawnPoint
/// convention, and arbitrary string-keyed properties such as a `script`
/// reference, ADR-0022).
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

/// A cooked scene graph (ADR-0032): every node in parent-before-child
/// order, so a node's parent_index is always less than its own index in
/// this vector (or kSceneNodeNoParent for a root).
struct SceneData {
  std::vector<SceneNode> nodes;
};

/// Pack-relative path the cooker files a stage's scene graph under.
inline constexpr std::string_view kScenePath = "Scene";

/// The world transform of every node, index for index with scene.nodes
/// (ADR-0032 stores only local transforms). Relies on ADR-0032's ordering, with
/// every parent listed before its children, which Pack::ResolveScene guarantees.
std::vector<math::Mat4> ComputeWorldTransforms(const SceneData& scene);

/// The DirectXTex block-compression format a texture blob was compressed
/// to (ADR-0017), one-to-one with DXGI_FORMAT_BC7_UNORM/BC5_UNORM/
/// BC4_UNORM. Its own enum rather than depending on DXGI_FORMAT directly:
/// augusta_assets has no DirectXTex/D3D dependency of its own - only the
/// offline cooker's native modules (tools/pack/cpp) link DirectXTex.
enum class TextureFormat : std::uint8_t {
  kBC7,
  kBC5,
  kBC4,
};

/// True if value is one of TextureFormat's defined enumerators - a texture
/// blob's format byte is untrusted wire data and must be checked against
/// this before being treated as a TextureFormat.
bool IsValidTextureFormat(std::uint8_t value);

/// A cooked texture (ADR-0017/ADR-0031): DirectXTex's own SaveToDDSMemory
/// output (DDS header included) plus the block format it was compressed
/// to, so a caller can pick a shader/PSO without re-parsing the DDS
/// header itself.
struct TextureData {
  std::vector<std::byte> dds_bytes;
  TextureFormat format = TextureFormat::kBC7;
};

/// A mono PCM sound (ADR-0020): a cue the client plays. samples are as the
/// authored WAV file held them, little-endian: unsigned at 8 bits per sample,
/// signed at 16, 24 or 32.
struct AudioData {
  std::uint32_t sample_rate = 0;
  std::uint8_t bits_per_sample = 0;
  std::vector<std::byte> samples;
};

/// Pack-relative path, in a scenario's client pack, of the sounds folder its cue
/// sounds are addressed under: each at `<sounds folder>/<cue>` (ADR-0031).
inline constexpr std::string_view kSoundsPath = "Sounds";

/// Pack-relative path of the Parameters script (ADR-0039) in a scenario's
/// server pack: `parameters.lua` at the root of the scenario's folder.
inline constexpr std::string_view kParametersScriptPath = "parameters.lua";

/// Pack-relative path of a scenario's character list (ADR-0042), in both of its
/// packs.
inline constexpr std::string_view kCharactersPath = "Characters";

/// Pack-relative path, in a scenario's server pack, of the hash of the client
/// pack cooked with it.
inline constexpr std::string_view kClientPackPath = "ClientPack";

/// The size of a pack's BLAKE3 hash, in bytes.
inline constexpr std::size_t kPackHashSize = 32;

/// A pack's BLAKE3 hash, the one its trailer signs (ADR-0031): names one cook of it.
using PackHash = std::array<std::byte, kPackHashSize>;

/// Most characters a scenario can compose: what bounds the character list a
/// pack is read with (ADR-0042).
inline constexpr std::size_t kMaxCharacters = 255;

/// A cooked spawn-point marker (ADR-0032): the point's own local
/// translation/rotation, as recorded on the authoring prim's SceneNode.
/// Unlike MeshData-shaped blobs, a spawn point has no geometry - it's a
/// bare transform, resolvable directly by path without walking the whole
/// scene graph.
struct SpawnPointData {
  math::Vec3 translation{0.0F, 0.0F, 0.0F};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
};

/// A character's eye (ADR-0040): the point its player sees from - where the
/// client puts the local player's camera and the server fires its Shots from -
/// in the character's own root space: its feet at the origin, the same space
/// its visual mesh is cooked into.
struct EyeData {
  math::Vec3 position{0.0F, 0.0F, 0.0F};
};

/// Pack-relative path of the eye of the character at character_path (its path
/// relative to `authoring/`): its `Character` root prim's `Eye` child
/// (ADR-0040), e.g. "characters/player/Character/Eye".
[[nodiscard]] inline std::string CharacterEyePath(std::string_view character_path) {
  return std::format("{}/Character/Eye", character_path);
}

/// Pack-relative path of the visual mesh of the character at character_path
/// (its path relative to `authoring/`): its `Character` root prim's `Visual`
/// child (ADR-0040), e.g. "characters/player/Character/Visual".
[[nodiscard]] inline std::string CharacterMeshPath(std::string_view character_path) {
  return std::format("{}/Character/Visual", character_path);
}

/// Sanitizes a USD prim path (e.g. "/Geom/Cube") into the pack-relative
/// path ADR-0031 addresses its blob by: the leading '/' is stripped, '/'
/// is kept as the path separator.
std::string SanitizePrimPath(std::string_view usd_prim_path);

}  // namespace augusta::assets

// The Encode*/WritePack functions (and matching Decode* half) live in
// their own, private header/source pairs (encoder.h/encoder.cpp,
// decoder.h/decoder.cpp - not under include/augusta/, not installed):
// the pack-cooking pipeline (tools/pack, ADR-0030) is pure
// Python and reimplements this wire format independently rather than
// linking against it, so nothing outside this module calls Encode*/
// WritePack. They remain as the wire format's canonical reference and as
// tests/assets_test.cpp's round-trip fixture.
namespace augusta::assets {

/// One raw blob to be written into a pack, already encoded (e.g. by
/// EncodeMeshBlob) and addressed (e.g. by SanitizePrimPath).
struct AssetEntry {
  AssetType type;
  std::string path;
  std::vector<std::byte> data;
};

/// The sizes of an Ed25519 key, in bytes.
inline constexpr std::size_t kEd25519PublicKeySize = 32;
inline constexpr std::size_t kEd25519PrivateKeySize = 64;

/// An Ed25519 public key.
using Ed25519PublicKey = std::array<std::byte, kEd25519PublicKeySize>;
/// An Ed25519 private (secret) key.
using Ed25519PrivateKey = std::array<std::byte, kEd25519PrivateKeySize>;

struct Ed25519KeyPair {
  Ed25519PublicKey public_key;
  Ed25519PrivateKey private_key;
};

/// Generates a new Ed25519 keypair (libsodium's CSPRNG), for tests that
/// need a throwaway keypair for a single run (ENGINEERING.md's
/// asset-pipeline CI check) without shelling out to a CLI.
Ed25519KeyPair GenerateEd25519KeyPair();

/// Why ReadEd25519PublicKeyFile could not read a key.
enum class ReadKeyFileError {
  /// path could not be opened, or its size didn't match the key type
  /// being read (a short/long read - see ReadEd25519PublicKeyFile).
  kIoError,
};

/// Reads a raw 32-byte Ed25519 public key from path - the runtime-side
/// counterpart to whatever key file a developer generated to sign packs
/// (tools/pack's keys.py). Every caller of Pack::Load outside a
/// test (the client/server executables, ADR-0019) needs this same "read
/// the public key I was handed, then load a pack against it" step, so it
/// lives here rather than being duplicated per executable.
std::expected<Ed25519PublicKey, ReadKeyFileError> ReadEd25519PublicKeyFile(const std::filesystem::path& path);

/// Why Pack::Load refused a pack.
enum class LoadError {
  /// path could not be opened or read.
  kIoError,
  /// The file's first 4 bytes are not "AUGP".
  kBadMagic,
  /// The file's format version is not one this build understands.
  kUnsupportedVersion,
  /// The file is shorter than its own header/index/trailer claims, or
  /// exceeds this module's pragmatic v1 size limits (kMaxEntries,
  /// kMaxPathLength, kMaxPackSize).
  kTruncated,
  /// An index entry's type byte is not a defined AssetType, or two or more
  /// entries share the same path.
  kInvalidIndex,
  /// The BLAKE3 hash recomputed from the file's bytes does not match the
  /// hash stored in the trailer - the file was corrupted or truncated
  /// after signing.
  kHashMismatch,
  /// The trailer's Ed25519 signature does not verify against public_key -
  /// the file was tampered with, or was signed by a different key.
  kSignatureInvalid,
};

/// A human-readable phrase for error (e.g. "failed signature verification")
/// - every caller of Pack::Load outside a test needs to turn a LoadError
/// into an actionable message for whoever's running the process (the
/// client/server executables, ADR-0019), so it lives here rather than
/// being duplicated per executable.
std::string_view DescribeLoadError(LoadError error);

/// Why a Pack::Resolve* call found no usable asset at a path.
enum class ResolveError {
  /// No index entry has this path.
  kNotFound,
  /// An index entry exists at this path, but not as the requested AssetType.
  kTypeMismatch,
  /// The blob's bytes don't decode as the type its index entry claims, or
  /// decode to semantically invalid data (e.g. a triangle index at or past
  /// the mesh's own point count).
  kCorruptBlob,
};

/// A phrase for error that follows the asset's name; expected_type is what the
/// asset should have been ("mesh", "scene", "collision geometry").
std::string DescribeResolveError(ResolveError error, std::string_view expected_type);

/// A loaded pack file (ADR-0031's header/index/trailer). Load() verifies
/// the BLAKE3 hash and Ed25519 signature before parsing the index, and
/// only the parsed index is kept besides the file's mapping: each Resolve*
/// call decodes its blob on demand, straight out of the mapped file.
class Pack {
 public:
  /// Reads path, verifies its trailer against public_key, and parses its
  /// header/index. Fails closed: any parse or verification problem (bad
  /// magic, unsupported version, hash/signature mismatch, or offsets/sizes
  /// reaching past the file's actual length) is reported here rather than
  /// deferred to a later Resolve call.
  static std::expected<Pack, LoadError> Load(const std::filesystem::path& path, const Ed25519PublicKey& public_key);

  Pack(const Pack&) = delete;
  Pack& operator=(const Pack&) = delete;
  Pack(Pack&&) noexcept;
  Pack& operator=(Pack&&) noexcept;
  ~Pack();

  /// Resolves a mesh by its pack-relative path (see SanitizePrimPath).
  [[nodiscard]] std::expected<MeshData, ResolveError> ResolveMesh(std::string_view path) const;

  /// Resolves a scene graph by its pack-relative path (see
  /// SanitizePrimPath).
  [[nodiscard]] std::expected<SceneData, ResolveError> ResolveScene(std::string_view path) const;

  /// Resolves a texture by its pack-relative path (see SanitizePrimPath).
  [[nodiscard]] std::expected<TextureData, ResolveError> ResolveTexture(std::string_view path) const;

  /// Resolves PhysX-authored collision geometry by its pack-relative path
  /// (ADR-0019/ADR-0031). Present in both client and server packs.
  [[nodiscard]] std::expected<MeshData, ResolveError> ResolveCollision(std::string_view path) const;

  /// Resolves a hitbox by its pack-relative path (ADR-0019/ADR-0031).
  /// Present in both client and server packs.
  [[nodiscard]] std::expected<HitboxData, ResolveError> ResolveHitbox(std::string_view path) const;

  /// Resolves every hitbox of the character at character_path (its path
  /// relative to `authoring/`, e.g. "characters/player", ADR-0040): each one
  /// addressed under it, in path order. Empty if it has none. Present in both
  /// client and server packs.
  [[nodiscard]] std::expected<std::vector<HitboxData>, ResolveError> ResolveHitboxes(
      std::string_view character_path) const;

  /// Resolves a spawn-point marker by its pack-relative path (ADR-0019/
  /// ADR-0032). Present in both client and server packs.
  [[nodiscard]] std::expected<SpawnPointData, ResolveError> ResolveSpawnPoint(std::string_view path) const;

  /// Resolves a character's eye by its pack-relative path (CharacterEyePath,
  /// ADR-0040). Present in both client and server packs.
  [[nodiscard]] std::expected<EyeData, ResolveError> ResolveEye(std::string_view path) const;

  /// Resolves a sound by its pack-relative path (ADR-0020). Present in the client
  /// pack only.
  [[nodiscard]] std::expected<AudioData, ResolveError> ResolveAudio(std::string_view path) const;

  /// Resolves, at kSoundsPath, the sounds folder the scenario's cue sounds are
  /// addressed under, relative to `authoring/` (e.g. "sounds/augusta"). Present in
  /// the client pack only.
  [[nodiscard]] std::expected<std::string, ResolveError> ResolveSoundsPath() const;

  /// Resolves a Lua script's text by its path relative to the scenario's
  /// folder, e.g. kParametersScriptPath (ADR-0031). Present in the server pack
  /// only: a client is sent the values a script decides, never the script.
  [[nodiscard]] std::expected<std::string, ResolveError> ResolveScript(std::string_view path) const;

  /// Resolves the scenario's character list at kCharactersPath: each character's
  /// path relative to `authoring/`, in manifest order (ADR-0042). Present in
  /// both client and server packs.
  [[nodiscard]] std::expected<std::vector<std::string>, ResolveError> ResolveCharacters() const;

  /// Resolves, at kClientPackPath, the Hash() of the client pack cooked with
  /// this one. Present in the server pack only: the server admits only clients
  /// that loaded that pack.
  [[nodiscard]] std::expected<PackHash, ResolveError> ResolveClientPackHash() const;

  /// This pack's hash, as its trailer holds it and Load verified it.
  [[nodiscard]] const PackHash& Hash() const;

  /// The file this pack was loaded from, as Load was given it.
  [[nodiscard]] const std::filesystem::path& Path() const;

 private:
  Pack();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Which step of LoadVerifiedPack failed.
enum class VerifiedPackFailure {
  /// The public key could not be read (ReadEd25519PublicKeyFile).
  kPublicKeyUnreadable,
  /// Pack::Load refused the pack.
  kPackRejected,
};

/// Why LoadVerifiedPack failed.
struct VerifiedPackError {
  VerifiedPackFailure failure;
  /// Why Pack::Load refused the pack; only meaningful for kPackRejected.
  LoadError load_error{};
};

/// Reads the Ed25519 public key at public_key_path and loads the pack at
/// pack_path against it - the step the client and server executables both take
/// before anything else starts (ADR-0018, ADR-0019).
std::expected<Pack, VerifiedPackError> LoadVerifiedPack(const std::filesystem::path& pack_path,
                                                        const std::filesystem::path& public_key_path);

/// What to tell whoever runs the process about why LoadVerifiedPack failed.
std::string DescribeVerifiedPackError(const VerifiedPackError& error, const std::filesystem::path& pack_path,
                                      const std::filesystem::path& public_key_path);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_H_
