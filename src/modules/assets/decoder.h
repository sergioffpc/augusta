#ifndef AUGUSTA_ASSETS_DECODER_H_
#define AUGUSTA_ASSETS_DECODER_H_

#include <optional>
#include <span>

#include "augusta/assets.h"

// Private (not under include/augusta/, never installed) declarations for
// decoder.cpp's blob decoders. Unlike the Encode* functions (part of
// augusta_assets' public API - augusta::asset_cooking calls them
// directly to build AssetEntry blobs), these stay internal: a caller only
// ever gets typed data back through Pack::Resolve* (assets.cpp), never a
// raw decoder call - this header exists purely so assets.cpp can see
// decoder.cpp's definitions from a separate translation unit.
namespace augusta::assets {

std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob);
std::optional<SceneData> DecodeSceneBlob(std::span<const std::byte> blob);
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob);
std::optional<SpawnPointData> DecodeSpawnPointBlob(std::span<const std::byte> blob);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_DECODER_H_
