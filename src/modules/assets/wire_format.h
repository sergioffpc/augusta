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

#include "augusta/math.h"

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
// is the same kind of string WriteString/ReadString cap here).
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

// Writes the wire format into a caller-owned buffer. The writer does not own
// the buffer because encoders assemble several independent sections and blobs.
class ByteWriter {
 public:
  explicit ByteWriter(std::vector<std::byte>& data) : data_(data) {}

  void WriteU8(std::uint8_t value) { data_.push_back(static_cast<std::byte>(value)); }

  void WriteU32(std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
      data_.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
    }
  }

  void WriteU64(std::uint64_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
      data_.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
    }
  }

  void WriteF32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32(bits);
  }

  void WriteChars(std::string_view chars) {
    for (char chr : chars) {
      data_.push_back(static_cast<std::byte>(static_cast<unsigned char>(chr)));
    }
  }

  void WriteBytes(std::span<const std::byte> data) { data_.insert(data_.end(), data.begin(), data.end()); }

  void WriteVec3(const math::Vec3& value) {
    WriteF32(value.x);
    WriteF32(value.y);
    WriteF32(value.z);
  }

  void WriteQuat(const math::Quat& value) {
    WriteF32(value.x);
    WriteF32(value.y);
    WriteF32(value.z);
    WriteF32(value.w);
  }

  // Writes a length-prefixed string, failing if it exceeds kMaxPathLength
  // (used for names/paths/property keys/values alike - all the same kind of
  // short authored string).
  [[nodiscard]] bool WriteString(std::string_view str) {
    if (str.size() > kMaxPathLength) {
      return false;
    }
    WriteU32(static_cast<std::uint32_t>(str.size()));
    WriteChars(str);
    return true;
  }

  [[nodiscard]] bool WriteOptionalPath(const std::optional<std::string>& path) { return !path || WriteString(*path); }

 private:
  std::vector<std::byte>& data_;
};

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
  // read-side counterpart of WriteString.
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
