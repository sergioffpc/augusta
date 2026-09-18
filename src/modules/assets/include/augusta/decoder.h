#ifndef AUGUSTA_ASSETS_DECODER_H_
#define AUGUSTA_ASSETS_DECODER_H_

#include <optional>
#include <span>

#include "augusta/assets.h"

// Declarations for decoder.cpp's blob decoders, colocated under
// include/augusta/ for symmetry with encoder.h - but unlike the Encode*
// functions there (part of augusta_assets' public API, called directly
// by augusta::asset_cooking), these are NOT part of the public contract:
// a caller only ever gets typed data back through Pack::Resolve*
// (assets.cpp), never a raw decoder call. This header exists purely so
// assets.cpp can see decoder.cpp's definitions from a separate
// translation unit - augusta/assets.h deliberately does not include it,
// so "#include <augusta/assets.h>" alone still never exposes Decode*.
namespace augusta::assets {

std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob);
std::optional<SceneData> DecodeSceneBlob(std::span<const std::byte> blob);
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob);
std::optional<SpawnPointData> DecodeSpawnPointBlob(std::span<const std::byte> blob);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_DECODER_H_
