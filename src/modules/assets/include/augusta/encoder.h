#ifndef AUGUSTA_ASSETS_ENCODER_H_
#define AUGUSTA_ASSETS_ENCODER_H_

#include <cstddef>
#include <expected>
#include <vector>

#include "augusta/assets.h"

// Declares augusta_assets' Encode* blob functions (ADR-0031/ADR-0032/
// ADR-0007), implemented in encoder.cpp. Part of the public API -
// augusta::asset_cooking calls these directly to build the AssetEntry
// blobs it writes into a pack - unlike the matching Decode* half
// (decoder.h/decoder.cpp), which stays internal to this module since a
// caller only ever gets typed data back through Pack::Resolve*.
//
// Included from augusta/assets.h itself, after the data types these
// declarations need (MeshData, SceneData, TextureData, SpawnPointData,
// EncodeError) are already defined there, so "#include <augusta/
// assets.h>" alone still gives every consumer everything, same as before
// this file existed. Also includes augusta/assets.h itself so this
// header stays includable standalone - the two inclusions of assets.h
// (whichever order a caller triggers them in) resolve cheaply through
// the ordinary header-guard mechanism.
namespace augusta::assets {

enum class EncodeError {
  // A count or length exceeded what the wire format's fields can hold, or
  // this module's own pragmatic v1 sanity limits (kMaxPathLength,
  // kMaxMeshPoints, kMaxMeshIndices, kMaxSceneNodes, kMaxProperties).
  kTooLarge,
};

// Encodes mesh into the pack's mesh-blob byte layout (ADR-0031), for
// augusta::asset_cooking to embed as an AssetEntry's data. The exact
// on-disk layout is otherwise an implementation detail, shared only with
// Pack's own decode path.
std::expected<std::vector<std::byte>, EncodeError> EncodeMeshBlob(const MeshData& mesh);

// Encodes scene into the pack's scene-blob byte layout (ADR-0032).
std::expected<std::vector<std::byte>, EncodeError> EncodeSceneBlob(const SceneData& scene);

// Encodes texture into the pack's texture-blob byte layout (ADR-0031),
// for augusta::asset_cooking to embed as an AssetEntry's data.
std::expected<std::vector<std::byte>, EncodeError> EncodeTextureBlob(const TextureData& texture);

// Encodes spawn_point into the pack's spawn-point-blob byte layout
// (ADR-0031/ADR-0032). Collision and hitbox blobs need no analogous
// EncodeCollisionBlob/EncodeHitboxBlob - they reuse EncodeMeshBlob
// directly (see MeshData's own comment).
std::expected<std::vector<std::byte>, EncodeError> EncodeSpawnPointBlob(const SpawnPointData& spawn_point);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_ENCODER_H_
