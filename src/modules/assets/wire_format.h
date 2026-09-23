#ifndef AUGUSTA_ASSETS_WIRE_FORMAT_H_
#define AUGUSTA_ASSETS_WIRE_FORMAT_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Private (not under include/augusta/, never installed) byte-level
// primitives shared by encoder.cpp, decoder.cpp, and assets.cpp's own
// pack-container (header/index/trailer) assembly and parsing - the same
// length-prefixed little-endian encoding (ADR-0007) underlies every blob
// type's wire format and the container around them, so this is the one
// place both sides of every read/write pair agree on it.
namespace augusta::assets {

inline constexpr std::size_t kBitsPerByte = 8;
inline constexpr std::uint32_t kByteMask = 0xFFU;

// Pragmatic v1 sanity limits (review: "Não existem limites razoáveis para
// tamanho de pack, quantidade de entries ou path length"). Chosen to be
// far above any real v1 content while still rejecting a hostile or
// corrupt file long before it could cause a multi-gigabyte allocation.
// Every place that narrows a size_t count into a u32 wire field checks
// the relevant limit below first, which doubles as the narrowing guard:
// all limits are comfortably under UINT32_MAX. kMaxPathLength is also
// reused directly by assets.cpp's ValidateEntries (an AssetEntry's path
// is the same kind of string AppendString/ReadString cap here).
inline constexpr std::uint32_t kMaxPathLength = 4096;
inline constexpr std::uint32_t kMaxMeshPoints = 16'000'000;
inline constexpr std::uint32_t kMaxMeshIndices = 48'000'000;
inline constexpr std::uint32_t kMaxSceneNodes = 1'000'000;
inline constexpr std::uint32_t kMaxProperties = 256;
// A single BC7-compressed 8K DDS is well under this; comfortably above
// any real v1 texture while still rejecting a hostile/corrupt blob long
// before an oversized allocation.
inline constexpr std::uint32_t kMaxTextureBytes = 256U * 1024 * 1024;
// A Lua script is text an author wrote by hand; a megabyte is far beyond any
// real one and rejects a hostile blob before an oversized allocation.
inline constexpr std::uint32_t kMaxScriptBytes = 1U * 1024 * 1024;

// Bit flags for a scene node's optional references (encoder.cpp's
// EncodeSceneNode / decoder.cpp's DecodeSceneNode - see ADR-0032). Shared
// between the two files rather than duplicated so the encode and decode
// sides can never silently drift apart on what each bit means.
inline constexpr std::uint8_t kNodeHasMesh = 1U << 0;
inline constexpr std::uint8_t kNodeHasMaterial = 1U << 1;
inline constexpr std::uint8_t kNodeHasCollider = 1U << 2;
inline constexpr std::uint8_t kNodeHasHitbox = 1U << 3;
inline constexpr std::uint8_t kNodeIsSpawnPoint = 1U << 4;

// Every definition here is inline: the header is shared across
// assets.cpp/encoder.cpp/decoder.cpp, so each needs one definition across
// them, and no single one of those translation units calls every helper here
// (an inline function a TU leaves unused is no -Wunused-function warning).
inline void AppendU8(std::vector<std::byte>& buf, std::uint8_t value) { buf.push_back(static_cast<std::byte>(value)); }

inline void AppendU32(std::vector<std::byte>& buf, std::uint32_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    buf.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

inline void AppendU64(std::vector<std::byte>& buf, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    buf.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

inline void AppendF32(std::vector<std::byte>& buf, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(buf, bits);
}

inline void AppendChars(std::vector<std::byte>& buf, std::string_view chars) {
  for (char chr : chars) {
    buf.push_back(static_cast<std::byte>(static_cast<unsigned char>(chr)));
  }
}

inline void AppendBytes(std::vector<std::byte>& buf, std::span<const std::byte> data) {
  buf.insert(buf.end(), data.begin(), data.end());
}

// Appends a length-prefixed string, failing if it exceeds kMaxPathLength
// (used for names/paths/property keys/values alike - all the same kind of
// short authored string).
[[nodiscard]] inline bool AppendString(std::vector<std::byte>& buf, std::string_view str) {
  if (str.size() > kMaxPathLength) {
    return false;
  }
  AppendU32(buf, static_cast<std::uint32_t>(str.size()));
  AppendChars(buf, str);
  return true;
}

[[nodiscard]] inline bool AppendOptionalPath(std::vector<std::byte>& blob, const std::optional<std::string>& path) {
  return !path || AppendString(blob, *path);
}

// Reads primitive values out of a byte span left-to-right, failing (via
// std::nullopt) the moment a read would run past the end - the one place
// every decode path (blob decoders, plus assets.cpp's own header/index
// parsing) routes every bounds check through.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

  std::optional<std::uint8_t> ReadU8() {
    const auto bytes = ReadBytes(sizeof(std::uint8_t));
    if (!bytes) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>((*bytes)[0]);
  }

  std::optional<std::uint32_t> ReadU32() {
    const auto bytes = ReadBytes(sizeof(std::uint32_t));
    if (!bytes) {
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
      value |= static_cast<std::uint32_t>((*bytes)[i]) << (kBitsPerByte * i);
    }
    return value;
  }

  std::optional<std::uint64_t> ReadU64() {
    const auto bytes = ReadBytes(sizeof(std::uint64_t));
    if (!bytes) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
      value |= static_cast<std::uint64_t>((*bytes)[i]) << (kBitsPerByte * i);
    }
    return value;
  }

  std::optional<float> ReadF32() {
    const auto bits = ReadU32();
    if (!bits) {
      return std::nullopt;
    }
    float value = 0.0F;
    const std::uint32_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  std::optional<std::span<const std::byte>> ReadBytes(std::size_t count) {
    if (count > data_.size() - pos_) {
      return std::nullopt;
    }
    const auto result = data_.subspan(pos_, count);
    pos_ += count;
    return result;
  }

  // Reads a length-prefixed string no longer than kMaxPathLength - the
  // read-side counterpart of AppendString.
  std::optional<std::string> ReadString() {
    const auto length = ReadU32();
    if (!length || *length > kMaxPathLength) {
      return std::nullopt;
    }
    const auto bytes = ReadBytes(*length);
    if (!bytes) {
      return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
};

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_WIRE_FORMAT_H_
