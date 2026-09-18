#ifndef AUGUSTA_ASSETS_ENCODER_H_
#define AUGUSTA_ASSETS_ENCODER_H_

#include <cstddef>
#include <expected>
#include <filesystem>
#include <vector>

#include "augusta/assets.h"

// Private (not under include/augusta/, never installed) declarations of
// augusta_assets' Encode* blob functions and WritePack (ADR-0031/ADR-0032/
// ADR-0007), implemented in encoder.cpp/assets.cpp. The pack-cooking
// pipeline (tools/asset-pipeline) reimplements this same wire format in
// pure Python instead of linking against it (ADR-0030) - these no longer
// have any production caller, and stay only as the canonical reference the
// Python side is validated against, and for augusta_assets' own round-trip
// tests (tests/assets_test.cpp), which gets a private include path to this
// directory for exactly that reason. Mirrors decoder.h's own (pre-existing)
// non-public status.
namespace augusta::assets {

enum class EncodeError {
  // A count or length exceeded what the wire format's fields can hold, or
  // this module's own pragmatic v1 sanity limits (kMaxPathLength,
  // kMaxMeshPoints, kMaxMeshIndices, kMaxSceneNodes, kMaxProperties).
  kTooLarge,
};

// Encodes mesh into the pack's mesh-blob byte layout (ADR-0031). The exact
// on-disk layout is otherwise an implementation detail, shared only with
// Pack's own decode path.
std::expected<std::vector<std::byte>, EncodeError> EncodeMeshBlob(const MeshData& mesh);

// Encodes scene into the pack's scene-blob byte layout (ADR-0032).
std::expected<std::vector<std::byte>, EncodeError> EncodeSceneBlob(const SceneData& scene);

// Encodes texture into the pack's texture-blob byte layout (ADR-0031).
std::expected<std::vector<std::byte>, EncodeError> EncodeTextureBlob(const TextureData& texture);

// Encodes spawn_point into the pack's spawn-point-blob byte layout
// (ADR-0031/ADR-0032). Collision and hitbox blobs need no analogous
// EncodeCollisionBlob/EncodeHitboxBlob - they reuse EncodeMeshBlob
// directly (see MeshData's own comment).
std::expected<std::vector<std::byte>, EncodeError> EncodeSpawnPointBlob(const SpawnPointData& spawn_point);

enum class WriteError {
  // output_path (or its temporary file) could not be created, written,
  // or renamed into place.
  kIoError,
  // Two or more entries share the same path - ResolveMesh/ResolveScene
  // would be ambiguous about which one they name.
  kDuplicatePath,
  // entries.size(), an entry's path, or a blob exceeded this module's
  // pragmatic v1 size limits (kMaxEntries, kMaxPathLength, kMaxPackSize).
  kTooLarge,
};

// Writes entries into a new pack file at output_path, in ADR-0031's
// header/data/index/trailer write order, signing the trailer with
// signing_key. The write is atomic: entries are assembled into a
// temporary file first, which is only renamed into place at output_path
// once fully written.
std::expected<void, WriteError> WritePack(const std::filesystem::path& output_path,
                                          const std::vector<AssetEntry>& entries, const Ed25519PrivateKey& signing_key);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_ENCODER_H_
