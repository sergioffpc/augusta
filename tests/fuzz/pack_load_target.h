#ifndef AUGUSTA_TESTS_FUZZ_PACK_LOAD_TARGET_H_
#define AUGUSTA_TESTS_FUZZ_PACK_LOAD_TARGET_H_

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include "augusta/assets.h"

/// \file
/// What the pack_load fuzz target does with its input (ADR-0013,
/// tests/fuzz/README.md): sign it with the golden packs' committed test key,
/// load it, and read every asset the engine reads from a pack out of it. Shared
/// by the target (pack_load_fuzz.cpp) and its seeds test, which checks that
/// each seed reaches what it is there to reach.
namespace augusta::pack_load_fuzz {

/// The golden packs' test key pair (tests/fixtures/example-packs).
struct TestKey {
  assets::Ed25519PublicKey public_key{};
  assets::Ed25519PrivateKey private_key{};
};

/// The test key, read once per process; aborts when it cannot be read.
[[nodiscard]] const TestKey& GlobalTestKey();

/// The path of the texture seed's one texture, which ResolveEverything resolves
/// as a texture.
inline constexpr std::string_view kTexturePath = "Root/Floor/Material/Diffuse";

/// unsigned_pack, a pack without its trailer, given one signed with the test
/// key, then loaded through assets::Pack::Load. A trailer the fuzzer wrote
/// would fail verification before the index or any blob is parsed.
[[nodiscard]] std::expected<assets::Pack, assets::LoadError> LoadSigned(std::span<const std::uint8_t> unsigned_pack);

/// What ResolveEverything got through to, of what a seed is there to reach.
struct Reached {
  /// The Clips of every Character the pack lists loaded, as the server loads
  /// them: each one's ozz Skeleton and Clip archives parsed.
  bool clips_loaded = false;
  /// The texture at kTexturePath resolved.
  bool texture_resolved = false;
};

/// Resolves every asset of the example scenario out of pack, each path as every
/// type, then reads each Character the pack lists (the example's when the list
/// does not resolve) as the server's content loading does: its Hitboxes bound
/// to its Skeleton, its eye, and its Clips (animation::LoadClips).
Reached ResolveEverything(const assets::Pack& pack);

}  // namespace augusta::pack_load_fuzz

#endif  // AUGUSTA_TESTS_FUZZ_PACK_LOAD_TARGET_H_
