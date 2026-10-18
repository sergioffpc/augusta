#ifndef AUGUSTA_PACK_DECODER_H_
#define AUGUSTA_PACK_DECODER_H_

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "augusta/assets.h"

/// \file
/// augusta_assets' pack decoder: the Decode* blob functions (ADR-0031/
/// ADR-0032/ADR-0007), implemented in decoder.cpp. The read half of the pack's
/// wire format, mirroring augusta/pack_encoder.h's Encode*; Pack::Resolve*
/// (augusta/assets.h) decodes through them.
namespace augusta::assets {

/// Each decodes one blob type's byte layout, the inverse of
/// augusta/pack_encoder.h's Encode*, or is nullopt if blob is not a valid blob
/// of that type.
std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob);
std::optional<SceneData> DecodeSceneBlob(std::span<const std::byte> blob);
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob);
std::optional<SpawnPointData> DecodeSpawnPointBlob(std::span<const std::byte> blob);
std::optional<HitboxData> DecodeHitboxBlob(std::span<const std::byte> blob);
std::optional<EyeData> DecodeEyeBlob(std::span<const std::byte> blob);
std::optional<std::string> DecodeScriptBlob(std::span<const std::byte> blob);
std::optional<std::vector<std::string>> DecodeCharactersBlob(std::span<const std::byte> blob);
std::optional<AudioData> DecodeAudioBlob(std::span<const std::byte> blob);
std::optional<std::string> DecodeSoundsBlob(std::span<const std::byte> blob);

}  // namespace augusta::assets

#endif  // AUGUSTA_PACK_DECODER_H_
