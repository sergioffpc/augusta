#ifndef AUGUSTA_ASSETS_PACK_DECODER_H_
#define AUGUSTA_ASSETS_PACK_DECODER_H_

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "augusta/assets.h"

/// \file
/// The read half of the pack's wire format (ADR-0031/ADR-0032/ADR-0007): it
/// turns a blob's bytes back into the content augusta/pack_encoder.h encoded,
/// and refuses any that are not a valid blob of their type, since a blob is
/// untrusted until it decodes. Pack's Resolve* (augusta/assets.h) decode
/// through it, for the client and server; finding a blob in its pack, and
/// verifying the pack, are Pack's.
namespace augusta::assets {

/// Each decodes one blob type's byte layout, the inverse of
/// augusta/pack_encoder.h's Encode*, or is nullopt if blob is not a valid blob
/// of that type.
std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob);
std::optional<SceneData> DecodeSceneBlob(std::span<const std::byte> blob);
std::optional<TextureData> DecodeTextureBlob(std::span<const std::byte> blob);
std::optional<SpawnPointData> DecodeSpawnPointBlob(std::span<const std::byte> blob);
std::optional<HitboxData> DecodeHitboxBlob(std::span<const std::byte> blob);
std::optional<SkeletonData> DecodeSkeletonBlob(std::span<const std::byte> blob);
std::optional<EyeData> DecodeEyeBlob(std::span<const std::byte> blob);
std::optional<RifleData> DecodeRifleBlob(std::span<const std::byte> blob);
std::optional<WeaponSocketData> DecodeWeaponSocketBlob(std::span<const std::byte> blob);
std::optional<std::string> DecodeScriptBlob(std::span<const std::byte> blob);
std::optional<std::vector<std::string>> DecodeCharactersBlob(std::span<const std::byte> blob);
std::optional<AudioData> DecodeAudioBlob(std::span<const std::byte> blob);
std::optional<CueSoundData> DecodeCueSoundBlob(std::span<const std::byte> blob);
std::optional<std::vector<std::byte>> DecodeAnimationSkeletonBlob(std::span<const std::byte> blob);
std::optional<ClipData> DecodeClipBlob(std::span<const std::byte> blob);
std::optional<StanceTransitionsData> DecodeStanceTransitionsBlob(std::span<const std::byte> blob);

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_PACK_DECODER_H_
