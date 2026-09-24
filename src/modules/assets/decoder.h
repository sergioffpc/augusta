#ifndef AUGUSTA_ASSETS_DECODER_H_
#define AUGUSTA_ASSETS_DECODER_H_

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "augusta/assets.h"

// Private (not under include/augusta/, never installed) declarations for
// decoder.cpp's blob decoders - see encoder.h's own comment for why
// Encode*/WritePack are equally private. These are NOT part of the public
// contract: a caller only ever gets typed data back through
// Pack::Resolve* (assets.cpp), never a raw decoder call. This header
// exists purely so assets.cpp can see decoder.cpp's definitions from a
// separate translation unit.
namespace augusta::assets {

std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob);
std::optional<SceneData> DecodeSceneBlob(std::span<const std::byte> blob);
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob);
std::optional<SpawnPointData> DecodeSpawnPointBlob(std::span<const std::byte> blob);
std::optional<EyeData> DecodeEyeBlob(std::span<const std::byte> blob);
std::optional<std::string> DecodeScriptBlob(std::span<const std::byte> blob);
std::optional<std::vector<std::string>> DecodeCharactersBlob(std::span<const std::byte> blob);
std::optional<PackHash> DecodeClientPackBlob(std::span<const std::byte> blob);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_DECODER_H_
