#ifndef AUGUSTA_ASSETS_PACK_FORMAT_H_
#define AUGUSTA_ASSETS_PACK_FORMAT_H_

#include <cstdint>

#include "augusta/assets.h"

/// \file
/// The pack container's layout (ADR-0031), private to the module: its format
/// version, the size of its header and trailer, and its container-only limits,
/// which pack_writer.cpp writes and pack_reader.cpp checks a pack against. The
/// bytes inside them are wire_format.h's.
namespace augusta::assets {

inline constexpr std::uint32_t kFormatVersion = 7;
/// The header's flags byte: whether it names a client pack's hash.
inline constexpr std::uint8_t kHeaderHasClientPack = 1U << 0U;
/// magic + version(u32) + data_offset(u64) + index_offset(u64) + index_count(u32)
/// + flags(u8) + client pack hash, per ADR-0031's header field list.
inline constexpr std::uint64_t kHeaderSize = kPackMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                                             sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint8_t) +
                                             kPackHashSize;
/// BLAKE3 hash + Ed25519 signature of that hash, ADR-0031's trailer.
inline constexpr std::uint64_t kTrailerSize = kPackHashSize + kEd25519SignatureSize;

/// Pragmatic sanity limits - see wire_format.h's own comment on
/// kMaxPathLength for the rest of this module's limits (shared with
/// encoder.cpp/decoder.cpp); these two are pack-container-only.
inline constexpr std::uint32_t kMaxEntries = 1U << 20;
inline constexpr std::uint64_t kMaxPackSize = 8ULL * 1024 * 1024 * 1024;

/// A pack's trailer (ADR-0031): BLAKE3 hash of the sections above,
/// followed by the Ed25519 signature of that hash. Shared shape for both
/// the write side (SignPack, freshly computed) and the read side
/// (ReadTrailer, read back off disk to verify against).
struct PackTrailer {
  PackHash hash;
  Ed25519Signature signature;
};

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_PACK_FORMAT_H_
