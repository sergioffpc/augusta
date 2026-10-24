#ifndef AUGUSTA_PACK_ENCODER_H_
#define AUGUSTA_PACK_ENCODER_H_

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"

/// \file
/// augusta_assets' pack encoder: the Encode* blob functions and WritePack
/// (ADR-0031/ADR-0032/ADR-0007), implemented in encoder.cpp/assets.cpp. The
/// write half of the pack's wire format, for the cooker, which links it
/// through its native module (tools/pack, ADR-0030), and for tests that build
/// packs; augusta/pack_decoder.h is its read half.
namespace augusta::assets {

/// Why an Encode* function refused its input.
enum class EncodeError {
  /// A count or length exceeded what the wire format's fields can hold, or
  /// this module's own pragmatic sanity limits (kMaxPathLength,
  /// kMaxMeshPoints, kMaxMeshIndices, kMaxSceneNodes, kMaxProperties,
  /// kMaxCueVariants).
  kTooLarge,
  /// A sound's bits per sample is not one a PCM WAV file holds (8, 16, 24 or
  /// 32).
  kUnsupportedSampleWidth,
  /// A sound's samples are not a whole number of samples of its width.
  kPartialSample,
  /// A cue's sound has no variant, or a gain that is not a finite number.
  kInvalidCueSound,
  /// A Clip's stride length is negative or not finite, a Foot contact of it
  /// lies outside it, or a stance transition's duration is not a positive
  /// finite number.
  kInvalidAnimation,
};

/// A phrase for error that follows the name of what was encoded, e.g. "exceeds
/// the pack's size limits": what the cooker tells whoever runs it.
std::string_view DescribeEncodeError(EncodeError error);

/// Encodes mesh into the pack's mesh-blob byte layout (ADR-0031). The exact
/// on-disk layout is otherwise an implementation detail, shared only with
/// Pack's own decode path.
std::expected<std::vector<std::byte>, EncodeError> EncodeMeshBlob(const MeshData& mesh);

/// Encodes scene into the pack's scene-blob byte layout (ADR-0032).
std::expected<std::vector<std::byte>, EncodeError> EncodeSceneBlob(const SceneData& scene);

/// Encodes a KTX2 file into the pack's texture-blob byte layout (ADR-0031).
std::expected<std::vector<std::byte>, EncodeError> EncodeTextureBlob(std::span<const std::byte> ktx2_bytes);

/// Encodes spawn_point into the pack's spawn-point-blob byte layout
/// (ADR-0031/ADR-0032). Collision blobs need no analogous
/// EncodeCollisionBlob - they reuse EncodeMeshBlob directly (see MeshData's
/// own comment).
std::expected<std::vector<std::byte>, EncodeError> EncodeSpawnPointBlob(const SpawnPointData& spawn_point);

/// Encodes hitbox into the pack's hitbox-blob byte layout (ADR-0040, ADR-0061):
/// its body part as one byte, its Joint's name as a length-prefixed string, then
/// its geometry as a mesh blob.
std::expected<std::vector<std::byte>, EncodeError> EncodeHitboxBlob(const HitboxData& hitbox);

/// Encodes eye into the pack's eye-blob byte layout (ADR-0040).
std::expected<std::vector<std::byte>, EncodeError> EncodeEyeBlob(const EyeData& eye);

/// Encodes rifle into the pack's rifle-blob byte layout (ADR-0064).
std::expected<std::vector<std::byte>, EncodeError> EncodeRifleBlob(const RifleData& rifle);

/// Encodes script, a Lua script's text, into the pack's script-blob byte layout
/// (ADR-0031): the text itself, with no framing.
std::expected<std::vector<std::byte>, EncodeError> EncodeScriptBlob(std::string_view script);

/// Encodes skeleton into the pack's skeleton-blob byte layout (ADR-0061):
/// kTooLarge past kMaxJoints Joints.
std::expected<std::vector<std::byte>, EncodeError> EncodeSkeletonBlob(const SkeletonData& skeleton);

/// Encodes archive, an ozz::animation::Skeleton archive, into the pack's
/// animation-skeleton blob byte layout (ADR-0066): the archive itself, with no
/// framing. kTooLarge past kMaxArchiveBytes.
std::expected<std::vector<std::byte>, EncodeError> EncodeAnimationSkeletonBlob(std::span<const std::byte> archive);

/// Encodes clip into the pack's clip-blob byte layout (ADR-0066):
/// kInvalidAnimation as the decoder would refuse it, kTooLarge past
/// kMaxArchiveBytes or kMaxFootContacts.
std::expected<std::vector<std::byte>, EncodeError> EncodeClipBlob(const ClipData& clip);

/// Encodes transitions into the pack's stance-transitions blob byte layout
/// (ADR-0066): kInvalidAnimation unless each duration is a positive finite
/// number.
std::expected<std::vector<std::byte>, EncodeError> EncodeStanceTransitionsBlob(
    const StanceTransitionsData& transitions);

/// Encodes characters, a scenario's character names in manifest order, into the
/// pack's character-list blob byte layout (ADR-0042): kTooLarge past
/// kMaxCharacters.
std::expected<std::vector<std::byte>, EncodeError> EncodeCharactersBlob(std::span<const std::string> characters);

/// Encodes sound into the pack's audio-blob byte layout (ADR-0020, ADR-0031):
/// kTooLarge past kMaxAudioBytes, and refused as the decoder would refuse it
/// otherwise.
std::expected<std::vector<std::byte>, EncodeError> EncodeAudioBlob(const AudioData& sound);

/// Encodes sound into the pack's cue-sound-blob byte layout (ADR-0031,
/// ADR-0041): kInvalidCueSound with no variant or a gain that is not finite,
/// kTooLarge past kMaxCueVariants variants.
std::expected<std::vector<std::byte>, EncodeError> EncodeCueSoundBlob(const CueSoundData& sound);

/// Why WritePack failed.
enum class WriteError {
  /// output_path (or its temporary file) could not be created, written,
  /// or renamed into place.
  kIoError,
  /// Two or more entries share the same path - ResolveMesh/ResolveScene
  /// would be ambiguous about which one they name.
  kDuplicatePath,
  /// entries.size(), an entry's path, or a blob exceeded this module's
  /// pragmatic size limits (kMaxEntries, kMaxPathLength, kMaxPackSize).
  kTooLarge,
};

/// A phrase for error that follows the pack's path, e.g. "has two entries that
/// share a path".
std::string_view DescribeWriteError(WriteError error);

/// Writes entries into a new pack file at output_path, in ADR-0031's
/// header/data/index/trailer write order, signing the trailer with
/// signing_key. Its header names client_pack, the hash of the client pack
/// cooked with it, when given (a server pack's). The write is atomic: entries
/// are assembled into a temporary file first, which is only renamed into place
/// at output_path once fully written. Returns the pack's hash, the one its
/// trailer signs, which a server pack's cook names as its client_pack.
std::expected<PackHash, WriteError> WritePack(const std::filesystem::path& output_path,
                                              const std::vector<AssetEntry>& entries,
                                              const Ed25519PrivateKey& signing_key,
                                              const std::optional<PackHash>& client_pack = std::nullopt);

}  // namespace augusta::assets

#endif  // AUGUSTA_PACK_ENCODER_H_
